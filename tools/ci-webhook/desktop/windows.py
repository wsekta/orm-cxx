"""Windows lifecycle metadata and quiescent backups for the Desktop launcher.

This helper never sends application IPC requests, kills a process, removes a
source file, or restores a backup. JSON output contains metadata and hashes only.
"""

from __future__ import annotations

import argparse
import base64
import ctypes
import hashlib
import json
import os
import re
import shutil
import sqlite3
import sys
import uuid
from contextlib import contextmanager
from ctypes import wintypes
from pathlib import Path


PIPE_PREFIX = "codex-app-tools-"
PIPE_DIRECTORY = "\\\\.\\pipe\\"
HISTORY_TABLES = {"thread_items", "thread_realtime_items", "thread_turns"}
CONTEXT_TABLES = {"thread_attachments", "thread_dynamic_tools"}
THREAD_COLUMNS = ("thread_id", "threadId", "conversation_id", "conversationId")


def windows_api():
    if os.name != "nt":
        raise RuntimeError("This command requires Windows")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = (
        wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
        wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
    )
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.GetNamedPipeServerProcessId.argtypes = (
        wintypes.HANDLE, ctypes.POINTER(wintypes.ULONG),
    )
    kernel.GetNamedPipeServerProcessId.restype = wintypes.BOOL
    kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.GetProcessTimes.argtypes = (
        wintypes.HANDLE, ctypes.POINTER(wintypes.FILETIME), ctypes.POINTER(wintypes.FILETIME),
        ctypes.POINTER(wintypes.FILETIME), ctypes.POINTER(wintypes.FILETIME),
    )
    kernel.GetProcessTimes.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel.CloseHandle.restype = wintypes.BOOL
    return kernel


def process_handle(kernel, pid: int):
    # SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION. Keep a handle rather than
    # reopening a PID while waiting, so a reused PID cannot change our target.
    handle = kernel.OpenProcess(0x00100000 | 0x1000, False, pid)
    if not handle:
        error = ctypes.get_last_error()
        if error == 87:  # ERROR_INVALID_PARAMETER: process no longer exists.
            return None
        raise ctypes.WinError(error)
    return handle


def is_alive(kernel, handle) -> bool:
    result = kernel.WaitForSingleObject(handle, 0)
    if result == 0:  # WAIT_OBJECT_0
        return False
    if result == 0x102:  # WAIT_TIMEOUT
        return True
    raise ctypes.WinError(ctypes.get_last_error())


def alive(pid: int) -> dict:
    kernel = windows_api()
    handle = process_handle(kernel, pid)
    if handle is None:
        return {"alive": False}
    try:
        return {"alive": is_alive(kernel, handle)}
    finally:
        kernel.CloseHandle(handle)


def pipe(pid: int) -> dict:
    kernel = windows_api()
    matches = []
    invalid_handle = ctypes.c_void_p(-1).value
    for name in os.listdir(PIPE_DIRECTORY):
        if not re.fullmatch(PIPE_PREFIX + r"[0-9a-fA-F-]{36}", name):
            continue
        path = PIPE_DIRECTORY + name
        # FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        # OPEN_EXISTING. No read/write of application messages is performed.
        handle = kernel.CreateFileW(path, 0x80, 3, None, 3, 0, None)
        if handle == invalid_handle:
            continue
        try:
            owner = wintypes.ULONG()
            if not kernel.GetNamedPipeServerProcessId(handle, ctypes.byref(owner)):
                continue
            if owner.value == pid:
                matches.append(path)
        finally:
            kernel.CloseHandle(handle)
    if len(matches) != 1:
        raise RuntimeError(
            f"Expected one Desktop tools pipe owned by PID {pid}; found {len(matches)}"
        )
    return {"pipePath": matches[0], "serverProcessId": pid}


