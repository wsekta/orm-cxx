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


def run(*args, cwd=None, env=None):
    return subprocess.check_output(list(map(str, args)), cwd=cwd, env=env, text=True).strip()


def gh_api(endpoint, payload=None, allow_not_found=False):
    command = ["gh", "api", endpoint, "--include"]
    if payload is not None:
        command += ["--method", "POST", "--input", "-"]
    result = subprocess.run(command, input=json.dumps(payload) if payload is not None else None,
                            text=True, capture_output=True)
    response = result.stdout.replace("\r\n", "\n")
    headers, separator, body = response.partition("\n\n")
    status = re.match(r"HTTP/\S+\s+(\d{3})(?:\s|$)", headers)
    if status and int(status[1]) == 404 and allow_not_found:
        return None
    result.check_returncode()
    if not separator or not status:
        raise ValueError("GitHub API response is missing HTTP status headers")
    if not 200 <= int(status[1]) < 300:
        raise subprocess.CalledProcessError(
            1, command, output=result.stdout, stderr=result.stderr
        )
    return json.loads(body) if body.strip() else None


def submission_identity(expected_owner="wsekta"):
    user = gh_api("user")
    login = user.get("login", "")
    if login.casefold() != expected_owner.casefold():
        raise ValueError(f"Package submissions require {expected_owner}, authenticated as {login}")
    if type(user.get("id")) is not int or user["id"] <= 0:
        raise ValueError("GitHub returned an invalid submission account id")
    return user


def configure_submission_identity(checkout, user):
    run("git", "config", "user.name", user["login"], cwd=checkout)
    run("git", "config", "user.email",
        f"{user['id']}+{user['login']}@users.noreply.github.com", cwd=checkout)


def validate_submission_configuration(conan_issue, expected_owner="wsekta"):
    user = submission_identity(expected_owner)
    issue_number = validate_conan_issue(conan_issue)
    issue = gh_api(f"repos/conan-io/conan-center-index/issues/{issue_number}")
    if "pull_request" in issue or "orm-cxx" not in issue.get("title", "").casefold():
        raise ValueError("CONANCENTER_ISSUE must identify an orm-cxx issue in ConanCenter")
    return user


def release_notes(metadata):
    return (
        f"# orm-cxx {metadata['version']}\n\n"
        "A C++20 object-relational mapper with compile-time aggregate reflection.\n\n"
        "## Features\n\n"
        "- SQLite and optional PostgreSQL backends with a shared portability contract.\n"
        "- Closed schemas, typed model mappings, and reflection for up to 128 fields.\n"
        "- CRUD, transactions, generated integer keys, nullable fields, projections, "
        "and aggregate queries.\n"
        "- One-to-one, one-to-many, and many-to-many relations, batched collection "
        "loading, and collection predicates.\n"
        "- Static library and standalone header-only reflection CMake targets.\n\n"
        "## Installation and requirements\n\n"
        "Requires C++20, CMake 3.22 or newer, and GCC 13+, Clang 18+, or MSVC 19.38+. "
        "The vcpkg and Conan recipes install SOCI and the enabled database client "
        "dependencies automatically. PostgreSQL applications need a running server.\n\n"
        f"[Package installation and backend options](https://github.com/"
        f"{metadata['repository']}/blob/{metadata['tag']}/docs/packaging.md).\n\n"
        "The workflow submits this release to microsoft/vcpkg and ConanCenter. "
        "Central installation becomes available after their review and acceptance; "
        "the GitHub release alone does not make the package available in those catalogs.\n\n"
        "## Source verification\n\n"
        f"Commit: `{metadata['commit']}`.\n\n"
        f"SHA256 for `{metadata['archive']}`: `{metadata['sha256']}`. "
        "The release includes SHA256SUMS and release.json with source provenance.\n"
    )


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
    # Read before creating even a tag: auth, rate-limit, and network failures
    # must not be mistaken for an absent release or cause partial publication.
    state = gh_api(f"repos/{repo}/releases/tags/{tag}", allow_not_found=True)
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

    if state is not None:
        # Never overwrite assets: source hashes in accepted central recipes are
        # immutable. On reruns verify any existing bytes before adding missing files.
        with tempfile.TemporaryDirectory() as temporary:
            assets = {item["name"] for item in state["assets"]}
            required = (archive.name, "SHA256SUMS", "release.json")
            for name in required:
                if name in assets:
                    run("gh", "release", "download", tag, "--repo", repo,
                        "--pattern", name, "--dir", temporary)
                    if (Path(temporary) / name).read_bytes() != (distribution / name).read_bytes():
                        raise ValueError(f"Existing release asset {name} differs; increase VERSION.txt")
                else:
                    if not state["draft"]:
                        raise ValueError(f"Published release lacks {name}; refusing to change it")
            for name in required:
                if name not in assets:
                    run("gh", "release", "upload", tag, distribution / name, "--repo", repo)
    else:
        notes = distribution / "release-notes.md"
        notes.write_text(release_notes(metadata), encoding="utf-8")
        run("gh", "release", "create", tag, archive, distribution / "SHA256SUMS",
            distribution / "release.json", "--repo", repo, "--verify-tag", "--draft",
            "--title", f"orm-cxx {metadata['version']}", "--notes-file", notes)
    if state is None or state["draft"]:
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


