import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Journal } from '../bridge/core.mjs';
import { createDispatcher } from '../bridge/dispatcher.mjs';

const repository = 'wsekta/orm-cxx';
const a = '11111111-1111-1111-1111-111111111111';
const b = '22222222-2222-2222-2222-222222222222';
const sha = 'a'.repeat(40);
const deferred = () => {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
};
function notice(branch, sequence = 1) {
  return { id: `${sha}:branch:${createHash('sha256').update(branch).digest('hex')}:failure:${sequence}:1`, repository, branch, sha,
    outcome: 'failure', url: `https://github.com/${repository}/actions/runs/${sequence}` };
}
function fixture(t) {
  const folder = mkdtempSync(join(tmpdir(), 'ci-dispatcher-'));
  const journal = new Journal(join(folder, 'journal.sqlite'));
  let dispatcher;
  t.after(async () => { await dispatcher?.stop(); journal.close(); rmSync(folder, { recursive: true }); });
  return {
    journal,
    start(sender, log, options = {}) {
      dispatcher = createDispatcher(journal, sender, { reconcileIntervalMs: 60000, log, ...options });
      return dispatcher;
    }
  };
}

test('an unregistered event does not block another conversation and wakes after registration', { timeout: 5000 }, async t => {
  const { journal, start } = fixture(t);
  const first = notice('config/first'), second = notice('config/second');
  journal.register(repository, second.branch, sha, b);
  journal.intake(first); journal.intake(second);
  const reachedB = deferred(), reachedA = deferred(), sent = [];
  const dispatcher = start({ probe: async () => {}, send: async thread => { sent.push(thread); } }, line => {
    const event = JSON.parse(line);
    if (event.event === 'delivered') (event.thread === a ? reachedA : reachedB).resolve();
  });
  await reachedB.promise;
  assert.deepEqual(sent, [b]);
  assert.equal(journal.waiting(first.id).status, 'waiting_for_route');
  journal.register(repository, first.branch, sha, a);
  dispatcher.wake();
  await reachedA.promise;
  assert.deepEqual(sent, [b, a]);
});

test('pre-send failure pauses only its ordered queue and preserves another recipient', { timeout: 5000 }, async t => {
  const { journal, start } = fixture(t);
  const first = notice('config/first'), later = notice('config/first', 2), second = notice('config/second');
  journal.register(repository, first.branch, sha, a); journal.register(repository, second.branch, sha, b);
  for (const event of [first, later, second]) journal.intake(event);
  const reachedB = deferred(), deferredA = deferred(), sent = [];
  start({ probe: async thread => { if (thread === a) throw new Error('recipient offline'); }, send: async thread => sent.push(thread) }, line => {
    const event = JSON.parse(line);
    if (event.event === 'delivered' && event.thread === b) reachedB.resolve();
    if (event.event === 'delivery-deferred' && event.thread === a) deferredA.resolve();
  }, { retryDelayMs: 60000 });
  await Promise.all([reachedB.promise, deferredA.promise]);
  assert.deepEqual(sent, [b]);
  assert.equal(journal.waiting(first.id).status, 'ready');
  assert.equal(journal.waiting(later.id).status, 'ready');
  assert.deepEqual(journal.candidates().map(event => event.thread), []);
});

test('uncertain delivery blocks its own FIFO without stopping unrelated intake or dispatch', { timeout: 5000 }, async t => {
  const { journal, start } = fixture(t);
  const first = notice('config/first'), later = notice('config/first', 2), second = notice('config/second');
  journal.register(repository, first.branch, sha, a); journal.register(repository, second.branch, sha, b);
  journal.intake(first); journal.intake(later);
  const uncertain = deferred(), reachedB = deferred(), sent = [];
  const dispatcher = start({ probe: async () => {}, send: async thread => { sent.push(thread); if (thread === a) throw new Error('queue confirmation lost'); } }, line => {
    const event = JSON.parse(line);
    if (event.event === 'delivery-uncertain') uncertain.resolve();
    if (event.event === 'delivered' && event.thread === b) reachedB.resolve();
  });
  await uncertain.promise;
  journal.intake(second);
  dispatcher.wake();
  await reachedB.promise;
  assert.deepEqual(sent, [a, b]);
  assert.equal(journal.waiting(first.id).status, 'uncertain');
  assert.equal(journal.waiting(later.id).status, 'ready');
});

test('same-conversation branches share FIFO while another conversation dispatches concurrently', { timeout: 5000 }, async t => {
  const { journal, start } = fixture(t);
  const first = notice('config/first'), later = notice('config/shared-thread', 2), second = notice('config/second');
  journal.register(repository, first.branch, sha, a); journal.register(repository, later.branch, sha, a); journal.register(repository, second.branch, sha, b);
  for (const event of [first, later, second]) journal.intake(event);
  const startedA = deferred(), releaseA = deferred(), reachedB = deferred(), reachedLater = deferred(), started = [];
  start({ probe: async () => {}, send: async (thread, text) => {
    const facts = JSON.parse(text.slice(text.indexOf('{"eventId":')));
    started.push(facts.eventId);
    if (facts.eventId === first.id) { startedA.resolve(); await releaseA.promise; }
  } }, line => {
    const event = JSON.parse(line);
    if (event.event === 'delivered' && event.id === second.id) reachedB.resolve();
    if (event.event === 'delivered' && event.id === later.id) reachedLater.resolve();
  });
  await Promise.all([startedA.promise, reachedB.promise]);
  assert.deepEqual(started, [first.id, second.id]);
  releaseA.resolve();
  await reachedLater.promise;
  assert.deepEqual(started, [first.id, second.id, later.id]);
});

test('shutdown waits for a claimed command and restart never resends its accepted record', { timeout: 5000 }, async t => {
  const { journal, start } = fixture(t);
  const event = notice('config/first');
  journal.register(repository, event.branch, sha, a); journal.intake(event);
  const started = deferred(), release = deferred();
  let sent = 0;
  const dispatcher = start({ probe: async () => {}, send: async () => { sent++; started.resolve(); await release.promise; } }, () => {});
  await started.promise;
  let stopped = false;
  const stopping = dispatcher.stop().then(() => { stopped = true; });
  await Promise.resolve();
  assert.equal(stopped, false);
  release.resolve();
  await stopping;
  assert.equal(journal.waiting(event.id).status, 'accepted');
  const replacement = createDispatcher(journal, { probe: async () => assert.fail('accepted event'), send: async () => sent++ }, { log: () => {} });
  await replacement.stop();
  assert.equal(sent, 1);
});
