import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Journal } from '../bridge/core.mjs';
import { acquireDispatcherLock } from '../bridge/dispatcher-lock.mjs';
import { runDraining } from '../bridge/drain.mjs';

const repository = 'wsekta/orm-cxx', sha = 'e'.repeat(40);
const firstThread = '11111111-1111-1111-1111-111111111111';
const secondThread = '22222222-2222-2222-2222-222222222222';
function notice(branch, sequence = 1) {
  return { id: `${sha}:branch:${createHash('sha256').update(branch).digest('hex')}:failure:${sequence}:1`, repository, branch, sha,
    outcome: 'failure', url: `https://github.com/${repository}/actions/runs/${sequence}` };
}
function fixture(t) {
  const folder = mkdtempSync(join(tmpdir(), 'ci-local-drain-'));
  const journal = new Journal(join(folder, 'journal.sqlite'));
  t.after(() => { journal.close(); rmSync(folder, { recursive: true }); });
  // No endpoint/token: a local drain must not need a receiver connection.
  return { journal, config: { journalPath: journal.path, concurrency: 2 } };
}

test('local drain settles queued deliveries without a receiver or an upstream ACK', { timeout: 5000 }, async t => {
  const { journal, config } = fixture(t);
  const first = notice('config/first'), later = notice('config/first', 2), second = notice('config/second');
  journal.register(repository, first.branch, sha, firstThread);
  journal.register(repository, second.branch, sha, secondThread);
  for (const event of [first, later, second]) journal.intake(event);
  const sent = [];
  const report = await runDraining(config, journal, { probe: async () => {}, send: async thread => sent.push(thread) });
  assert.equal(report.complete, true);
  assert.equal(report.pending, 0);
  assert.equal(report.receiverConnected, false);
  assert.equal(report.schemaDowngradeVerified, false);
  assert.deepEqual(sent, [firstThread, secondThread, firstThread]);
  assert.equal(journal.status().deliveries.every(event => event.receiverReleased === null), true);
  const lock = acquireDispatcherLock(journal.path);
  lock.release();
});

test('a missing route remains pending while an eligible destination drains', { timeout: 5000 }, async t => {
  const { journal, config } = fixture(t);
  const unregistered = notice('config/waiting'), registered = notice('config/ready');
  journal.register(repository, registered.branch, sha, secondThread);
  journal.intake(unregistered); journal.intake(registered);
  const sent = [];
  const report = await runDraining(config, journal, { probe: async () => {}, send: async thread => sent.push(thread) });
  assert.deepEqual(sent, [secondThread]);
  assert.equal(report.complete, false);
  assert.equal(report.pending, 1);
  assert.equal(report.remaining[0].status, 'waiting_for_route');
  assert.match(report.rollback, /Refused/);
});

test('a pre-send retry is attempted once, then reported instead of waiting for future backoff', { timeout: 5000 }, async t => {
  const { journal, config } = fixture(t);
  const event = notice('config/offline');
  journal.register(repository, event.branch, sha, firstThread); journal.intake(event);
  let probes = 0;
  const report = await runDraining({ ...config, retryDelayMs: 1 }, journal, {
    probe: async () => { probes++; throw new Error('offline'); }, send: async () => assert.fail('pre-send failure')
  });
  assert.equal(probes, 1);
  assert.equal(report.complete, false);
  assert.equal(report.pending, 1);
  assert.equal(journal.waiting(event.id).status, 'ready');
  assert.equal(report.remaining[0].waitingFor, 'backoff outside this pass');
});

test('uncertain sends remain held through later drain passes while unrelated work settles', { timeout: 5000 }, async t => {
  const { journal, config } = fixture(t);
  const first = notice('config/ambiguous'), second = notice('config/healthy');
  journal.register(repository, first.branch, sha, firstThread);
  journal.register(repository, second.branch, sha, secondThread);
  journal.intake(first); journal.intake(second);
  const sent = [];
  const report = await runDraining(config, journal, { probe: async () => {}, send: async thread => {
    sent.push(thread);
    if (thread === firstThread) throw new Error('confirmation lost');
  } });
  assert.equal(report.pending, 1);
  assert.equal(report.remaining[0].status, 'uncertain');
  const again = await runDraining(config, journal, { probe: async () => assert.fail('uncertain retry'), send: async () => assert.fail('uncertain retry') });
  assert.equal(again.pending, 1);
  assert.deepEqual(sent, [firstThread, secondThread]);
});

test('local drain refuses an active dispatcher and preserves its pending rows', async t => {
  const { journal, config } = fixture(t);
  const event = notice('config/locked');
  journal.register(repository, event.branch, sha, firstThread); journal.intake(event);
  const lock = acquireDispatcherLock(journal.path);
  try {
    await assert.rejects(runDraining(config, journal, { probe: async () => assert.fail('locked'), send: async () => assert.fail('locked') }), /Another dispatcher/);
  } finally { lock.release(); }
  assert.equal(journal.waiting(event.id).status, 'ready');
});

test('an ambiguous historical target blocks draining without guessing a destination', async t => {
  const { journal, config } = fixture(t);
  const event = notice('config/unknown-history');
  journal.register(repository, event.branch, sha, firstThread); journal.intake(event);
  journal.mark(event.id, 'uncertain');
  journal.db.prepare('UPDATE delivery_details SET destination=NULL,target_unknown=1 WHERE id=?').run(event.id);
  await assert.rejects(runDraining(config, journal, { probe: async () => assert.fail('unknown historical target'), send: async () => assert.fail('unknown historical target') }), /Migration blocked/);
  assert.equal(journal.waiting(event.id).destination, undefined);
  assert.equal(journal.waiting(event.id).status, 'uncertain');
  const lock = acquireDispatcherLock(journal.path);
  lock.release();
});