def commit_changes(checkout, message, user=None):
    run("git", "add", "--all", cwd=checkout)
    if run("git", "diff", "--cached", "--name-only", cwd=checkout):
        identity_env = None
        if user is not None:
            identity_env = os.environ.copy()
            for role in ("AUTHOR", "COMMITTER"):
                identity_env[f"GIT_{role}_NAME"] = user["login"]
                identity_env[f"GIT_{role}_EMAIL"] = (
                    f"{user['id']}+{user['login']}@users.noreply.github.com"
                )
        run("git", "commit", "-m", message, cwd=checkout, env=identity_env)


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


def submission_body(manager, metadata, conan_issue=None):
    details = (
        f"Upstream release: https://github.com/{metadata['repository']}/releases/tag/{metadata['tag']}\n\n"
        "A static C++20 library with SQLite enabled by default and optional PostgreSQL. "
        "The package manager supplies SOCI and the enabled database client dependencies. "
        "The source distribution contains no vendored dependency submodules.\n\n"
        "The upstream Packages workflow passed six configurations for this manager: "
        "SQLite, PostgreSQL, both backends, and core on Linux; SQLite and both backends "
        "on Windows. Installed consumers exercise SQLite CRUD, PostgreSQL linking and "
        "backend registration, and the standalone reflection target. Live PostgreSQL "
        "behavior is tested by the separate upstream integration workflow.\n\n"
        f"Validated source commit: `{metadata['commit']}`.\n\n"
        f"SHA256: `{metadata['sha256']}`.\n\n"
    )
    if manager == "conan":
        issue = validate_conan_issue(conan_issue)
        return (
            f"### Summary\n\nNew recipe: **orm-cxx/{metadata['version']}**\n\n"
            "#### Motivation\n\nMake orm-cxx available through ConanCenter with "
            "package-manager-provided dependencies.\n\n#### Details\n\n" + details
            + "- [x] Read the contributing guidelines.\n"
            "- [x] Checked for duplicate submissions before opening this PR.\n"
            "- [ ] Tested locally with at least one configuration using a recent Conan version.\n\n"
            "The recipe was validated in the upstream CI configurations listed above; "
            "the local-testing checkbox is left for a maintainer to confirm separately.\n\n"
            f"Fixes #{issue}\n"
        )
    return (
        f"Add orm-cxx {metadata['version']}, a C++20 object-relational mapper.\n\n" + details
        + "### New port checklist\n\n"
        "- [ ] Changes comply with the [maintainer guide]"
        "(https://learn.microsoft.com/en-us/vcpkg/contributing/maintainer-guide).\n"
        "- [ ] The packaged project is mature and ready for broad sharing with vcpkg users.\n"
        "  - [ ] Has a release at least 6 months old or 6 months of demonstrated public development.\n"
        "  - [ ] Is an official component of something else meeting that criterion.\n"
        "  - [ ] Some other reason (please explain).\n"
        "- [ ] The packaged project shows strong association with the chosen port name.\n"
        "  - [ ] The project is in Repology.\n"
        "  - [ ] The project is among the first web search results, with a screenshot attached.\n"
        "  - [ ] The port name follows the Owner-Project form.\n"
        "- [x] Optional dependencies of the build are controlled by the port.\n"
        "- [x] The versioning scheme matches upstream.\n"
        "- [x] The license declaration and installed copyright files match upstream.\n"
        "- [x] Sources come from the authoritative upstream release.\n"
        "- [x] Usage text is brief and accurate.\n"
        "- [x] The version database was generated with x-add-version.\n"
        "- [x] Exactly one version is added in the modified versions file.\n\n"
        "The upstream repository was created on 2023-12-14 and its public commit "
        "history starts on that date. The port name matches the upstream repository "
        "and documented CMake package name, orm-cxx. The naming and maturity "
        "checkboxes remain open for evidence and review. This first tagged release follows "
        "the existing development history and includes compiler, backend, and package "
        "validation.\n"
    )