@contextmanager
def registered_process(kernel, handle, pid: int, expected_created: int | None = None):
    """Register exactly one process incarnation with Windows Restart Manager."""
    class UniqueProcess(ctypes.Structure):
        _fields_ = [("processId", wintypes.DWORD), ("started", wintypes.FILETIME)]

    class ProcessInfo(ctypes.Structure):
        _fields_ = [
            ("process", UniqueProcess), ("appName", wintypes.WCHAR * 256),
            ("serviceName", wintypes.WCHAR * 64), ("applicationType", wintypes.DWORD),
            ("appStatus", wintypes.ULONG), ("terminalSessionId", wintypes.DWORD),
            ("restartable", wintypes.BOOL),
        ]

    creation, exited, kernel_time, user_time = (wintypes.FILETIME() for _ in range(4))
    if not kernel.GetProcessTimes(
        handle, ctypes.byref(creation), ctypes.byref(exited),
        ctypes.byref(kernel_time), ctypes.byref(user_time),
    ):
        raise ctypes.WinError(ctypes.get_last_error())
    creation_ticks = (creation.dwHighDateTime << 32) | creation.dwLowDateTime
    if expected_created is not None and creation_ticks != expected_created:
        raise RuntimeError("Process creation time changed; refusing to close a reused PID")
    manager = ctypes.WinDLL("Rstrtmgr", use_last_error=True)
    manager.RmStartSession.argtypes = (
        ctypes.POINTER(wintypes.DWORD), wintypes.DWORD, wintypes.LPWSTR,
    )
    manager.RmStartSession.restype = wintypes.DWORD
    manager.RmRegisterResources.argtypes = (
        wintypes.DWORD, wintypes.UINT, ctypes.POINTER(wintypes.LPCWSTR),
        wintypes.UINT, ctypes.POINTER(UniqueProcess),
        wintypes.UINT, ctypes.POINTER(wintypes.LPCWSTR),
    )
    manager.RmRegisterResources.restype = wintypes.DWORD
    manager.RmGetList.argtypes = (
        wintypes.DWORD, ctypes.POINTER(wintypes.UINT), ctypes.POINTER(wintypes.UINT),
        ctypes.POINTER(ProcessInfo), ctypes.POINTER(wintypes.DWORD),
    )
    manager.RmGetList.restype = wintypes.DWORD
    manager.RmShutdown.argtypes = (wintypes.DWORD, wintypes.ULONG, ctypes.c_void_p)
    manager.RmShutdown.restype = wintypes.DWORD
    manager.RmEndSession.argtypes = (wintypes.DWORD,)
    manager.RmEndSession.restype = wintypes.DWORD
    session = wintypes.DWORD()
    key = ctypes.create_unicode_buffer(33)
    status = manager.RmStartSession(ctypes.byref(session), 0, key)
    if status:
        raise ctypes.WinError(status)
    try:
        process = UniqueProcess(pid, creation)
        status = manager.RmRegisterResources(session, 0, None, 1, ctypes.byref(process), 0, None)
        if status:
            raise ctypes.WinError(status)
        needed, count, reasons = wintypes.UINT(), wintypes.UINT(), wintypes.DWORD()
        status = manager.RmGetList(
            session, ctypes.byref(needed), ctypes.byref(count), None, ctypes.byref(reasons),
        )
        applications = []
        for _ in range(3):
            if status != 234:  # ERROR_MORE_DATA
                break
            count.value = needed.value
            entries = (ProcessInfo * count.value)()
            status = manager.RmGetList(
                session, ctypes.byref(needed), ctypes.byref(count), entries, ctypes.byref(reasons),
            )
            if status == 0:
                applications = [entries[index] for index in range(count.value)]
        if status:
            raise ctypes.WinError(status)
        plan = {
            "requestedPid": pid,
            # FILETIME exceeds JavaScript's safe integer range. Preserve its
            # exact 64-bit value across JSON and the launcher's configuration.
            "requestedCreationTime": str(creation_ticks),
            "impactedPids": sorted({entry.process.processId for entry in applications}),
            "rebootReasons": reasons.value,
        }
        yield manager, session, plan
    finally:
        manager.RmEndSession(session)


def shutdown_plan(pid: int) -> dict:
    kernel = windows_api()
    handle = process_handle(kernel, pid)
    if handle is None:
        return {"requestedPid": pid, "requestedCreationTime": None, "impactedPids": [], "rebootReasons": 0, "alreadyExited": True}
    try:
        if not is_alive(kernel, handle):
            return {"requestedPid": pid, "requestedCreationTime": None, "impactedPids": [], "rebootReasons": 0, "alreadyExited": True}
        with registered_process(kernel, handle, pid) as (_, _, plan):
            return plan
    finally:
        kernel.CloseHandle(handle)


