"""Persisted-history and quiescent-backup checks, runnable on Linux in CI."""

import importlib.util
import json
import sqlite3
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch


HELPER_PATH = Path(__file__).resolve().parents[1] / "desktop" / "windows.py"
SPEC = importlib.util.spec_from_file_location("ci_webhook_windows", HELPER_PATH)
WINDOWS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(WINDOWS)

THREAD = "11111111-1111-4111-8111-111111111111"
OTHER_THREAD = "22222222-2222-4222-8222-222222222222"
COMPLETED_TURN = "33333333-3333-4333-8333-333333333333"
STALE_TURN = "44444444-4444-4444-8444-444444444444"
NEW_TURN = "55555555-5555-4555-8555-555555555555"


class DesktopStateTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ci-desktop-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.home = self.root / "home"
        self.home.mkdir()
        self.state_path = self.home / "state_5.sqlite"
        self.history_path = self.home / "thread_history_1.sqlite"
        with sqlite3.connect(self.state_path) as db:
            db.executescript(
                "CREATE TABLE threads (id TEXT PRIMARY KEY, title TEXT, updated_at INTEGER);"
                "CREATE TABLE thread_dynamic_tools (thread_id TEXT, position INTEGER, name TEXT);"
                "CREATE TABLE thread_read_receipts (thread_id TEXT, revision INTEGER);"
            )
            db.execute("INSERT INTO threads VALUES (?, ?, ?)", (THREAD, "Original title", 1))
            db.execute("INSERT INTO thread_dynamic_tools VALUES (?, ?, ?)", (THREAD, 0, "codex_app"))
            db.execute("INSERT INTO thread_read_receipts VALUES (?, ?)", (THREAD, 1))
        with sqlite3.connect(self.history_path) as db:
            db.executescript(
                "CREATE TABLE thread_history_projection_state "
                "(thread_id TEXT, next_rollout_byte_offset INTEGER, next_rollout_ordinal INTEGER);"
                "CREATE TABLE thread_items "
                "(thread_id TEXT, turn_id TEXT, item_id TEXT, rollout_ordinal INTEGER, item_json TEXT);"
                "CREATE TABLE thread_realtime_items "
                "(thread_id TEXT, item_id TEXT, rollout_ordinal INTEGER, item_json TEXT);"
                "CREATE TABLE thread_turns "
                "(thread_id TEXT, turn_id TEXT, rollout_ordinal INTEGER, status TEXT);"
            )
            db.execute("INSERT INTO thread_history_projection_state VALUES (?, ?, ?)", (THREAD, 100, 2))
            db.execute(
                "INSERT INTO thread_items VALUES (?, ?, ?, ?, ?)",
                (THREAD, COMPLETED_TURN, "user-item", 1, '{"role":"user","text":"Keep this history"}'),
            )
            db.execute(
                "INSERT INTO thread_items VALUES (?, ?, ?, ?, ?)",
                (OTHER_THREAD, COMPLETED_TURN, "other-item", 1, '{"role":"user","text":"Other history"}'),
            )
            db.execute("INSERT INTO thread_turns VALUES (?, ?, ?, ?)", (THREAD, COMPLETED_TURN, 2, "completed"))
        session_folder = self.home / "sessions" / "2026" / "10" / "27"
        session_folder.mkdir(parents=True)
        self.rollout = session_folder / f"rollout-2026-10-27T17-12-25-{THREAD}.jsonl"
        self.rollout.write_text(
            json.dumps({"type": "session_meta", "payload": {"id": THREAD}}) + "\n"
            + json.dumps({"type": "response_item", "payload": {"text": "Keep this history"}}) + "\n",
            encoding="utf-8",
        )
        (self.home / ".codex-global-state.json").write_text('{"selectedThread":"' + THREAD + '"}\n', encoding="utf-8")

    def fingerprint(self):
        return WINDOWS.history(self.home, self.home, THREAD)

    def source_bytes(self):
        return {
            path.relative_to(self.home).as_posix(): path.read_bytes()
            for path in self.home.rglob("*") if path.is_file()
        }

    def test_projection_watermarks_and_ui_metadata_do_not_change_history_fingerprint(self):
        before = self.fingerprint()
        with sqlite3.connect(self.state_path) as db:
            db.execute("UPDATE threads SET title = ?, updated_at = ? WHERE id = ?", ("New title", 99, THREAD))
            db.execute("UPDATE thread_read_receipts SET revision = 99 WHERE thread_id = ?", (THREAD,))
        with sqlite3.connect(self.history_path) as db:
            db.execute(
                "UPDATE thread_history_projection_state SET next_rollout_byte_offset = 999, "
                "next_rollout_ordinal = 77 WHERE thread_id = ?", (THREAD,),
            )
        after = self.fingerprint()
        self.assertEqual(before["historyTables"], after["historyTables"])
        self.assertEqual(before["rollouts"], after["rollouts"])
        self.assertEqual(before["contextTables"], after["contextTables"])

    def test_missing_target_content_changes_history_fingerprint(self):
        before = self.fingerprint()
        with sqlite3.connect(self.history_path) as db:
            db.execute("DELETE FROM thread_items WHERE thread_id = ?", (THREAD,))
        after = self.fingerprint()
        old_items = next(item for item in before["historyTables"] if item["table"] == "thread_items")
        new_items = next(item for item in after["historyTables"] if item["table"] == "thread_items")
        self.assertEqual(old_items["count"], 1)
        self.assertEqual(new_items["count"], 0)
        self.assertNotEqual(old_items["sha256"], new_items["sha256"])

    def test_other_conversation_changes_do_not_satisfy_target_history(self):
        before = self.fingerprint()
        with sqlite3.connect(self.history_path) as db:
            db.execute("UPDATE thread_items SET item_json = ? WHERE thread_id = ?", ('{"changed":true}', OTHER_THREAD))
        self.assertEqual(before["historyTables"], self.fingerprint()["historyTables"])

    def test_snapshot_copies_state_wal_and_rollout_without_modifying_sources(self):
        # Source processes are quiescent. Retain a real SQLite WAL and SHM while
        # checking that backup includes their bytes rather than only the DB file.
        db = sqlite3.connect(self.history_path)
        self.addCleanup(db.close)
        db.execute("PRAGMA journal_mode = WAL")
        db.execute("PRAGMA wal_autocheckpoint = 0")
        db.execute(
            "INSERT INTO thread_realtime_items VALUES (?, ?, ?, ?)",
            (THREAD, "voice-item", 3, '{"text":"Persisted voice"}'),
        )
        db.commit()
        before = self.source_bytes()
        destination = self.root / "snapshot"
        result = WINDOWS.snapshot(self.home, self.home, THREAD, destination)
        after = self.source_bytes()
        # SQLite readers may update SHM read marks. Durable DB/WAL/transcript
        # bytes must remain untouched; copy the resulting SHM as metadata too.
        def durable(files):
            return {name: content for name, content in files.items() if not name.endswith("-shm")}

        self.assertEqual(durable(before), durable(after))
        self.assertEqual(result["destination"], str(destination))
        for relative, content in after.items():
            self.assertEqual((destination / "home" / relative).read_bytes(), content)
        self.assertIn("thread_history_1.sqlite-wal", before)
        self.assertIn("thread_history_1.sqlite-shm", before)
        self.assertEqual(
            json.loads((destination / "history.json").read_text(encoding="utf-8")),
            result["history"],
        )
        self.assertEqual(
            json.loads((destination / "manifest.json").read_text(encoding="utf-8"))["history"],
            result["history"],
        )
        copied_database = destination / "home" / "thread_history_1.sqlite"
        restored = sqlite3.connect(copied_database.as_uri() + "?mode=ro", uri=True)
        try:
            saved_voice = restored.execute(
                "SELECT item_json FROM thread_realtime_items WHERE thread_id = ?", (THREAD,),
            ).fetchone()
            self.assertEqual(json.loads(saved_voice[0]), {"text": "Persisted voice"})
        finally:
            restored.close()

    def test_snapshot_rejects_inside_source_and_existing_destination(self):
        before = self.source_bytes()
        with self.assertRaises(ValueError):
            WINDOWS.snapshot(self.home, self.home, THREAD, self.home / "backup")
        self.assertFalse((self.home / "backup").exists())
        destination = self.root / "existing"
        destination.mkdir()
        marker = destination / "keep.txt"
        marker.write_text("Do not replace", encoding="utf-8")
        with self.assertRaises(FileExistsError):
            WINDOWS.snapshot(self.home, self.home, THREAD, destination)
        self.assertEqual(marker.read_text(encoding="utf-8"), "Do not replace")
        self.assertEqual(before, self.source_bytes())

    def test_idle_ignores_only_the_exact_stale_turn_and_not_new_work(self):
        with sqlite3.connect(self.history_path) as db:
            db.execute("INSERT INTO thread_turns VALUES (?, ?, ?, ?)", (OTHER_THREAD, STALE_TURN, 3, "inProgress"))
        self.assertEqual(WINDOWS.idle(self.home, THREAD, set())["busyThreadIds"], [OTHER_THREAD])
        ignored = WINDOWS.idle(self.home, THREAD, {STALE_TURN})
        self.assertEqual(ignored["busyThreadIds"], [])
        self.assertEqual(ignored["ignoredTurnIds"], [STALE_TURN])
        self.assertEqual(ignored["targetLastTurnId"], COMPLETED_TURN)
        with sqlite3.connect(self.history_path) as db:
            db.execute("INSERT INTO thread_turns VALUES (?, ?, ?, ?)", (OTHER_THREAD, NEW_TURN, 4, "inProgress"))
        new_work = WINDOWS.idle(self.home, THREAD, {STALE_TURN})
        self.assertEqual(new_work["busyThreadIds"], [OTHER_THREAD])
        self.assertEqual(new_work["ignoredTurnIds"], [STALE_TURN])

    def test_unknown_turn_status_is_busy(self):
        with sqlite3.connect(self.history_path) as db:
            db.execute("INSERT INTO thread_turns VALUES (?, ?, ?, ?)", (OTHER_THREAD, NEW_TURN, 4, "futureStatus"))
        self.assertEqual(WINDOWS.idle(self.home, THREAD, set())["busyThreadIds"], [OTHER_THREAD])

    def test_reused_pid_is_rejected_before_loading_restart_manager(self):
        ticks = 134000000000000001

        def process_times(_handle, creation, _exited, _kernel, _user):
            creation._obj.dwLowDateTime = ticks & 0xFFFFFFFF
            creation._obj.dwHighDateTime = ticks >> 32
            return True

        kernel = SimpleNamespace(GetProcessTimes=process_times)
        with patch.object(WINDOWS.ctypes, "WinDLL", create=True) as load_library:
            with self.assertRaisesRegex(RuntimeError, "reused PID"):
                with WINDOWS.registered_process(kernel, object(), 123, ticks + 1):
                    self.fail("A reused PID must not be registered for shutdown")
            load_library.assert_not_called()
        self.assertEqual(WINDOWS.filetime_value(str(ticks)), ticks)


if __name__ == "__main__":
    unittest.main()