def submit(distribution, metadata, manager, work, conan_issue=None, expected_owner="wsekta"):
    validate_archive(distribution, metadata)
    if manager == "conan":
        conan_issue = validate_conan_issue(conan_issue)
    upstream = "microsoft/vcpkg" if manager == "vcpkg" else "conan-io/conan-center-index"
    repo_name = upstream.split("/")[1]
    user = submission_identity(expected_owner)
    owner = user["login"]
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
    configure_submission_identity(checkout, user)
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
        commit_changes(checkout, f"[orm-cxx] Add version {version}", user)
        run(executable, "x-add-version", "orm-cxx", "--overwrite-version", cwd=checkout)
        commit_changes(checkout, f"[orm-cxx] Update version database for {version}", user)
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
        commit_changes(checkout, f"orm-cxx/{version}: add release", user)
    # Ordinary push only: retries never force-push over review changes.
    run("git", "push", "fork", f"HEAD:refs/heads/{branch}", cwd=checkout)
    if existing_submission(upstream, owner, branch):
        return
    body = work / f"{manager}-body.md"
    body.write_text(submission_body(manager, metadata, conan_issue), encoding="utf-8")
    print(run("gh", "pr", "create", "--repo", upstream, "--base", default_branch,
              "--head", f"{owner}:{branch}", "--title", f"[orm-cxx] Add {version}", "--body-file", body))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("preflight", "release", "vcpkg", "conan"))
    parser.add_argument("--distribution", type=Path)
    parser.add_argument("--work", type=Path, default=Path("build/submissions"))
    parser.add_argument("--conan-issue", default=os.environ.get("CONANCENTER_ISSUE"))
    parser.add_argument("--expected-owner", default=os.environ.get("PACKAGE_REGISTRY_OWNER", "wsekta"))
    args = parser.parse_args()
    if args.action == "preflight":
        user = validate_submission_configuration(args.conan_issue, args.expected_owner)
        print(f"Submission configuration verified for {user['login']} and ConanCenter #{args.conan_issue}")
        return
    if args.distribution is None:
        parser.error("--distribution is required for release and catalog submissions")
    distribution = args.distribution.resolve()
    metadata = json.loads((distribution / "release.json").read_text(encoding="utf-8"))
    if args.action == "release":
        release(distribution, metadata)
    else:
        submit(distribution, metadata, args.action, args.work.resolve(), args.conan_issue, args.expected_owner)


if __name__ == "__main__":
    main()