def close(pid: int, timeout: float, expected_created: int | None = None) -> dict:
    kernel = windows_api()
    handle = process_handle(kernel, pid)
    if handle is None:
        return {"closed": True, "alreadyExited": True}
    try:
        if not is_alive(kernel, handle):
            return {"closed": True, "alreadyExited": True}
        with registered_process(kernel, handle, pid, expected_created) as (manager, session, plan):
            if plan["impactedPids"] != [pid]:
                raise RuntimeError("Restart Manager shutdown would not target exactly the requested process")
            if plan["rebootReasons"]:
                raise RuntimeError("Restart Manager reports reboot reasons; graceful shutdown was not requested")
            # Zero flags: do not use RmForceShutdown or demand that the
            # application registered for automatic application restart.
            status = manager.RmShutdown(session, 0, None)
            if status:
                raise ctypes.WinError(status)
            result = kernel.WaitForSingleObject(handle, int(timeout * 1000))
            if result == 0x102:
                raise RuntimeError("Desktop did not exit after graceful shutdown; no process was killed")
            if result != 0:
                raise ctypes.WinError(ctypes.get_last_error())
            return {"closed": True, "alreadyExited": False}
    finally:
        kernel.CloseHandle(handle)


def absolute_directory(value: str, *, exists: bool = True) -> Path:
    path = Path(value)
    if not path.is_absolute():
        raise ValueError("Directory arguments must be absolute")
    resolved = path.resolve(strict=exists)
    if exists and not resolved.is_dir():
        raise ValueError("Expected an existing directory")
    return resolved


def thread_uuid(value: str) -> str:
    normalized = str(uuid.UUID(value))
    if normalized != value.lower():
        raise ValueError("Thread must be a canonical UUID")
    return normalized


def source_files(home: Path, sqlite_home: Path, thread: str) -> list[tuple[str, Path, Path]]:
    files = []
    roots = [("home", home)]
    if sqlite_home != home:
        roots.append(("sqlite", sqlite_home))
    for label, root in roots:
        databases = sorted(set(root.glob("state_*.sqlite")) | set(root.glob("thread_history_*.sqlite")))
        for database in databases:
            for suffix in ("", "-wal", "-shm"):
                candidate = Path(str(database) + suffix)
                if candidate.is_file():
                    resolved = candidate.resolve(strict=True)
                    resolved.relative_to(root)
                    files.append((label, root, resolved))
    for candidate in (home / ".codex-global-state.json", home / ".codex-global-state.json.bak"):
        if candidate.is_file():
            resolved = candidate.resolve(strict=True)
            resolved.relative_to(home)
            files.append(("home", home, resolved))
    for folder_name in ("sessions", "archived_sessions"):
        folder = home / folder_name
        if not folder.is_dir():
            continue
        for candidate in sorted(folder.rglob(f"*{thread}*.jsonl")):
            resolved = candidate.resolve(strict=True)
            resolved.relative_to(home)
            # Match actual session identity as well as the filename. Never print
            # session metadata or transcript content.
            with resolved.open("r", encoding="utf-8") as stream:
                line = stream.readline(2 * 1024 * 1024)
            metadata = json.loads(line)
            if metadata.get("type") != "session_meta" or metadata.get("payload", {}).get("id") != thread:
                raise RuntimeError("A matching rollout filename has a different session identity")
            files.append(("home", home, resolved))
    return files


def digest_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def quote_identifier(value: str) -> str:
    return '"' + value.replace('"', '""') + '"'


def canonical_value(value):
    if isinstance(value, bytes):
        return {"blob": base64.b64encode(value).decode("ascii")}
    return value


def table_digest(db: sqlite3.Connection, table: str, column: str, thread: str) -> dict:
    columns = [row[1] for row in db.execute(f"PRAGMA table_info({quote_identifier(table)})")]
    statement = f"SELECT * FROM {quote_identifier(table)} WHERE {quote_identifier(column)} = ?"
    # Per-row hashes keep memory bounded by row count rather than transcript
    # size. Sorting also makes a table fingerprint independent of query order.
    row_hashes = []
    for row in db.execute(statement, (thread,)):
        encoded = json.dumps(
            [canonical_value(value) for value in row],
            ensure_ascii=False, separators=(",", ":"), allow_nan=False,
        ).encode("utf-8")
        row_hashes.append(hashlib.sha256(encoded).digest())
    digest = hashlib.sha256()
    digest.update(json.dumps(columns, separators=(",", ":")).encode("utf-8"))
    for value in sorted(row_hashes):
        digest.update(value)
    return {"table": table, "columns": columns, "count": len(row_hashes), "sha256": digest.hexdigest()}


