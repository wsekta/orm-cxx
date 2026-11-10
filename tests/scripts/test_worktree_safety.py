from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "worktree-safety.py"


class WorktreeSafetyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2])
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name)
        self.git("init", "-q")
        self.git("config", "user.name", "Test")
        self.git("config", "user.email", "test@example.invalid")
        (self.repo / ".gitignore").write_text("build/\n*.local\n", encoding="utf-8")
        (self.repo / "tracked.txt").write_text("original", encoding="utf-8")
        self.git("add", ".gitignore", "tracked.txt")
        self.git("commit", "-q", "-m", "initial")
        self.snapshot = self.repo / ".baseline.json"

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.repo), *args], check=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def run_script(self, command, *args):
        return subprocess.run([sys.executable, str(SCRIPT), command, "--snapshot", str(self.snapshot), *args],
                              cwd=self.repo, capture_output=True, text=True)

    def test_disposable_build_and_snapshot_do_not_block_clean_worktree(self):
        self.assertEqual(self.run_script("snapshot").returncode, 0)
        (self.repo / "build").mkdir()
        (self.repo / "build" / "output.local").write_text("object")
        result = self.run_script("check")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_new_untracked_and_ignored_files_block(self):
        self.assertEqual(self.run_script("snapshot").returncode, 0)
        (self.repo / "private.local").write_text("private")
        (self.repo / "notes.txt").write_text("notes")
        result = self.run_script("check")
        self.assertEqual(result.returncode, 1)
        self.assertIn("ignored new: private.local", result.stdout)
        self.assertIn("untracked new: notes.txt", result.stdout)

    def test_baseline_data_is_protected_and_content_changes_are_detected(self):
        private = self.repo / "private.local"
        private.write_text("old")
        self.assertEqual(self.run_script("snapshot").returncode, 0)
        result = self.run_script("check")
        self.assertEqual(result.returncode, 1)
        self.assertIn("ignored unchanged: private.local", result.stdout)
        private.write_text("new")
        result = self.run_script("check")
        self.assertIn("ignored changed: private.local", result.stdout)
        private.unlink()
        result = self.run_script("check")
        self.assertIn("ignored removed: private.local", result.stdout)

    def test_tracked_change_blocks_and_snapshot_is_not_overwritten(self):
        self.assertEqual(self.run_script("snapshot").returncode, 0)
        self.assertEqual(self.run_script("snapshot").returncode, 2)
        (self.repo / "tracked.txt").write_text("changed")
        result = self.run_script("check")
        self.assertEqual(result.returncode, 1)
        self.assertIn("tracked changes", result.stdout)

    def test_private_copies_only_exempt_when_equal_to_original(self):
        source_temp = tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2])
        self.addCleanup(source_temp.cleanup)
        source = Path(source_temp.name)
        for base in (self.repo, source):
            (base / "docs" / "agents").mkdir(parents=True)
            (base / "AGENTS.md").write_text("private instructions")
            (base / "docs" / "agents" / "workflow.md").write_text("original")
        self.assertEqual(self.run_script("snapshot").returncode, 0)
        result = self.run_script("check", "--private-source", str(source))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        (self.repo / "docs" / "agents" / "workflow.md").write_text("changed")
        result = self.run_script("check", "--private-source", str(source))
        self.assertEqual(result.returncode, 1)
        self.assertIn("differ from the original", result.stdout)
        (self.repo / "docs" / "agents" / "workflow.md").unlink()
        result = self.run_script("check", "--private-source", str(source))
        self.assertEqual(result.returncode, 1)

    def test_private_source_inside_worktree_is_rejected(self):
        source = self.repo / "source"
        source.mkdir()
        self.assertEqual(self.run_script("snapshot").returncode, 0)
        result = self.run_script("check", "--private-source", str(source))
        self.assertEqual(result.returncode, 2)
        self.assertIn("outside the worktree", result.stderr)

    def test_populated_submodule_path_blocks(self):
        head = self.git("rev-parse", "HEAD").stdout.decode().strip()
        self.git("update-index", "--add", "--cacheinfo", f"160000,{head},externals/example")
        self.git("commit", "-q", "-m", "add gitlink")
        self.assertEqual(self.run_script("snapshot").returncode, 0)
        path = self.repo / "externals" / "example"
        path.mkdir(parents=True, exist_ok=True)
        (path / "private-file").write_text("do not delete")
        result = self.run_script("check")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("populated submodule paths", result.stdout)


if __name__ == "__main__":
    unittest.main()
