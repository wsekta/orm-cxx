import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { Journal } from './core.mjs';
import { loadConfig, codexSender } from './main.mjs';
import { createDispatcher } from './dispatcher.mjs';
import { acquireDispatcherLock } from './dispatcher-lock.mjs';
import { parseOptions } from './worktree.mjs';

// Drain only the local journal. No receiver connection is opened and no Worker
// record is acknowledged. A pre-send retry scheduled after startedAt stays held.
export async function runDraining(config, journal, sender, log = () => {}) {
  const lock = acquireDispatcherLock(journal.path ?? config.journalPath);
  const startedAt = Date.now();
  let dispatcher;
  try {
    dispatcher = createDispatcher(journal, sender, {
      concurrency: config.concurrency ?? 2,
      retryDelayMs: config.retryDelayMs ?? 1000,
      clock: () => startedAt,
      log
    });
    await dispatcher.idle();
    if (dispatcher.fault) throw dispatcher.fault;
    const status = journal.status();
    const pending = Object.entries(status.counts).filter(([state]) => state !== 'accepted').reduce((sum, [, count]) => sum + count, 0);
    const remaining = status.deliveries.filter(row => row.status !== 'accepted').map(row => {
      const nextAttempt = journal.waiting(row.id)?.next_attempt ?? 0;
      return { ...row, nextAttempt: nextAttempt || null, waitingFor: row.status === 'ready' ?
        (nextAttempt > startedAt ? 'backoff outside this pass' : 'conversation queue') : row.status };
    });
    return {
      mode: 'local-drain', startedAt, complete: pending === 0, pending,
      receiverConnected: false, counts: status.counts,
      remaining,
      remainingListTruncated: pending > remaining.length,
      schemaDowngradeVerified: false,
      rollback: pending ? 'Refused: local deliveries remain; preserve this journal and runtime.' :
        'Local deliveries settled; an old runtime still requires a separately verified compatible schema.'
    };
  } finally {
    try { await dispatcher?.stop(); } finally { lock.release(); }
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  let journal;
  try {
    const options = parseOptions(process.argv.slice(2), ['config'], ['config']);
    const config = loadConfig(options.config);
    journal = new Journal(config.journalPath);
    const report = await runDraining(config, journal, codexSender(config), line => process.stderr.write(line + '\n'));
    console.log(JSON.stringify(report));
    if (!report.complete) process.exitCode = 2;
  } catch (error) {
    console.error(error.message);
    if (journal) {
      try { console.log(JSON.stringify({ mode: 'local-drain', complete: false, error: error.message, status: journal.status() })); }
      catch { /* Preserve the original failure when the journal itself is unavailable. */ }
    }
    process.exitCode = 2;
  } finally { journal?.close(); }
}