def history(home: Path, sqlite_home: Path, thread: str) -> dict:
    output = {
        "thread": thread, "historyTables": [], "contextTables": [], "rollouts": [],
        "metadata": {"formatVersion": 1, "codexHome": str(home), "sqliteHome": str(sqlite_home)},
    }
    roots = sorted({home, sqlite_home}, key=str)
    for root in roots:
        databases = sorted(set(root.glob("state_*.sqlite")) | set(root.glob("thread_history_*.sqlite")))
        for database in databases:
            db = sqlite3.connect(database.resolve().as_uri() + "?mode=ro", uri=True, timeout=5)
            try:
                db.execute("PRAGMA query_only = ON")
                db.execute("BEGIN")
                tables = [row[0] for row in db.execute("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")]
                is_history_database = database.name.startswith("thread_history_")
                for table in tables:
                    columns = [row[1] for row in db.execute(f"PRAGMA table_info({quote_identifier(table)})")]
                    column = next((name for name in THREAD_COLUMNS if name in columns), None)
                    if column is None:
                        continue
                    # Projection watermarks and thread metadata may legitimately
                    # change on resume. They are backed up but are not history.
                    if table == "thread_history_projection_state":
                        continue
                    if is_history_database or table in HISTORY_TABLES:
                        category = "historyTables"
                    elif table in CONTEXT_TABLES:
                        category = "contextTables"
                    else:
                        continue
                    fingerprint = table_digest(db, table, column, thread)
                    fingerprint["database"] = str(database.resolve())
                    output[category].append(fingerprint)
            finally:
                db.close()
    for label, root, path in source_files(home, sqlite_home, thread):
        if path.suffix == ".jsonl":
            output["rollouts"].append({
                "path": str(path), "relativePath": str(path.relative_to(root)),
                "bytes": path.stat().st_size, "sha256": digest_file(path),
            })
    if not output["rollouts"] and not any(item["count"] for item in output["historyTables"]):
        raise RuntimeError("No persisted history found for the requested thread")
    return output


def idle(sqlite_home: Path, thread: str, ignore_turns: set[str]) -> dict:
    """Inspect all persisted turns; unknown/incomplete statuses are busy."""
    busy = set()
    latest = None
    inspected = 0
    ignored = set()
    terminal = {"completed", "failed", "interrupted"}
    for database in sorted(sqlite_home.glob("thread_history_*.sqlite")):
        db = sqlite3.connect(database.resolve().as_uri() + "?mode=ro", uri=True, timeout=5)
        try:
            db.execute("PRAGMA query_only = ON")
            db.execute("BEGIN")
            columns = {row[1] for row in db.execute('PRAGMA table_info("thread_turns")')}
            if not {"thread_id", "turn_id", "status", "rollout_ordinal"}.issubset(columns):
                raise RuntimeError("Unrecognized thread_turns schema; idle state is not established")
            inspected += 1
            for owner, turn_id, status in db.execute('SELECT thread_id, turn_id, status FROM "thread_turns"'):
                normalized = status.lower().replace("_", "").replace("-", "") if isinstance(status, str) else None
                if normalized not in terminal:
                    if turn_id in ignore_turns:
                        ignored.add(turn_id)
                        continue
                    busy.add(owner)
            row = db.execute(
                'SELECT turn_id, status, rollout_ordinal FROM "thread_turns" '
                'WHERE thread_id = ? ORDER BY rollout_ordinal DESC LIMIT 1', (thread,),
            ).fetchone()
            if row is not None and (latest is None or row[2] > latest[2]):
                latest = row
        finally:
            db.close()
    if inspected == 0 or latest is None:
        raise RuntimeError("Target turn history was not found; idle state is not established")
    return {
        "busyThreadIds": sorted(busy), "targetLatestTurnStatus": latest[1],
        "targetLastTurnId": latest[0], "ignoredTurnIds": sorted(ignored),
    }


