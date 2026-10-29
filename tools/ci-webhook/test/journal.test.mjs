import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { DatabaseSync } from 'node:sqlite';
import { Journal, deliverNotice } from '../bridge/core.mjs';

const repository = 'wsekta/orm-cxx';
const firstThread = '11111111-1111-1111-1111-111111111111';
const secondThread = '22222222-2222-2222-2222-222222222222';
const thirdThread = '33333333-3333-3333-3333-333333333333';
const sha = 'a'.repeat(40), nextSha = 'b'.repeat(40);
const firstBranch = 'config/first-feature', secondBranch = 'config/second-feature';

function notice(branch = firstBranch, commit = sha, run = 1) {
  return { id: `${commit}:branch:${createHash('sha256').update(branch).digest('hex')}:failure:${run}:1`,
    repository, branch, sha: commit, outcome: 'failure', workflow: 'Packages', job: 'conan',
    url: `https://github.com/${repository}/actions/runs/${run}` };
}

function fixture(t) {
  const folder = mkdtempSync(join(tmpdir(), 'ci-journal-'));
  const path = join(folder, 'journal.sqlite');
  let journal = new Journal(path);
  t.after(() => { journal?.close(); rmSync(folder, { recursive: true }); });
  return { folder, path, get journal() { return journal; },
    reopen() { journal.close(); journal = new Journal(path); return journal; } };
}

test('registration is idempotent for its owner and conflicts leave both route and branch ownership intact', t => {
  const { journal } = fixture(t);
  journal.register(repository, firstBranch, sha, firstThread, '/features/first');
  assert.doesNotThrow(() => journal.register(repository, firstBranch, sha, firstThread, '/features/first'));
  assert.throws(() => journal.register(repository, firstBranch, sha, secondThread), /owner|conflict/i);
  assert.throws(() => journal.register(repository, firstBranch, nextSha, secondThread), /owner|conflict/i);
  assert.equal(journal.thread(notice()), firstThread);
  assert.equal(journal.thread(notice(firstBranch, nextSha)), undefined);
});

test('obsolete registration writers cannot replace or insert routes after migration while the current API still works', t => {
  const { journal, path } = fixture(t);
  journal.register(repository, firstBranch, sha, firstThread);
  const obsolete = new DatabaseSync(path);
  try {
    const registerOld = obsolete.prepare('INSERT OR REPLACE INTO routes_by_branch VALUES (?, ?, ?, ?)');
    assert.throws(() => registerOld.run(repository, firstBranch, sha, secondThread), /obsolete|current installed registration/i);
    assert.throws(() => registerOld.run(repository, firstBranch, nextSha, secondThread), /obsolete|current installed registration/i);
    assert.throws(() => obsolete.prepare('UPDATE routes_by_branch SET thread=? WHERE repository=? AND branch=? AND sha=?')
      .run(secondThread, repository, firstBranch, sha), /obsolete|current installed registration/i);
    assert.equal(journal.thread(notice()), firstThread, 'A rejected replacement must preserve the original exact route');
    assert.equal(journal.thread(notice(firstBranch, nextSha)), undefined, 'An obsolete writer must not create unowned new routes');
    journal.register(repository, firstBranch, nextSha, firstThread);
    assert.equal(journal.thread(notice(firstBranch, nextSha)), firstThread);
    assert.throws(() => registerOld.run(repository, firstBranch, nextSha, secondThread), /obsolete|current installed registration/i,
      'The current writer must reset the guard before committing its transaction');
  } finally { obsolete.close(); }
});

test('a deliberate transfer affects future commits while old exact routes retain their destination', t => {
  const { journal } = fixture(t);
  journal.register(repository, firstBranch, sha, firstThread);
  assert.throws(() => journal.transfer(repository, firstBranch, thirdThread, secondThread), /owner|conflict|expected/i);
  journal.transfer(repository, firstBranch, firstThread, secondThread);
  journal.register(repository, firstBranch, nextSha, secondThread);
  assert.equal(journal.thread(notice()), firstThread);
  assert.equal(journal.thread(notice(firstBranch, nextSha)), secondThread);
  assert.throws(() => journal.register(repository, firstBranch, sha, secondThread), /owner|conflict|route/i);
});

test('closing a branch preserves all registered historical routes and pending history', t => {
  const { journal } = fixture(t);
  const value = notice();
  journal.register(repository, firstBranch, sha, firstThread);
  journal.intake(value);
  assert.throws(() => journal.closeBranch(repository, firstBranch, secondThread), /owner|conflict|expected/i);
  journal.closeBranch(repository, firstBranch, firstThread);
  assert.equal(journal.thread(value), firstThread);
  assert.equal(journal.waiting(value.id).notice.id, value.id);
  assert.throws(() => journal.register(repository, firstBranch, nextSha, firstThread), /closed|owner|conflict/i);
});

