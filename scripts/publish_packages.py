"""Publish immutable release assets, then propose recipes to central catalogs.

Requires GitHub CLI authentication. Never uploads binaries to ConanCenter:
its maintainers build and publish accepted recipes using their infrastructure.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

import yaml


def run(*args, cwd=None):
    return subprocess.check_output(list(map(str, args)), cwd=cwd, text=True).strip()


def gh_api(endpoint, payload=None):
    command = ["gh", "api", endpoint]
    if payload is not None:
        command += ["--method", "POST", "--input", "-"]
    result = subprocess.run(command, input=json.dumps(payload) if payload is not None else None,
                            text=True, capture_output=True, check=True)
    return json.loads(result.stdout) if result.stdout.strip() else None


def validate_archive(distribution, metadata):
    archive = distribution / metadata["archive"]
    data = archive.read_bytes()
    for algorithm in ("sha256", "sha512"):
        if hashlib.new(algorithm, data).hexdigest() != metadata[algorithm]:
            raise ValueError(f"Release archive does not match its {algorithm} checksum")
    return archive


def release(distribution, metadata):
    archive = validate_archive(distribution, metadata)
    repo, tag = metadata["repository"], metadata["tag"]
    # Existing tags must identify exactly the tested commit. Resolve annotated
    # tags as well, so a rerun cannot silently publish a different source tree.
    tags = gh_api(f"repos/{repo}/git/matching-refs/tags/{tag}")
    matching = [ref for ref in tags if ref["ref"] == f"refs/tags/{tag}"]
    if matching:
        obj = matching[0]["object"]
        while obj["type"] == "tag":
            obj = gh_api(f"repos/{repo}/git/tags/{obj['sha']}")["object"]
        if obj["sha"] != metadata["commit"]:
            raise ValueError(f"{tag} already points to another commit; increase VERSION.txt")
    else:
        gh_api(f"repos/{repo}/git/refs", {
            "ref": f"refs/tags/{tag}", "sha": metadata["commit"]
        })

    existing = subprocess.run(["gh", "release", "view", tag, "--repo", repo,
                               "--json", "isDraft,assets"], capture_output=True, text=True)
    if existing.returncode == 0:
        state = json.loads(existing.stdout)
        # Never overwrite assets: source hashes in accepted central recipes are
        # immutable. On reruns verify any existing bytes before adding missing files.
        with tempfile.TemporaryDirectory() as temporary:
            assets = {item["name"] for item in state["assets"]}
            for name in (archive.name, "SHA256SUMS", "release.json"):
                if name in assets:
                    run("gh", "release", "download", tag, "--repo", repo,
                        "--pattern", name, "--dir", temporary)
                    if (Path(temporary) / name).read_bytes() != (distribution / name).read_bytes():
                        raise ValueError(f"Existing release asset {name} differs; increase VERSION.txt")
                else:
                    if not state["isDraft"]:
                        raise ValueError(f"Published release lacks {name}; refusing to change it")
                    run("gh", "release", "upload", tag, distribution / name, "--repo", repo)
    else:
        notes = distribution / "release-notes.md"
        notes.write_text(
            f"orm-cxx {metadata['version']}\n\n"
            "Source distribution with SHA256 verification. Package-manager consumers "
            "receive SOCI and the selected database client dependencies automatically.\n\n"
            "The workflow submits this release to microsoft/vcpkg and ConanCenter. "
            "Availability in those central catalogs follows their review and CI; "
            "creating this release alone does not publish a central package.\n",
            encoding="utf-8",
        )
        run("gh", "release", "create", tag, archive, distribution / "SHA256SUMS",
            distribution / "release.json", "--repo", repo, "--verify-tag", "--draft",
            "--title", f"orm-cxx {metadata['version']}", "--notes-file", notes)
    run("gh", "release", "edit", tag, "--repo", repo, "--draft=false")
    print(f"Published https://github.com/{repo}/releases/tag/{tag}")


def merge_versions(destination, source):
    old = yaml.safe_load(destination.read_text(encoding="utf-8")) if destination.exists() else {}
    new = yaml.safe_load(source.read_text(encoding="utf-8"))
    old = old or {}
    for section, entries in new.items():
        target = old.setdefault(section, {})
        for version, value in entries.items():
            if version in target and target[version] != value:
                raise ValueError(f"Refusing to replace existing {section}/{version} in {destination}")
            target[version] = value
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(yaml.safe_dump(old, sort_keys=False), encoding="utf-8")


def commit_changes(checkout, message):
    run("git", "add", "--all", cwd=checkout)
    if run("git", "diff", "--cached", "--name-only", cwd=checkout):
        run("git", "commit", "-m", message, cwd=checkout)


def vcpkg_already_published(checkout, version):
    history = checkout / "versions/o-/orm-cxx.json"
    if history.exists():
        for entry in json.loads(history.read_text(encoding="utf-8"))["versions"]:
            if any(entry.get(key) == version for key in ("version", "version-semver", "version-string")):
                return True
    manifest = checkout / "ports/orm-cxx/vcpkg.json"
    if manifest.exists():
        data = json.loads(manifest.read_text(encoding="utf-8"))
        current = data.get("version", data.get("version-semver", data.get("version-string")))
        if current == version:
            return True
        if current and tuple(map(int, current.split("."))) > tuple(map(int, version.split("."))):
            raise ValueError(f"Refusing to downgrade central orm-cxx from {current} to {version}")
    return False


def validate_conan_issue(issue):
    if not isinstance(issue, str) or not re.fullmatch(r"[1-9][0-9]*", issue):
        raise ValueError("Set CONANCENTER_ISSUE to the positive number of the related ConanCenter issue")
    return issue


def existing_submission(upstream, owner, branch):
    prs = gh_api(f"repos/{upstream}/pulls?head={owner}:{branch}&state=all")
    if not prs:
        return False
    pull_request = prs[0]
    print(pull_request["html_url"])
    if pull_request["state"] != "open" and not pull_request.get("merged_at"):
        raise ValueError("Previous submission was closed without merging; maintainer action is required")
    return True


def submit(distribution, metadata, manager, work, conan_issue=None):
    validate_archive(distribution, metadata)
    if manager == "conan":
        conan_issue = validate_conan_issue(conan_issue)
    upstream = "microsoft/vcpkg" if manager == "vcpkg" else "conan-io/conan-center-index"
    repo_name = upstream.split("/")[1]
    owner = gh_api("user")["login"]
    fork = f"{owner}/{repo_name}"
    # GitHub's standard GITHUB_TOKEN cannot create forks or cross-repository PRs.
    # This job is deliberately authenticated using PACKAGE_REGISTRY_TOKEN.
    fork_data = gh_api(f"repos/{upstream}/forks", {"default_branch_only": True})
    if fork_data["full_name"].lower() != fork.lower():
        raise ValueError("GitHub returned an unexpected fork owner")
    for attempt in range(30):
        try:
            fork_info = gh_api(f"repos/{fork}")
            if fork_info.get("parent", {}).get("full_name", "").lower() != upstream.lower():
                raise ValueError(f"{fork} is not a fork of {upstream}")
            break
        except subprocess.CalledProcessError:
            if attempt == 29:
                raise
            time.sleep(2)
    default_branch = gh_api(f"repos/{upstream}")["default_branch"]
    checkout = work / manager
    if checkout.exists():
        raise ValueError(f"Use a fresh submission work directory: {checkout}")
    checkout.parent.mkdir(parents=True, exist_ok=True)
    run("git", "clone", "--depth", "1", "https://github.com/" + upstream + ".git", checkout)
    run("git", "config", "user.name", "orm-cxx release automation", cwd=checkout)
    run("git", "config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com", cwd=checkout)
    version = metadata["version"]
    if manager == "vcpkg":
        if vcpkg_already_published(checkout, version):
            print(f"orm-cxx {version} is already in {upstream}")
            return
    else:
        central = checkout / "recipes/orm-cxx/config.yml"
        if central.exists() and version in yaml.safe_load(central.read_text()).get("versions", {}):
            print(f"orm-cxx {version} is already in {upstream}")
            return
    branch = f"orm-cxx-{version}"
    # An existing PR may contain reviewer-requested edits. Its branch is never
    # overwritten by a retry of this immutable release.
    if existing_submission(upstream, owner, branch):
        return
    run("git", "remote", "add", "fork", f"https://github.com/{fork}.git", cwd=checkout)
    run("gh", "auth", "setup-git")
    refs = run("git", "ls-remote", "--heads", "fork", branch, cwd=checkout)
    if refs:
        run("git", "fetch", "--depth", "1", "fork", branch, cwd=checkout)
        run("git", "checkout", "-b", branch, "FETCH_HEAD", cwd=checkout)
    else:
        run("git", "checkout", "-b", branch, cwd=checkout)

    if manager == "vcpkg":
        shutil.copytree(distribution / "vcpkg/orm-cxx", checkout / "ports/orm-cxx", dirs_exist_ok=True)
        run("bash", "bootstrap-vcpkg.sh", "-disableMetrics", cwd=checkout)
        executable = checkout / "vcpkg"
        run(executable, "format-manifest", checkout / "ports/orm-cxx/vcpkg.json", cwd=checkout)
        commit_changes(checkout, f"[orm-cxx] Add version {version}")
        run(executable, "x-add-version", "orm-cxx", "--overwrite-version", cwd=checkout)
        commit_changes(checkout, f"[orm-cxx] Update version database for {version}")
    else:
        source = distribution / "conan/recipes/orm-cxx"
        destination = checkout / "recipes/orm-cxx"
        merge_versions(destination / "config.yml", source / "config.yml")
        merge_versions(destination / "all/conandata.yml", source / "all/conandata.yml")
        for path in (source / "all").rglob("*"):
            if path.is_file() and path.name != "conandata.yml":
                target = destination / "all" / path.relative_to(source / "all")
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, target)
        commit_changes(checkout, f"orm-cxx/{version}: add release")
    # Ordinary push only: retries never force-push over review changes.
    run("git", "push", "fork", f"HEAD:refs/heads/{branch}", cwd=checkout)
    if existing_submission(upstream, owner, branch):
        return
    body = work / f"{manager}-body.md"
    body.write_text(
        f"Add orm-cxx {version}, a C++20 ORM with SQLite and optional PostgreSQL support.\n\n"
        f"Upstream release: https://github.com/{metadata['repository']}/releases/tag/{metadata['tag']}\n\n"
        "The upstream Packages workflow builds the release archive through both managers "
        "and runs installed-consumer tests, including SQLite writes/reads and backend selection. "
        "Dependencies are supplied by the package manager; no vendored libraries are included.\n\n"
        "This submission is generated by the upstream release workflow and is ready for review.\n"
        + (f"\nFixes #{conan_issue}\n" if manager == "conan" else ""),
        encoding="utf-8",
    )
    print(run("gh", "pr", "create", "--repo", upstream, "--base", default_branch,
              "--head", f"{owner}:{branch}", "--title", f"[orm-cxx] Add {version}", "--body-file", body))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("release", "vcpkg", "conan"))
    parser.add_argument("--distribution", type=Path, required=True)
    parser.add_argument("--work", type=Path, default=Path("build/submissions"))
    parser.add_argument("--conan-issue", default=os.environ.get("CONANCENTER_ISSUE"))
    args = parser.parse_args()
    distribution = args.distribution.resolve()
    metadata = json.loads((distribution / "release.json").read_text(encoding="utf-8"))
    if args.action == "release":
        release(distribution, metadata)
    else:
        submit(distribution, metadata, args.action, args.work.resolve(), args.conan_issue)


if __name__ == "__main__":
    main()