def snapshot(home: Path, sqlite_home: Path, thread: str, destination: Path) -> dict:
    for root in {home, sqlite_home}:
        if destination == root or destination.is_relative_to(root):
            raise ValueError("Snapshot destination must be outside the source state directories")
    if destination.exists():
        raise FileExistsError("Snapshot destination must not already exist")
    fingerprint = history(home, sqlite_home, thread)
    files = source_files(home, sqlite_home, thread)
    if not any(path.name.startswith(("state_", "thread_history_")) for _, _, path in files):
        raise RuntimeError("No state databases found to snapshot")
    destination.mkdir(parents=True, exist_ok=False)
    manifest = {"thread": thread, "files": [], "history": fingerprint}
    for label, root, source in files:
        relative = Path(label) / source.relative_to(root)
        target = destination / relative
        target.resolve().relative_to(destination)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        manifest["files"].append({
            "source": str(source), "relativePath": str(relative), "bytes": source.stat().st_size,
        })
    (destination / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8",
    )
    (destination / "history.json").write_text(
        json.dumps(fingerprint, ensure_ascii=False, indent=2) + "\n", encoding="utf-8",
    )
    return {"destination": str(destination), "filesCopied": len(manifest["files"]), "history": fingerprint}


def positive_pid(value: str) -> int:
    pid = int(value)
    if not 0 < pid <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("PID must be a positive DWORD")
    return pid


def arguments() -> dict:
    """Parse a captured Windows command line, without executing it."""
    if os.name != "nt":
        raise RuntimeError("Windows command-line parsing requires Windows")
    line = json.load(sys.stdin)["commandLine"]
    if not isinstance(line, str) or "\0" in line:
        raise ValueError("Invalid captured command line")
    shell = ctypes.WinDLL("shell32", use_last_error=True)
    shell.CommandLineToArgvW.argtypes = (wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_int))
    shell.CommandLineToArgvW.restype = ctypes.POINTER(wintypes.LPWSTR)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.LocalFree.argtypes = (wintypes.HLOCAL,)
    kernel.LocalFree.restype = wintypes.HLOCAL
    count = ctypes.c_int()
    values = shell.CommandLineToArgvW(line, ctypes.byref(count))
    if not values:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        return {"arguments": [values[index] for index in range(count.value)]}
    finally:
        kernel.LocalFree(ctypes.cast(values, wintypes.HLOCAL))


def filetime_value(value: str) -> int:
    if not value.isdecimal():
        raise argparse.ArgumentTypeError("Creation time must be a decimal FILETIME value")
    ticks = int(value)
    if not 0 < ticks <= 0xFFFFFFFFFFFFFFFF:
        raise argparse.ArgumentTypeError("Creation time must be a positive 64-bit FILETIME value")
    return ticks


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("arguments")
    for name in ("pipe", "alive", "close", "shutdown-plan"):
        command = commands.add_parser(name)
        command.add_argument("--pid", type=positive_pid, required=True)
        if name == "close":
            command.add_argument("--timeout", type=float, default=30)
            command.add_argument("--expected-created", type=filetime_value)
    for name in ("history", "snapshot"):
        command = commands.add_parser(name)
        command.add_argument("--home", required=True)
        command.add_argument("--sqlite-home", required=True)
        command.add_argument("--thread", required=True)
        if name == "snapshot":
            command.add_argument("--destination", required=True)
    command = commands.add_parser("idle")
    command.add_argument("--sqlite-home", required=True)
    command.add_argument("--thread", required=True)
    command.add_argument("--ignore-turn", action="append", default=[])
    args = parser.parse_args()
    if args.command == "arguments":
        result = arguments()
    elif args.command == "pipe":
        result = pipe(args.pid)
    elif args.command == "alive":
        result = alive(args.pid)
    elif args.command == "shutdown-plan":
        result = shutdown_plan(args.pid)
    elif args.command == "close":
        if not 0 < args.timeout <= 60:
            raise ValueError("Close timeout must be greater than zero and at most 60 seconds")
        result = close(args.pid, args.timeout, args.expected_created)
    elif args.command == "idle":
        result = idle(
            absolute_directory(args.sqlite_home), thread_uuid(args.thread),
            {thread_uuid(value) for value in args.ignore_turn},
        )
    else:
        home = absolute_directory(args.home)
        sqlite_home = absolute_directory(args.sqlite_home)
        thread = thread_uuid(args.thread)
        if args.command == "history":
            result = history(home, sqlite_home, thread)
        else:
            destination = absolute_directory(args.destination, exists=False)
            result = snapshot(home, sqlite_home, thread, destination)
    print(json.dumps(result, ensure_ascii=False, separators=(",", ":")))


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        # The operations above do not include transcript values in error text.
        print(json.dumps({"error": str(error)}, ensure_ascii=False), file=sys.stderr)
        raise SystemExit(1)
