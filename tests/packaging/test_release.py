"""Release safety and source/recipe integrity checks; no network or credentials."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import prepare_packages as prepare
import publish_packages as publish
import check_packages as check


class PackageReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def test_version_rejects_tag_or_shell_input(self):
        for value in ("v1.2.3", "1.2", "01.2.3", "1.2.3; echo unsafe", "1.2.3\n4.5.6"):
            with self.subTest(value=value):
                (self.directory / "VERSION.txt").write_text(value)
                with self.assertRaises(ValueError):
                    prepare.read_version(self.directory)

    def test_sources_are_reproducible_and_contain_no_submodules(self):
        first = self.directory / "first.tar.gz"
        second = self.directory / "second.tar.gz"
        prepare.create_archive(prepare.ROOT, first, "0.1.0")
        prepare.create_archive(prepare.ROOT, second, "0.1.0")
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with tarfile.open(first) as archive:
            names = archive.getnames()
        self.assertIn("orm-cxx-0.1.0/modules/orm.cppm", names)
        self.assertIn("orm-cxx-0.1.0/modules/orm.reflection.cppm", names)
        for interface in (prepare.ROOT / "modules").rglob("*.cppm"):
            self.assertIn("orm-cxx-0.1.0/" + interface.relative_to(prepare.ROOT).as_posix(), names)
        self.assertIn("orm-cxx-0.1.0/scripts/export_llvm_coverage.py", names)
        self.assertIn("orm-cxx-0.1.0/VERSION.txt", names)
        self.assertFalse(any("/externals/" in name or "/.git/" in name for name in names))
        self.assertFalse(any("/include/orm-cxx/" in name for name in names))
        self.assertFalse(any(Path(name).suffix in (".pcm", ".ifc", ".gcm") for name in names))

    def test_sources_normalize_checkout_line_endings(self):
        source = self.directory / "source"
        for directory in ("cmake", "include", "src", "modules", "scripts"):
            (source / directory).mkdir(parents=True)
        for name in ("CMakeLists.txt", "VERSION.txt", "LICENSE", "THIRD_PARTY_NOTICES.md", "README.md"):
            (source / name).write_text("line one\nline two\n", encoding="utf-8", newline="\n")
        (source / "scripts/export_llvm_coverage.py").write_text("# coverage exporter\n", encoding="utf-8")
        candidate = source / "src" / "sample.cpp"
        candidate.write_bytes(b"line one\r\nline two\r\n")
        crlf_archive = self.directory / "crlf.tar.gz"
        lf_archive = self.directory / "lf.tar.gz"
        prepare.create_archive(source, crlf_archive, "0.1.0")
        candidate.write_bytes(b"line one\nline two\n")
        prepare.create_archive(source, lf_archive, "0.1.0")
        self.assertEqual(crlf_archive.read_bytes(), lf_archive.read_bytes())
        with tarfile.open(crlf_archive) as archive:
            contents = archive.extractfile("orm-cxx-0.1.0/src/sample.cpp").read()
        self.assertEqual(contents, b"line one\nline two\n")

    def test_generated_recipes_share_version_url_and_hashes(self):
        metadata = prepare.prepare(self.directory)
        manifest = json.loads((self.directory / "vcpkg/orm-cxx/vcpkg.json").read_text())
        data = json.loads((self.directory / "conan/recipes/orm-cxx/all/conandata.yml").read_text())
        port = (self.directory / "vcpkg/orm-cxx/portfile.cmake").read_text()
        self.assertEqual(manifest["version"], metadata["version"])
        self.assertEqual(data["sources"][metadata["version"]], {
            "url": metadata["url"], "sha256": metadata["sha256"]
        })
        self.assertIn(metadata["sha512"], port)
        self.assertIn(metadata["url"], port)
        self.assertNotIn("@ORM_CXX_", port)
        publish.validate_archive(self.directory, metadata)
        (self.directory / metadata["archive"]).write_bytes(b"changed")
        with self.assertRaises(ValueError):
            publish.validate_archive(self.directory, metadata)

    def test_installed_consumer_check_uses_prepared_recipes(self):
        distribution = self.directory / "distribution"
        metadata = prepare.prepare(distribution)
        source_url = f"http://127.0.0.1:12345/{metadata['archive']}"
        work = self.directory / "work"
        check.stage_test_recipes(distribution, work, metadata, source_url)
        self.assertIn(source_url, (work / "vcpkg/orm-cxx/portfile.cmake").read_text())
        conandata = json.loads((work / "conan/recipes/orm-cxx/all/conandata.yml").read_text())
        self.assertEqual(conandata["sources"][metadata["version"]]["url"], source_url)
        self.assertEqual(
            conandata["sources"][metadata["version"]]["sha256"], metadata["sha256"]
        )

    def test_version_merge_preserves_history_and_rejects_replacement(self):
        existing = self.directory / "existing.yml"
        incoming = self.directory / "incoming.yml"
        prepare.write_json(existing, {"sources": {"0.1.0": {"sha256": "old"}}})
        prepare.write_json(incoming, {"sources": {"0.2.0": {"sha256": "new"}}})
        publish.merge_versions(existing, incoming)
        merged = publish.yaml.safe_load(existing.read_text())["sources"]
        self.assertEqual(set(merged), {"0.1.0", "0.2.0"})
        before = existing.read_bytes()
        prepare.write_json(incoming, {"sources": {"0.1.0": {"sha256": "replacement"}}})
        with self.assertRaises(ValueError):
            publish.merge_versions(existing, incoming)
        self.assertEqual(before, existing.read_bytes())

    def test_release_refuses_retargeting_an_existing_version(self):
        metadata = prepare.prepare(self.directory)
        ref = [{"ref": f"refs/tags/{metadata['tag']}",
                "object": {"type": "commit", "sha": "different-commit"}}]
        with patch.object(publish, "gh_api", side_effect=[None, ref]) as api, \
             patch.object(publish, "run") as command:
            with self.assertRaisesRegex(ValueError, "another commit"):
                publish.release(self.directory, metadata)
        self.assertEqual(api.call_count, 2)
        command.assert_not_called()
        self.assertTrue(all(len(call.args) == 1 for call in api.call_args_list))

    def test_release_resolves_annotated_tags_before_comparing_commits(self):
        metadata = prepare.prepare(self.directory)
        ref = [{"ref": f"refs/tags/{metadata['tag']}",
                "object": {"type": "tag", "sha": "annotated-tag"}}]
        with patch.object(publish, "gh_api", side_effect=[
            None, ref, {"object": {"type": "commit", "sha": "different-commit"}}
        ]) as api, patch.object(publish, "run") as command:
            with self.assertRaisesRegex(ValueError, "another commit"):
                publish.release(self.directory, metadata)
        self.assertEqual(api.call_args_list[-1].args[0],
                         f"repos/{metadata['repository']}/git/tags/annotated-tag")
        command.assert_not_called()

    def test_gh_api_parses_headers_and_sends_json_payloads(self):
        response = subprocess.CompletedProcess(
            ["gh", "api"], 0,
            stdout='HTTP/2.0 201 Created\r\nContent-Type: application/json\r\n\r\n{"id": 42}\n',
            stderr="",
        )
        with patch.object(publish.subprocess, "run", return_value=response) as command:
            self.assertEqual(publish.gh_api("repos/example/repo/git/refs", {"sha": "tested"}),
                             {"id": 42})
        self.assertIn("--include", command.call_args.args[0])
        self.assertIn("--method", command.call_args.args[0])
        self.assertEqual(json.loads(command.call_args.kwargs["input"]), {"sha": "tested"})

    def test_gh_api_only_treats_actual_http_404_as_missing(self):
        cases = (
            (404, 1, True),
            (404, 1, False),
            (401, 1, True),
            (403, 1, True),
            (429, 1, True),
            (500, 1, True),
            (None, 1, True),
        )
        for status, returncode, allow_missing in cases:
            with self.subTest(status=status, allow_not_found=allow_missing):
                stdout = (f'HTTP/2.0 {status}\nContent-Type: application/json\n\n'
                          '{"message": "Not Found"}') if status else ""
                result = subprocess.CompletedProcess(
                    ["gh", "api"], returncode, stdout=stdout,
                    stderr="request failed: 404 is mentioned but no HTTP response was received",
                )
                with patch.object(publish.subprocess, "run", return_value=result):
                    if status == 404 and allow_missing:
                        self.assertIsNone(publish.gh_api("repos/example/repo", allow_not_found=True))
                    else:
                        with self.assertRaises(subprocess.CalledProcessError):
                            publish.gh_api("repos/example/repo", allow_not_found=allow_missing)

    def test_release_api_errors_stop_before_tag_or_asset_mutations(self):
        metadata = prepare.prepare(self.directory)
        for status in (401, 403, 429, None):
            with self.subTest(status=status):
                stdout = f'HTTP/2.0 {status}\n\n{{"message": "Failure"}}' if status else ""
                result = subprocess.CompletedProcess(["gh", "api"], 1, stdout=stdout,
                                                     stderr="request failed")
                with patch.object(publish.subprocess, "run", return_value=result) as api, \
                     patch.object(publish, "run") as command:
                    with self.assertRaises(subprocess.CalledProcessError):
                        publish.release(self.directory, metadata)
                self.assertEqual(api.call_count, 1)
                self.assertIn(f"repos/{metadata['repository']}/releases/tags/{metadata['tag']}",
                              api.call_args.args[0])
                command.assert_not_called()

    def test_release_checksum_errors_stop_before_github_calls(self):
        metadata = prepare.prepare(self.directory)
        for algorithm in ("sha256", "sha512"):
            with self.subTest(algorithm=algorithm), \
                 patch.object(publish, "gh_api") as api, \
                 patch.object(publish, "run") as command:
                changed = {**metadata, algorithm: "incorrect-checksum"}
                with self.assertRaisesRegex(ValueError, algorithm):
                    publish.release(self.directory, changed)
            api.assert_not_called()
            command.assert_not_called()

    def test_vcpkg_rerun_does_not_downgrade_a_newer_central_version(self):
        prepare.write_json(self.directory / "ports/orm-cxx/vcpkg.json", {"version": "0.2.0"})
        prepare.write_json(self.directory / "versions/o-/orm-cxx.json", {
            "versions": [{"version": "0.2.0"}, {"version": "0.1.0"}]
        })
        self.assertTrue(publish.vcpkg_already_published(self.directory, "0.1.0"))
        with self.assertRaisesRegex(ValueError, "downgrade"):
            publish.vcpkg_already_published(self.directory, "0.1.1")
        self.assertFalse(publish.vcpkg_already_published(self.directory, "0.3.0"))

    def test_conan_submission_requires_a_related_issue(self):
        self.assertEqual(publish.validate_conan_issue("123"), "123")
        for value in (None, "", "0", "-3", "12; git push"):
            with self.subTest(value=value):
                with self.assertRaisesRegex(ValueError, "CONANCENTER_ISSUE"):
                    publish.validate_conan_issue(value)

    def test_submission_identity_requires_expected_owner_and_positive_user_id(self):
        user = {"login": "wsekta", "id": 12345}
        with patch.object(publish, "gh_api", return_value=user) as api:
            self.assertEqual(publish.submission_identity(), user)
        api.assert_called_once_with("user")
        for invalid in ({"login": "another-user", "id": 12345},
                        *({"login": "wsekta", "id": value}
                          for value in (None, 0, -1, True, "12345"))):
            with self.subTest(user=invalid), \
                 patch.object(publish, "gh_api", return_value=invalid):
                with self.assertRaises(ValueError):
                    publish.submission_identity()

    def test_submission_rejects_another_account_before_creating_a_fork(self):
        metadata = prepare.prepare(self.directory)
        for manager in ("vcpkg", "conan"):
            with self.subTest(manager=manager), \
                 patch.object(publish, "gh_api", return_value={"login": "another-user", "id": 42}) as api, \
                 patch.object(publish, "run") as command:
                with self.assertRaises(ValueError):
                    publish.submit(self.directory, metadata, manager,
                                   self.directory / "submissions", conan_issue="123")
            api.assert_called_once_with("user")
            command.assert_not_called()
            self.assertFalse((self.directory / "submissions").exists())

    def test_preflight_validates_conan_issue_against_upstream(self):
        user = {"login": "wsekta", "id": 12345}
        issue = {"number": 123, "state": "open", "title": "[request] orm-cxx/0.1.0",
                 "html_url": "https://example.test/issues/123"}
        with patch.object(publish, "gh_api", side_effect=[user, issue]) as api:
            self.assertEqual(publish.validate_submission_configuration("123"), user)
        self.assertEqual(api.call_args_list[-1].args,
                         ("repos/conan-io/conan-center-index/issues/123",))
        with patch.object(publish, "gh_api", side_effect=[user, {**issue, "pull_request": {}}]):
            with self.assertRaises(ValueError):
                publish.validate_submission_configuration("123")
        with patch.object(publish, "gh_api", side_effect=[user, {**issue, "title": "another package"}]):
            with self.assertRaises(ValueError):
                publish.validate_submission_configuration("123")
        with patch.object(publish, "gh_api", side_effect=[user, {**issue, "state": "closed"}]):
            self.assertEqual(publish.validate_submission_configuration("123"), user)

    def test_preflight_rejects_invalid_issue_without_creating_remote_state(self):
        user = {"login": "wsekta", "id": 12345}
        with patch.object(publish, "gh_api", return_value=user) as api, \
             patch.object(publish, "run") as command:
            with self.assertRaisesRegex(ValueError, "CONANCENTER_ISSUE"):
                publish.validate_submission_configuration("0")
        self.assertTrue(all(call.args == ("user",) for call in api.call_args_list))
        command.assert_not_called()

    def test_submission_git_identity_matches_authenticated_account(self):
        user = {"login": "wsekta", "id": 12345}
        publish.run("git", "init", "--quiet", self.directory)
        publish.configure_submission_identity(self.directory, user)
        self.assertEqual(publish.run("git", "config", "--local", "--get", "user.name",
                                     cwd=self.directory), "wsekta")
        self.assertEqual(publish.run("git", "config", "--local", "--get", "user.email",
                                     cwd=self.directory), "12345+wsekta@users.noreply.github.com")

    def test_submission_commit_overrides_inherited_author_and_committer(self):
        user = {"login": "wsekta", "id": 12345}
        publish.run("git", "init", "--quiet", self.directory)
        publish.configure_submission_identity(self.directory, user)
        publish.run("git", "config", "commit.gpgsign", "false", cwd=self.directory)
        (self.directory / "package.txt").write_text("package change\n", encoding="utf-8")
        inherited = {"GIT_AUTHOR_NAME": "Unexpected author", "GIT_AUTHOR_EMAIL": "other@example.test",
                     "GIT_COMMITTER_NAME": "Unexpected committer",
                     "GIT_COMMITTER_EMAIL": "other@example.test"}
        with patch.dict(os.environ, inherited):
            publish.commit_changes(self.directory, "Add package", user=user)
        identity = publish.run("git", "log", "-1", "--format=%an|%ae|%cn|%ce", cwd=self.directory)
        self.assertEqual(identity,
                         "wsekta|12345+wsekta@users.noreply.github.com|"
                         "wsekta|12345+wsekta@users.noreply.github.com")

    def test_existing_submission_is_reused_without_changing_its_branch(self):
        pull_request = {"html_url": "https://example.test/pr/1", "state": "open", "merged_at": None}
        with patch.object(publish, "gh_api", return_value=[pull_request]):
            self.assertTrue(publish.existing_submission("upstream/repo", "owner", "orm-cxx-0.1.0"))
        pull_request["state"] = "closed"
        with patch.object(publish, "gh_api", return_value=[pull_request]):
            with self.assertRaisesRegex(ValueError, "closed without merging"):
                publish.existing_submission("upstream/repo", "owner", "orm-cxx-0.1.0")

    def test_release_refuses_overwriting_published_archive(self):
        metadata = prepare.prepare(self.directory)
        ref = [{"ref": f"refs/tags/{metadata['tag']}",
                "object": {"type": "commit", "sha": metadata["commit"]}}]
        state = {"draft": False, "assets": [{"name": metadata["archive"]}]}
        def download(*args, **kwargs):
            destination = Path(args[args.index("--dir") + 1])
            (destination / metadata["archive"]).write_bytes(b"published-original")
            return ""
        with patch.object(publish, "gh_api", side_effect=[state, ref]), \
             patch.object(publish, "run", side_effect=download) as upload:
            with self.assertRaisesRegex(ValueError, "differs"):
                publish.release(self.directory, metadata)
            self.assertEqual(upload.call_count, 1)
            self.assertEqual(upload.call_args.args[2], "download")

    def release_download(self, *args, **kwargs):
        if args[2] == "download":
            name = args[args.index("--pattern") + 1]
            destination = Path(args[args.index("--dir") + 1])
            shutil.copyfile(self.directory / name, destination / name)
        return ""

    def test_draft_release_resumes_by_adding_only_missing_assets(self):
        metadata = prepare.prepare(self.directory)
        ref = [{"ref": f"refs/tags/{metadata['tag']}",
                "object": {"type": "commit", "sha": metadata["commit"]}}]
        state = {"draft": True, "assets": [{"name": metadata["archive"]}]}
        with patch.object(publish, "gh_api", side_effect=[state, ref]) as api, \
             patch.object(publish, "run", side_effect=self.release_download) as command:
            publish.release(self.directory, metadata)
        self.assertTrue(all(len(call.args) == 1 for call in api.call_args_list))
        uploads = [call.args[4] for call in command.call_args_list if call.args[2] == "upload"]
        self.assertEqual(uploads, [self.directory / "SHA256SUMS", self.directory / "release.json"])
        self.assertEqual(command.call_args.args,
                         ("gh", "release", "edit", metadata["tag"], "--repo",
                          metadata["repository"], "--draft=false"))

    def test_complete_published_release_is_verified_without_mutations(self):
        metadata = prepare.prepare(self.directory)
        ref = [{"ref": f"refs/tags/{metadata['tag']}",
                "object": {"type": "commit", "sha": metadata["commit"]}}]
        state = {"draft": False,
                 "assets": [{"name": name} for name in (metadata["archive"], "SHA256SUMS", "release.json")]}
        with patch.object(publish, "gh_api", side_effect=[state, ref]) as api, \
             patch.object(publish, "run", side_effect=self.release_download) as command:
            publish.release(self.directory, metadata)
        self.assertTrue(all(len(call.args) == 1 for call in api.call_args_list))
        self.assertEqual(command.call_count, 3)
        self.assertTrue(all(call.args[2] == "download" for call in command.call_args_list))

    def test_draft_checks_all_existing_assets_before_adding_missing_ones(self):
        metadata = prepare.prepare(self.directory)
        ref = [{"ref": f"refs/tags/{metadata['tag']}",
                "object": {"type": "commit", "sha": metadata["commit"]}}]
        state = {"draft": True, "assets": [{"name": "release.json"}]}
        def corrupt_download(*args, **kwargs):
            if args[2] == "download":
                destination = Path(args[args.index("--dir") + 1])
                (destination / "release.json").write_bytes(b"different published metadata")
            return ""
        with patch.object(publish, "gh_api", side_effect=[state, ref]), \
             patch.object(publish, "run", side_effect=corrupt_download) as command:
            with self.assertRaisesRegex(ValueError, "differs"):
                publish.release(self.directory, metadata)
        self.assertEqual(command.call_count, 1)
        self.assertEqual(command.call_args.args[2], "download")

    def test_published_release_rejects_missing_asset_without_uploading(self):
        metadata = prepare.prepare(self.directory)
        ref = [{"ref": f"refs/tags/{metadata['tag']}",
                "object": {"type": "commit", "sha": metadata["commit"]}}]
        state = {"draft": False, "assets": [{"name": metadata["archive"]}]}
        with patch.object(publish, "gh_api", side_effect=[state, ref]), \
             patch.object(publish, "run", side_effect=self.release_download) as command:
            with self.assertRaisesRegex(ValueError, "Published release lacks"):
                publish.release(self.directory, metadata)
        self.assertTrue(all(call.args[2] == "download" for call in command.call_args_list))


if __name__ == "__main__":
    unittest.main()
