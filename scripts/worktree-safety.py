#!/usr/bin/env python3
"""Inventory a worktree and report whether its contents can be discarded.

This tool never deletes a worktree or its files. See worktree-safety.md.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys


# Only these root-level directories are treated as disposable build products.
DISPOSABLE = ("build", ".cache", "vcpkg_installed")
VERSION = 1


def git(root, *args):
    result = subprocess.run(
        ["git", "-C", str(root), *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors="replace").strip())
    return result.stdout


def reparse_point(path):
    info = path.lstat()
    return stat.S_ISLNK(info.st_mode) or bool(
        getattr(info, "st_file_attributes", 0) & 0x400
    )


def disposable(root, relative):
    parts = Path(relative).parts
    if not parts or parts[0] not in DISPOSABLE:
        return False
    # A symlink or junction may point outside the worktree or at user data.
    directory = root / parts[0]
    return directory.is_dir() and not reparse_point(directory)


def fingerprint(path):
    info = path.lstat()
    if reparse_point(path):
        if not path.is_symlink():
            raise RuntimeError(f"unsupported reparse point: {path}")
        return {"type": "symlink", "target": os.readlink(path)}
    if not stat.S_ISREG(info.st_mode):
        raise RuntimeError(f"unsupported file type: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return {"type": "file", "size": info.st_size, "sha256": digest.hexdigest()}


def files(root, snapshot):
    entries = {}
    for category, options in (
        ("untracked", ("--others", "--exclude-standard")),
        ("ignored", ("--others", "--ignored", "--exclude-standard")),
    ):
        output = git(root, "ls-files", "-z", *options)
        for raw in output.split(b"\0"):
            if not raw:
                continue
            relative = os.fsdecode(raw)
            path = root / relative
            if path.absolute() == snapshot.absolute():
                continue
            if disposable(root, relative):
                continue
            entries[relative] = {"category": category, **fingerprint(path)}
    return entries


def initialized_submodules(root):
    # Inspect gitlinks directly: `git submodule` launches a shell on Windows,
    # and Git's ordinary untracked inventory does not enter submodule paths.
    result = []
    for entry in git(root, "ls-files", "--stage", "-z").split(b"\0"):
        if not entry:
            continue
        metadata, raw_path = entry.split(b"\t", 1)
        if not metadata.startswith(b"160000 "):
            continue
        relative = os.fsdecode(raw_path)
        path = root / relative
        if path.is_symlink() or (path.exists() and (not path.is_dir() or next(path.iterdir(), None) is not None)):
            result.append(relative)
    return sorted(set(result))


def private_manifest(root):
    """Inventory the two explicitly supported private instruction paths."""
    result = {}
    def fail_walk(error):
        raise error

    for relative in ("AGENTS.md", "docs/agents"):
        path = root
        for component in Path(relative).parts:
            path = path / component
            if reparse_point(path):
                raise RuntimeError(f"private instruction path is a link: {path}")
        if relative == "AGENTS.md":
            result[relative] = fingerprint(path)
            continue
        if not path.is_dir():
            raise RuntimeError(f"private instruction directory is missing: {path}")
        for directory, dirs, names in os.walk(path, followlinks=False, onerror=fail_walk):
            for name in dirs + names:
                item = Path(directory) / name
                if reparse_point(item):
                    raise RuntimeError(f"private instruction path is a link: {item}")
                key = item.relative_to(root).as_posix()
                result[key] = {"type": "directory"} if item.is_dir() else fingerprint(item)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("snapshot", "check"))
    parser.add_argument("--snapshot", required=True, type=Path)
    parser.add_argument("--private-source", type=Path, help="original repository root containing private instructions")
    args = parser.parse_args()
    if args.command == "snapshot" and args.private_source:
        parser.error("--private-source is only valid with check")

    root = Path(os.fsdecode(git(Path.cwd(), "rev-parse", "--show-toplevel").strip())).resolve()
    gitdir = Path(os.fsdecode(git(root, "rev-parse", "--absolute-git-dir").strip())).resolve()
    snapshot = args.snapshot.absolute()

    if args.command == "snapshot":
        state = {"version": VERSION, "worktree": str(root), "gitdir": str(gitdir),
                 "files": files(root, snapshot)}
        with snapshot.open("x", encoding="utf-8") as destination:
            json.dump(state, destination, ensure_ascii=True, indent=2, sort_keys=True)
            destination.write("\n")
        print(f"Snapshot written: {snapshot} ({len(state['files'])} protected files)")
        return 0

    with snapshot.open(encoding="utf-8") as source:
        baseline = json.load(source)
    if baseline.get("version") != VERSION or baseline.get("worktree") != str(root) or baseline.get("gitdir") != str(gitdir):
        raise RuntimeError("snapshot does not belong to this worktree")
    current = files(root, snapshot)
    previous = baseline["files"]
    blocked = False
    private_match = False
    if args.private_source:
        source_input = args.private_source.absolute()
        if any(reparse_point(path) for path in (source_input, *source_input.parents)):
            raise RuntimeError("private source path must not traverse a link")
        source = source_input.resolve()
        if source == root or root in source.parents:
            raise RuntimeError("private source must be outside the worktree")
        if private_manifest(root) == private_manifest(source):
            private_match = True
            print("Private AGENTS.md and docs/agents match the original byte-for-byte.")
        else:
            blocked = True
            print("BLOCKED: private AGENTS.md or docs/agents differ from the original.")
    tracked = git(root, "status", "--porcelain=v1", "--untracked-files=no", "--ignore-submodules=none", "-z")
    if tracked:
        blocked = True
        print("BLOCKED: tracked changes or dirty submodules:")
        for entry in tracked.split(b"\0"):
            if entry:
                print("  " + os.fsdecode(entry))
    submodules = initialized_submodules(root)
    if submodules:
        blocked = True
        print("BLOCKED: populated submodule paths need separate preservation:")
        for line in submodules:
            print("  " + line)
    for relative in sorted(set(previous) | set(current)):
        before, after = previous.get(relative), current.get(relative)
        status = "new" if before is None else "removed" if after is None else "changed" if before != after else "unchanged"
        category = (after or before)["category"]
        private = relative == "AGENTS.md" or relative.startswith("docs/agents/")
        if private_match and private and after is not None:
            continue
        print(f"BLOCKED: {category} {status}: {relative}")
        blocked = True
    if not blocked:
        print("No protected files or tracked changes detected. Disposable directories: " + ", ".join(DISPOSABLE))
    return 1 if blocked else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        print(f"worktree-safety: {error}", file=sys.stderr)
        sys.exit(2)
