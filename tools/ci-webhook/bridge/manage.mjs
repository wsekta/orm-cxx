import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { Journal } from './core.mjs';
import { loadConfig } from './main.mjs';
import { parseOptions, verifyWaitingPush } from './worktree.mjs';
import { acquireDispatcherLock } from './dispatcher-lock.mjs';

export function runManage(argv, { configLoader = loadConfig, openJournal = path => new Journal(path), verifyPush = verifyWaitingPush, lockFactory = acquireDispatcherLock } = {}) {
  const [action, ...rest] = argv;
  const requiredByAction = {
    status: ['config'], list: ['config'], close: ['config', 'branch', 'thread'],
    transfer: ['config', 'branch', 'from', 'thread'], adopt: ['config', 'event', 'thread'],
    reconcile: ['config', 'event', 'status', 'evidence']
  };
  if (!Object.hasOwn(requiredByAction, action)) throw new Error('Expected status, list, close, transfer, adopt or reconcile');
  const allowedByAction = {
    status: ['config', 'branch', 'thread'], list: ['config', 'branch', 'thread'],
    close: ['config', 'branch', 'thread'], transfer: ['config', 'branch', 'from', 'thread'],
    adopt: ['config', 'event', 'thread'], reconcile: ['config', 'event', 'status', 'evidence', 'thread']
  };
  const options = parseOptions(rest, allowedByAction[action], requiredByAction[action]);
  const config = configLoader(options.config);
  const journal = openJournal(config.journalPath);
  let recoveryLock;
  try {
    if (action === 'status' || action === 'list') {
      const filter = Object.fromEntries(['branch', 'thread'].filter(key => options[key]).map(key => [key, options[key]]));
      return { ...journal.status(filter), desktopDeliveryVerified: false };
    }
    if (action === 'close') return journal.closeBranch(config.repository, options.branch, options.thread);
    if (action === 'transfer') {
      const from = options.from.startsWith('[') ? JSON.parse(options.from)
        : options.from.includes(',') ? options.from.split(',') : options.from;
      return journal.transfer(config.repository, options.branch, from, options.thread);
    }
    if (action === 'adopt') {
      const record = journal.waiting(options.event);
      if (!record || !['received', 'waiting_for_route'].includes(record.status)) throw new Error('Event is not waiting for registration');
      if (record.notice.repository !== config.repository) throw new Error('Waiting event belongs to another repository');
      const evidence = verifyPush(record.notice);
      return journal.adopt(options.event, options.thread, evidence);
    }
    if (!['accepted', 'received'].includes(options.status)) throw new Error('--status must be accepted or received after inspecting the destination');
    recoveryLock = lockFactory(journal.path);
    return journal.reconcile(options.event, options.status, options.evidence, options.thread);
  } finally {
    try { journal.close(); } finally { recoveryLock?.release(); }
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try { console.log(JSON.stringify(runManage(process.argv.slice(2)))); }
  catch (error) { console.error(error.message); process.exitCode = 1; }
}