test('durable receipt survives restart before Desktop dispatch and rejects altered duplicate payloads', t => {
  const state = fixture(t);
  const value = notice();
  state.journal.register(repository, firstBranch, sha, firstThread);
  assert.equal(state.journal.intake(value).status, 'ready');
  assert.equal(state.journal.intake(value).id, value.id);
  assert.throws(() => state.journal.intake({ ...value, job: 'different-job' }), /payload|identity|conflict|mismatch/i);
  const reopened = state.reopen();
  assert.deepEqual(reopened.candidates().map(row => row.id), [value.id]);
  assert.deepEqual(reopened.waiting(value.id).notice, value);
});

test('a missing route A does not block B and registration wakes the exact waiting event', t => {
  const { journal } = fixture(t);
  const first = notice(), second = notice(secondBranch);
  assert.equal(journal.intake(first).status, 'waiting_for_route');
  journal.register(repository, secondBranch, sha, secondThread);
  journal.intake(second);
  assert.deepEqual(journal.candidates().map(row => row.id), [second.id]);
  journal.register(repository, firstBranch, sha, firstThread);
  const candidates = journal.candidates();
  assert.deepEqual(candidates.map(row => row.id), [first.id, second.id]);
  assert.deepEqual(candidates.map(row => row.thread), [firstThread, secondThread]);
  assert.equal(journal.thread({ ...first, sha: nextSha }, thirdThread), undefined, 'A branch owner is not an exact-SHA route');
});

test('FIFO spans branches sharing one conversation while another conversation remains independent', t => {
  const { journal } = fixture(t);
  const first = notice(), later = notice(secondBranch, nextSha), independent = notice('config/third-feature', sha, 3);
  journal.register(repository, first.branch, first.sha, firstThread);
  journal.register(repository, later.branch, later.sha, firstThread);
  journal.register(repository, independent.branch, independent.sha, secondThread);
  for (const value of [first, later, independent]) journal.intake(value);
  assert.deepEqual(journal.candidates().map(row => row.id), [first.id, independent.id]);
  assert.equal(journal.claim(first.id, firstThread), true);
  assert.deepEqual(journal.candidates().map(row => row.id), [independent.id]);
  journal.mark(first.id, 'accepted');
  assert.deepEqual(journal.candidates().map(row => row.id), [later.id, independent.id]);
});

test('a pre-send retry delay blocks later A entries but does not block B', t => {
  const { journal } = fixture(t);
  const first = notice(), later = notice(firstBranch, nextSha), independent = notice(secondBranch, sha, 3);
  journal.register(repository, firstBranch, sha, firstThread);
  journal.register(repository, firstBranch, nextSha, firstThread);
  journal.register(repository, secondBranch, sha, secondThread);
  for (const value of [first, later, independent]) journal.intake(value);
  journal.retry(first.id, 'pre-send recipient unavailable', 60_000);
  assert.deepEqual(journal.candidates().map(row => row.id), [independent.id]);
  assert.deepEqual(journal.candidates(Date.now() + 120_000).map(row => row.id), [first.id, independent.id]);
});

test('uncertain dispatch blocks only its conversation and never becomes retryable on restart', t => {
  const state = fixture(t), journal = state.journal;
  const first = notice(), later = notice(firstBranch, nextSha), independent = notice(secondBranch, sha, 3);
  journal.register(repository, firstBranch, sha, firstThread);
  journal.register(repository, firstBranch, nextSha, firstThread);
  journal.register(repository, secondBranch, sha, secondThread);
  for (const value of [first, later, independent]) journal.intake(value);
  assert.equal(journal.claim(first.id, firstThread), true);
  journal.mark(first.id, 'uncertain', 'queue confirmation lost');
  const reopened = state.reopen();
  reopened.recoverDispatching();
  assert.equal(reopened.waiting(first.id).status, 'uncertain');
  assert.deepEqual(reopened.candidates().map(row => row.id), [independent.id]);
  assert.equal(reopened.claim(first.id, firstThread), false);
});

test('a crash after claim is reconciled as uncertain with its original pinned target', t => {
  const state = fixture(t), value = notice();
  state.journal.register(repository, firstBranch, sha, firstThread);
  state.journal.intake(value);
  assert.equal(state.journal.claim(value.id, firstThread), true);
  const reopened = state.reopen();
  reopened.recoverDispatching();
  assert.equal(reopened.waiting(value.id).status, 'uncertain');
  assert.equal(reopened.waiting(value.id).destination, firstThread);
  assert.deepEqual(reopened.candidates(), []);
});

