# Worktree safety check

Before starting work, in the new worktree, create a baseline using an explicit
path that does not exist yet. Prefer a path outside the worktree, so the
snapshot itself does not prevent an ordinary `git worktree remove`:

```sh
python scripts/worktree-safety.py snapshot --snapshot ../worktree-baseline.json
```

Before archiving or removing the worktree, run:

```sh
python scripts/worktree-safety.py check --snapshot ../worktree-baseline.json
```

If this worktree contains private copies of the original checkout's
`AGENTS.md` and `docs/agents`, supply the original checkout root:

```sh
python scripts/worktree-safety.py check --snapshot ../worktree-baseline.json --private-source /path/to/original-checkout
```

The source must be outside the worktree and its path must not traverse a link.
Only those two paths are exempted, and only when their current contents and
directory structure match the source byte-for-byte. Missing paths, differences,
symlinks, junctions, or unsupported file types fail closed. A file that was in
the baseline but has since disappeared still blocks removal.

If the snapshot is inside the worktree it is excluded from the inventory, but
it may itself prevent removal because it is untracked. The check returns 0 when no
protected data or tracked changes are detected, 1 when removal is blocked, and
2 if the check cannot complete. It never removes files or the worktree. Keep
the snapshot until the worktree has been removed.

The report distinguishes new, changed, removed and unchanged untracked or
ignored files. All such files block removal, including files present at the
initial snapshot, because archiving may not preserve ignored files, subject to
the explicit private-copy exception above. File contents are hashed. Populated
submodule paths block automatic removal and need a separate preservation
decision.

Only root-level `build/`, `.cache/`, and `vcpkg_installed/` are considered
disposable, and only when they are real directories rather than symlinks or
Windows junctions. They must contain only reproducible artifacts: move any
valuable files out before running the check. No other build directory is
automatically exempt. The check does not authorize deleting another worktree;
confirm the target path, run the check immediately before removal, and use the
appropriate worktree removal mechanism.
