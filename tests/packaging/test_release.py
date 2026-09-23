"""Release safety and source/recipe integrity checks; no network or credentials."""

import json
from pathlib import Path
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
        self.assertIn("orm-cxx-0.1.0/include/orm-cxx/database.hpp", names)
        self.assertIn("orm-cxx-0.1.0/VERSION.txt", names)
        self.assertFalse(any("/externals/" in name or "/.git/" in name for name in names))

    def test_sources_normalize_checkout_line_endings(self):
        source = self.directory / "source"
        for directory in ("cmake", "include", "src"):
            (source / directory).mkdir(parents=True)
        for name in ("CMakeLists.txt", "VERSION.txt", "LICENSE", "THIRD_PARTY_NOTICES.md", "README.md"):
            (source / name).write_text("line one\nline two\n", encoding="utf-8", newline="\n")
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
        with patch.object(publish, "gh_api", return_value=ref) as api:
            with self.assertRaisesRegex(ValueError, "another commit"):
                publish.release(self.directory, metadata)
        self.assertEqual(api.call_count, 1)

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
        state = {"isDraft": False, "assets": [{"name": metadata["archive"]}]}
        def download(*args, **kwargs):
            destination = Path(args[args.index("--dir") + 1])
            (destination / metadata["archive"]).write_bytes(b"published-original")
            return ""
        with patch.object(publish, "gh_api", return_value=ref), \
             patch.object(publish.subprocess, "run") as command, \
             patch.object(publish, "run", side_effect=download) as upload:
            command.return_value.returncode = 0
            command.return_value.stdout = json.dumps(state)
            with self.assertRaisesRegex(ValueError, "differs"):
                publish.release(self.directory, metadata)
            self.assertEqual(upload.call_count, 1)
            self.assertEqual(upload.call_args.args[2], "download")


if __name__ == "__main__":
    unittest.main()