test('accepted IDs remain deduplicated after lost ACK and restart', t => {
  const state = fixture(t), value = notice();
  state.journal.register(repository, firstBranch, sha, firstThread);
  state.journal.intake(value);
  assert.equal(state.journal.claim(value.id, firstThread), true);
  state.journal.mark(value.id, 'accepted');
  const reopened = state.reopen();
  assert.equal(reopened.intake(value).status, 'accepted');
  assert.deepEqual(reopened.candidates(), []);
  assert.equal(reopened.claim(value.id, firstThread), false);
});

test('a failed local commit never reports a notice as durably received', t => {
  const { journal } = fixture(t), value = notice();
  journal.db.exec("CREATE TRIGGER reject_receipt BEFORE INSERT ON notices BEGIN SELECT RAISE(ABORT, 'simulated disk-full commit failure'); END;");
  assert.throws(() => journal.intake(value), /simulated disk-full commit failure/);
  assert.equal(journal.waiting(value.id), undefined);
});

test('late adoption registers only the selected waiting event and records its verification evidence', t => {
  const { journal } = fixture(t), value = notice();
  journal.intake(value);
  journal.adopt(value.id, firstThread, 'Verified exact GitHub push run for this repository, branch and historical SHA');
  assert.equal(journal.thread(value), firstThread);
  assert.equal(journal.waiting(value.id).status, 'ready');
  assert.match(journal.waiting(value.id).evidence, /Verified exact GitHub push run/);
  assert.throws(() => journal.adopt(value.id, secondThread, 'Different claimed conversation'), /waiting|unregistered/i);
  assert.equal(journal.thread(value), firstThread);
});

test('uncertain delivery reconciliation requires evidence and cannot change a pinned destination', t => {
  const { journal } = fixture(t), value = notice();
  journal.register(repository, firstBranch, sha, firstThread);
  journal.intake(value);
  journal.claim(value.id, firstThread);
  journal.mark(value.id, 'uncertain', 'confirmation lost');
  assert.throws(() => journal.reconcile(value.id, 'received', ''), /evidence/i);
  assert.throws(() => journal.reconcile(value.id, 'received', 'Inspected the original conversation', secondThread), /destination|pinned/i);
  journal.reconcile(value.id, 'accepted', 'Found the exact eventId in the original conversation', firstThread);
  assert.equal(journal.waiting(value.id).status, 'accepted');
  assert.equal(journal.waiting(value.id).destination, firstThread);
  assert.deepEqual(journal.candidates(), []);
});

function legacyDatabase(path, routes, values = []) {
  const db = new DatabaseSync(path);
  db.exec(`CREATE TABLE notices (id TEXT PRIMARY KEY, repository TEXT NOT NULL, sha TEXT NOT NULL, data TEXT NOT NULL, status TEXT NOT NULL);
    CREATE TABLE routes (repository TEXT NOT NULL, sha TEXT NOT NULL, thread TEXT NOT NULL, PRIMARY KEY (repository, sha));
    CREATE TABLE routes_by_branch (repository TEXT NOT NULL, branch TEXT NOT NULL, sha TEXT NOT NULL, thread TEXT NOT NULL, PRIMARY KEY (repository, branch, sha));`);
  for (const route of routes) db.prepare('INSERT INTO routes_by_branch VALUES (?, ?, ?, ?)').run(repository, ...route);
  for (const [value, status] of values) db.prepare('INSERT INTO notices VALUES (?, ?, ?, ?, ?)').run(value.id, repository, value.sha, JSON.stringify(value), status);
  db.close();
}

test('legacy ambiguous records do not inherit a potentially overwritten current route', t => {
  const folder = mkdtempSync(join(tmpdir(), 'ci-journal-legacy-')), path = join(folder, 'journal.sqlite');
  const value = notice();
  legacyDatabase(path, [[firstBranch, sha, secondThread]], [[value, 'dispatching']]);
  const journal = new Journal(path);
  t.after(() => { journal.close(); rmSync(folder, { recursive: true }); });
  const pending = journal.waiting(value.id);
  assert.equal(pending.destination == null, true, 'Current route is not historical dispatch evidence');
  assert.throws(() => journal.recoverDispatching(), /target|destination|legacy|reconcil/i);
  assert.deepEqual(journal.candidates(), []);
  assert.throws(() => journal.reconcile(value.id, 'accepted', 'Inspected archived conversation'), /destination|historical/i);
  journal.reconcile(value.id, 'accepted', 'Found the exact eventId in the original archived conversation', firstThread);
  assert.equal(journal.waiting(value.id).destination, firstThread, 'Evidence can identify the old destination instead of the overwritten current route');
  assert.equal(journal.thread(value), secondThread, 'Reconciliation does not rewrite route history');
  assert.doesNotThrow(() => journal.recoverDispatching());
});

