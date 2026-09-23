"""Build reproducible release sources and central-registry recipe submissions."""

import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def read_version(root=ROOT):
    version = (root / "VERSION.txt").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        raise ValueError("VERSION.txt must contain a stable major.minor.patch version")
    return version


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def create_archive(root, archive, version):
    # The distribution deliberately excludes submodules: managers supply SOCI
    # and database client libraries. Normalize metadata and text line endings
    # so checked-out CRLF files produce the same release bytes as an LF checkout.
    entries = [root / name for name in (
        "CMakeLists.txt", "VERSION.txt", "LICENSE", "THIRD_PARTY_NOTICES.md", "README.md"
    )]
    for directory in ("cmake", "include", "src"):
        entries.extend(p for p in (root / directory).rglob("*") if p.is_file())
    archive.parent.mkdir(parents=True, exist_ok=True)
    with archive.open("wb") as raw, gzip.GzipFile(filename="", fileobj=raw, mode="wb", mtime=0) as gz:
        with tarfile.open(fileobj=gz, mode="w", format=tarfile.PAX_FORMAT) as tar:
            for path in sorted(entries, key=lambda item: item.relative_to(root).as_posix()):
                name = f"orm-cxx-{version}/{path.relative_to(root).as_posix()}"
                contents = path.read_bytes().replace(b"\r\n", b"\n")
                info = tarfile.TarInfo(name)
                info.size = len(contents)
                info.uid = info.gid = info.mtime = 0
                info.uname = info.gname = ""
                info.mode = 0o644
                tar.addfile(info, io.BytesIO(contents))


def stage_recipes(root, output, metadata, source_url=None):
    url = source_url or metadata["url"]
    port = output / "vcpkg" / "orm-cxx"
    port.mkdir(parents=True, exist_ok=True)
    template = root / "packaging" / "vcpkg" / "orm-cxx"
    manifest = json.loads((template / "vcpkg.json").read_text(encoding="utf-8"))
    manifest["version"] = metadata["version"]
    write_json(port / "vcpkg.json", manifest)
    text = (template / "portfile.cmake.in").read_text(encoding="utf-8")
    text = text.replace("@ORM_CXX_SOURCE_URL@", url)
    text = text.replace("@ORM_CXX_SOURCE_SHA512@", metadata["sha512"])
    (port / "portfile.cmake").write_text(text, encoding="utf-8", newline="\n")
    shutil.copyfile(template / "usage", port / "usage")
    recipe = output / "conan" / "recipes" / "orm-cxx"
    shutil.copytree(root / "packaging" / "conan", recipe / "all", dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("__pycache__", "build", "CMakeUserPresets.json"))
    # JSON is a YAML subset, and safely quotes URLs and version keys.
    write_json(recipe / "all" / "conandata.yml", {"sources": {
        metadata["version"]: {"url": url, "sha256": metadata["sha256"]}
    }})
    write_json(recipe / "config.yml", {"versions": {metadata["version"]: {"folder": "all"}}})


def prepare(output, repository="wsekta/orm-cxx", root=ROOT):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Invalid GitHub owner/repository")
    version = read_version(root)
    archive = output / f"orm-cxx-{version}.tar.gz"
    create_archive(root, archive, version)
    data = archive.read_bytes()
    metadata = {
        "version": version,
        "tag": f"v{version}",
        "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "repository": repository,
        "archive": archive.name,
        "url": f"https://github.com/{repository}/releases/download/v{version}/{archive.name}",
        "sha256": hashlib.sha256(data).hexdigest(),
        "sha512": hashlib.sha512(data).hexdigest(),
    }
    stage_recipes(root, output, metadata)
    write_json(output / "release.json", metadata)
    (output / "SHA256SUMS").write_text(f"{metadata['sha256']}  {archive.name}\n", encoding="utf-8")
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "distribution")
    parser.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY", "wsekta/orm-cxx"))
    args = parser.parse_args()
    metadata = prepare(args.output.resolve(), args.repository)
    print(json.dumps(metadata, indent=2))
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
            output.write(f"version={metadata['version']}\ntag={metadata['tag']}\n")


if __name__ == "__main__":
    main()
