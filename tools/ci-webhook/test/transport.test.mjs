import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHmac, randomUUID } from 'node:crypto';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createTestHarness } from 'wrangler';
import { Journal } from '../bridge/core.mjs';
import { connectBridge } from '../bridge/main.mjs';

const repository = 'wsekta/orm-cxx', sha = 'd'.repeat(40);
const mainThread = '22222222-2222-2222-2222-222222222222';
const featureThread = '33333333-3333-3333-3333-333333333333';

async function fixture(t) {
  const secret = 'transport-test-webhook-secret', token = 'transport-test-bridge-token';
  const folder = mkdtempSync(join(tmpdir(), 'ci-webhook-transport-'));
  const tokenFile = join(folder, 'token.txt');
  writeFileSync(tokenFile, token);
  const policy = { branches: '*', excludedBranches: ['gh-pages'], workflows: ['GCC'], workflowsByBranch: { main: ['GCC', 'Docs'] } };
  const server = createTestHarness({ workers: [{ configPath: './wrangler.jsonc', secrets: { GITHUB_WEBHOOK_SECRET: secret, BRIDGE_TOKEN: token }, vars: { REPOSITORY_CONFIG: JSON.stringify({ [repository]: policy }) } }] });
  const { url } = await server.listen();
  const journal = new Journal(join(folder, 'journal.sqlite'));
  const config = { repository, endpoint: url.href, tokenFile, journalPath: journal.path,
    defaultThread: '11111111-1111-1111-1111-111111111111', reconcileIntervalMs: 10, retryDelayMs: 10 };
  const events = [], waiters = new Set();
  let bridge;
  t.after(async () => {
    for (const waiter of waiters) { clearTimeout(waiter.timer); waiter.reject(new Error('Transport fixture closed')); }
    await bridge?.stop();
    await server.close();
    journal.close();
    rmSync(folder, { recursive: true });
  });
  function log(line) {
    const event = JSON.parse(line);
    events.push(event);
    for (const waiter of [...waiters]) {
      if (events.filter(waiter.predicate).length >= waiter.count) {
        clearTimeout(waiter.timer);
        waiters.delete(waiter);
        waiter.resolve();
      }
    }
  }
  function waitFor(predicate, count = 1) {
    if (events.filter(predicate).length >= count) return Promise.resolve();
    return new Promise((resolve, reject) => {
      const waiter = { predicate, count, resolve, reject };
      waiter.timer = setTimeout(() => { waiters.delete(waiter); reject(new Error(`Missing transport event; received ${JSON.stringify(events)}`)); }, 12000);
      waiters.add(waiter);
    });
  }
  async function completed(branch, workflow, id, conclusion, attempt = 1, commit = sha) {
    const payload = JSON.stringify({ repository: { full_name: repository }, workflow_run: { id, run_attempt: attempt, name: workflow, event: 'push', status: 'completed', conclusion, head_branch: branch, head_sha: commit, html_url: `https://github.com/${repository}/actions/runs/${id}` } });
    const response = await server.fetch('/webhooks/github', { method: 'POST', body: payload, headers: { 'X-GitHub-Event': 'workflow_run', 'X-GitHub-Delivery': randomUUID(), 'X-Hub-Signature-256': 'sha256=' + createHmac('sha256', secret).update(payload).digest('hex') } });
    assert.equal(response.status, 200);
  }
  return { journal, config, events, waitFor, completed,
    connect(sender) { bridge = connectBridge(config, journal, sender, log); return bridge; },
    async stop() { await bridge?.stop(); bridge = undefined; },
    pending: async () => (await server.fetch(`/pending?repo=${repository}`, { headers: { Authorization: `Bearer ${token}` } })).json() };
}

function facts(message) { return JSON.parse(message.slice(message.indexOf('{"eventId":'))); }
const kind = name => event => event.event === name;

test('real Worker routes the same SHA on two branches independently with offline replay and acknowledgment', { timeout: 30000 }, async t => {
  const state = await fixture(t), { journal } = state;
  const feature = 'codex/transport-feature';
  journal.register(repository, 'main', sha, mainThread);
  journal.register(repository, feature, sha, featureThread);
  const messages = [];
  const sender = { probe: async () => {}, send: async (thread, message) => { messages.push({ thread, facts: facts(message) }); } };
  await state.completed('main', 'GCC', 11, 'success');
  assert.deepEqual(await state.pending(), [], 'Main still requires Docs');
  await state.completed(feature, 'GCC', 12, 'failure');
  await state.completed('main', 'Docs', 13, 'success');
  assert.equal((await state.pending()).length, 2);
  state.connect(sender);
  await state.waitFor(kind('connected'));
  assert.throws(() => connectBridge(state.config, journal, sender, () => {}), /Another dispatcher/,
    'A second dispatcher must refuse before replacing the active repository socket');
  await Promise.all([state.waitFor(kind('acknowledged'), 2), state.waitFor(kind('delivered'), 2)]);
  assert.equal(state.events.some(event => event.event === 'disconnected' && event.code === 4001), false,
    'The rejected second instance must leave the first production connection intact');
  assert.equal(messages.length, 2);
  const featureMessage = messages.find(value => value.thread === featureThread);
  const mainMessage = messages.find(value => value.thread === mainThread);
  assert.equal(featureMessage.facts.outcome, 'failure');
  assert.equal(featureMessage.facts.branch, feature);
  assert.equal(mainMessage.facts.outcome, 'success');
  assert.equal(mainMessage.facts.branch, 'main');
  assert.equal(featureMessage.facts.sha, mainMessage.facts.sha);
  assert.notEqual(featureMessage.facts.eventId, mainMessage.facts.eventId);
  assert.deepEqual(await state.pending(), []);
  await state.stop();
  await state.completed(feature, 'GCC', 12, 'success', 2);
  assert.equal((await state.pending()).length, 1);
  state.connect(sender);
  await Promise.all([state.waitFor(kind('acknowledged'), 3), state.waitFor(kind('delivered'), 3)]);
  assert.equal(messages.length, 3);
  assert.equal(messages[2].thread, featureThread);
  assert.equal(messages[2].facts.outcome, 'success');
  assert.deepEqual(await state.pending(), []);
  await state.completed('gh-pages', 'GCC', 14, 'failure');
  assert.deepEqual(await state.pending(), [], 'The publication branch must not create CI notices');
});