test('migration preserves conflicting legacy routes without selecting a new branch owner', t => {
  const folder = mkdtempSync(join(tmpdir(), 'ci-journal-owners-')), path = join(folder, 'journal.sqlite');
  legacyDatabase(path, [[firstBranch, sha, firstThread], [firstBranch, nextSha, secondThread]]);
  const journal = new Journal(path);
  t.after(() => { journal.close(); rmSync(folder, { recursive: true }); });
  assert.equal(journal.thread(notice()), firstThread);
  assert.equal(journal.thread(notice(firstBranch, nextSha)), secondThread);
  assert.throws(() => journal.register(repository, firstBranch, 'c'.repeat(40), thirdThread), /conflict|owner/i);
  journal.transfer(repository, firstBranch, [firstThread, secondThread], thirdThread);
  journal.register(repository, firstBranch, 'c'.repeat(40), thirdThread);
  assert.equal(journal.thread(notice(firstBranch, 'c'.repeat(40))), thirdThread);
  assert.equal(journal.thread(notice()), firstThread);
});

test('legacy accepted history keeps its unknown historical destination without blocking cutover or replaying', async t => {
  const folder = mkdtempSync(join(tmpdir(), 'ci-journal-accepted-')), path = join(folder, 'journal.sqlite');
  const value = notice();
  legacyDatabase(path, [[firstBranch, sha, secondThread]], [[value, 'accepted']]);
  const journal = new Journal(path);
  t.after(() => { journal.close(); rmSync(folder, { recursive: true }); });
  const accepted = journal.waiting(value.id);
  assert.equal(accepted.status, 'accepted');
  assert.equal(accepted.destination == null, true, 'An overwritten route must not invent accepted delivery history');
  assert.equal(journal.status().deliveries.find(row => row.id === value.id).targetUnknown, true);
  assert.doesNotThrow(() => journal.recoverDispatching());
  assert.deepEqual(journal.candidates(), []);
  assert.equal(journal.intake(value).status, 'accepted');
  await deliverNotice(value, journal, secondThread, {
    probe: async () => assert.fail('Accepted historical notices are not retried'),
    send: async () => assert.fail('Accepted historical notices are not resent')
  });
});

test('legacy safely received rows become dispatchable but never create a feature fallback', t => {
  const folder = mkdtempSync(join(tmpdir(), 'ci-journal-received-')), path = join(folder, 'journal.sqlite');
  const value = notice();
  legacyDatabase(path, [[firstBranch, sha, firstThread]], [[value, 'received']]);
  const journal = new Journal(path);
  t.after(() => { journal.close(); rmSync(folder, { recursive: true }); });
  journal.recoverDispatching();
  assert.deepEqual(journal.candidates().map(row => row.id), [value.id]);
  assert.equal(journal.thread(notice(secondBranch), secondThread), undefined);
});

function childRegistration(script, path, branch, commit, target) {
  return new Promise((resolve, reject) => {
    const child = spawn(process.execPath, [script, path, branch, commit, target], { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let output = '';
    child.stdout.on('data', chunk => { output += chunk; });
    child.stderr.on('data', chunk => { output += chunk; });
    child.on('error', reject);
    child.on('close', code => resolve({ code, output }));
  });
}

test('concurrent registration processes allow independent branches and never silently steal ownership', { timeout: 15000 }, async t => {
  const folder = mkdtempSync(join(tmpdir(), 'ci-journal-processes-')), path = join(folder, 'journal.sqlite');
  t.after(() => rmSync(folder, { recursive: true }));
  const initial = new Journal(path);
  initial.close();
  const script = join(folder, 'register.mjs');
  writeFileSync(script, `import { Journal } from ${JSON.stringify(new URL('../bridge/core.mjs', import.meta.url).href)};
    const [path, branch, sha, thread] = process.argv.slice(2);
    let journal;
    try { journal = new Journal(path); journal.register(${JSON.stringify(repository)}, branch, sha, thread); }
    catch (error) { console.error(error.message); process.exitCode = 1; }
    finally { journal?.close(); }
  `);
  const independent = await Promise.all([
    childRegistration(script, path, firstBranch, sha, firstThread),
    childRegistration(script, path, secondBranch, sha, secondThread)
  ]);
  assert.deepEqual(independent.map(result => result.code), [0, 0], JSON.stringify(independent));
  const conflictBranch = 'config/same-branch';
  const conflicting = await Promise.all([
    childRegistration(script, path, conflictBranch, nextSha, firstThread),
    childRegistration(script, path, conflictBranch, nextSha, secondThread)
  ]);
  assert.deepEqual(conflicting.map(result => result.code).sort(), [0, 1], JSON.stringify(conflicting));
  assert.match(conflicting.find(result => result.code !== 0).output, /owner|conflict/i);
  const check = new Journal(path);
  try {
    assert.equal(check.thread(notice()), firstThread);
    assert.equal(check.thread(notice(secondBranch)), secondThread);
    assert.ok([firstThread, secondThread].includes(check.thread(notice(conflictBranch, nextSha))));
  } finally { check.close(); }
});
