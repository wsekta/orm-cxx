import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { Journal } from './core.mjs';
import { loadConfig } from './main.mjs';
import { inspectWorktree, parseOptions } from './worktree.mjs';

export function runRegister(argv, { configLoader = loadConfig, openJournal = path => new Journal(path), inspect = inspectWorktree } = {}) {
  const options = parseOptions(argv, ['config', 'worktree', 'branch', 'sha', 'thread'], ['config', 'worktree', 'branch', 'sha', 'thread']);
  const config = configLoader(options.config);
  const checked = inspect(options.worktree, { repository: config.repository, branch: options.branch, sha: options.sha });
  const journal = openJournal(config.journalPath);
  try {
    const registration = journal.register(checked.repository, checked.branch, checked.sha, options.thread, checked.worktree);
    return { registered: true, registration, desktopDeliveryVerified: false };
  } finally { journal.close(); }
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try { console.log(JSON.stringify(runRegister(process.argv.slice(2)))); }
  catch (error) { console.error(error.message); process.exitCode = 1; }
}
