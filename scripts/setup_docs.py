"""Install the pinned documentation tools locally on Linux/Windows x64.

Graphviz must be installed separately. Downloads and extracted tools stay in
build/docs-tools; no administrator privileges or system-wide changes are needed.
"""

import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tarfile
from urllib.request import urlopen
import zipfile


ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = json.loads((ROOT / "tools/docs-toolchain.json").read_text(encoding="utf-8"))
DOXYGEN_VERSION = TOOLCHAIN["doxygen"]["version"]
TOOLS = ROOT / "build/docs-tools"
THEME = TOOLS / f'doxygen-awesome-css-{TOOLCHAIN["theme"]["commit"]}'


def doxygen_binary():
    system = platform.system().lower()
    if system not in ("linux", "windows") or platform.machine().lower() not in ("x86_64", "amd64"):
        raise ValueError("The documentation installer supports Linux/Windows x64")
    return TOOLS / system / TOOLCHAIN["doxygen"][system]["binary"]


def download(url, name, digest):
    archive = TOOLS / "downloads" / name
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        with urlopen(url, timeout=120) as response:
            archive.write_bytes(response.read())
    with archive.open("rb") as downloaded:
        actual = hashlib.file_digest(downloaded, "sha256").hexdigest()
    if actual != digest:
        raise ValueError(f"SHA-256 mismatch for {name}: expected {digest}, got {actual}")
    return archive


def install():
    binary = doxygen_binary()
    system = platform.system().lower()
    package = TOOLCHAIN["doxygen"][system]
    tag = "Release_" + DOXYGEN_VERSION.replace(".", "_")
    archive = download(f'https://github.com/doxygen/doxygen/releases/download/{tag}/{package["archive"]}',
                       package["archive"], package["sha256"])
    destination = TOOLS / system
    destination.mkdir(parents=True, exist_ok=True)
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as package_zip:
            package_zip.extractall(destination)
    else:
        with tarfile.open(archive) as package_tar:
            package_tar.extractall(destination, filter="data")
    version = subprocess.check_output([str(binary), "--version"], text=True).strip()
    if version.split()[0] != DOXYGEN_VERSION:
        raise ValueError(f"Expected Doxygen {DOXYGEN_VERSION}, got {version}")
    theme = TOOLCHAIN["theme"]
    archive = download(f'https://codeload.github.com/jothepro/doxygen-awesome-css/tar.gz/{theme["commit"]}',
                       f'doxygen-awesome-css-{theme["version"]}.tar.gz', theme["sha256"])
    with tarfile.open(archive) as package_tar:
        package_tar.extractall(TOOLS, filter="data")
    if os.environ.get("GITHUB_PATH"):
        with open(os.environ["GITHUB_PATH"], "a", encoding="utf-8") as output:
            output.write(str(binary.parent) + "\n")
    print(f'Doxygen {version}; doxygen-awesome-css {theme["version"]}')
    print(f"Doxygen: {binary}")


if __name__ == "__main__":
    install()