test('real transport ACKs an unregistered A into the local inbox while delivering B independently', { timeout: 30000 }, async t => {
  const state = await fixture(t), { journal } = state;
  const firstBranch = 'config/unregistered-first', secondBranch = 'config/registered-second';
  journal.register(repository, secondBranch, sha, featureThread);
  await state.completed(firstBranch, 'GCC', 21, 'failure');
  await state.completed(secondBranch, 'GCC', 22, 'success');
  const messages = [];
  state.connect({ probe: async () => {}, send: async (thread, message) => messages.push({ thread, facts: facts(message) }) });
  await Promise.all([state.waitFor(kind('acknowledged'), 2), state.waitFor(kind('delivered'))]);
  assert.deepEqual(await state.pending(), [], 'The Worker releases durably received notices even when Desktop has no recipient');
  assert.deepEqual(messages.map(value => value.thread), [featureThread]);
  assert.equal(messages[0].facts.branch, secondBranch);
  const firstReceipt = state.events.find(event => event.event === 'locally-received' && event.status === 'waiting_for_route');
  assert.ok(firstReceipt);
  assert.equal(journal.waiting(firstReceipt.id).status, 'waiting_for_route');
  journal.register(repository, firstBranch, sha, mainThread);
  await state.waitFor(kind('delivered'), 2);
  assert.equal(messages.length, 2);
  assert.equal(messages[1].thread, mainThread);
  assert.equal(messages[1].facts.branch, firstBranch);
  assert.equal(journal.waiting(firstReceipt.id).status, 'accepted');
});

test('an uncertain Desktop command for A does not stop real transport intake or delivery for B', { timeout: 30000 }, async t => {
  const state = await fixture(t), { journal } = state;
  const firstBranch = 'config/uncertain-first', secondBranch = 'config/healthy-second';
  journal.register(repository, firstBranch, sha, mainThread);
  journal.register(repository, secondBranch, sha, featureThread);
  await state.completed(firstBranch, 'GCC', 31, 'failure');
  await state.completed(secondBranch, 'GCC', 32, 'success');
  const attempts = [];
  state.connect({ probe: async () => {}, send: async (thread, message) => {
    attempts.push({ thread, facts: facts(message) });
    if (thread === mainThread) throw new Error('Desktop did not confirm this conversation');
  } });
  await Promise.all([state.waitFor(kind('acknowledged'), 2), state.waitFor(kind('delivery-uncertain')), state.waitFor(kind('delivered'))]);
  const uncertain = state.events.find(kind('delivery-uncertain'));
  assert.equal(journal.waiting(uncertain.id).status, 'uncertain');
  assert.deepEqual(await state.pending(), []);
  assert.equal(attempts.filter(value => value.thread === mainThread).length, 1);
  assert.equal(attempts.filter(value => value.thread === featureThread).length, 1);
  await state.stop();
  const unexpected = [];
  state.connect({ probe: async () => {}, send: async (thread, message) => unexpected.push({ thread, message }) });
  await state.waitFor(kind('connected'), 2);
  await state.stop();
  assert.deepEqual(unexpected, [], 'Neither accepted nor uncertain events can resend on restart');
  assert.equal(journal.waiting(uncertain.id).status, 'uncertain');
});

test('lost upstream ACK replays an already accepted local event without a second Desktop message', { timeout: 30000 }, async t => {
  const state = await fixture(t), { journal } = state;
  const branch = 'config/lost-ack';
  journal.register(repository, branch, sha, featureThread);
  await state.completed(branch, 'GCC', 41, 'success');
  const [notice] = await state.pending();
  assert.ok(notice);
  journal.intake(notice);
  assert.equal(journal.claim(notice.id, featureThread), true);
  journal.mark(notice.id, 'accepted');
  let duplicateMessages = 0;
  state.connect({ probe: async () => {}, send: async () => { duplicateMessages++; } });
  await state.waitFor(kind('acknowledged'));
  await state.stop();
  assert.deepEqual(await state.pending(), []);
  assert.equal(journal.waiting(notice.id).status, 'accepted');
  assert.equal(duplicateMessages, 0);
  assert.equal(state.events.filter(kind('delivered')).length, 0);
});
