import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdtempSync, realpathSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { githubRepository, inspectWorktree, parseOptions, verifyWaitingPush } from '../bridge/worktree.mjs';
import { runRegister } from '../bridge/register.mjs';
import { runManage } from '../bridge/manage.mjs';

const repository = 'wsekta/orm-cxx';
const thread = '11111111-1111-1111-1111-111111111111';
const sha = 'a'.repeat(40);

function gitFixture(t) {
  const folder = mkdtempSync(join(tmpdir(), 'webhook-cli-'));
  const git = (...args) => execFileSync('git', ['-C', folder, ...args], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }).trim();
  git('init');
  git('config', 'user.name', 'Webhook test');
  git('config', 'user.email', 'webhook-test@example.invalid');
  git('commit', '--allow-empty', '-m', 'fixture');
  git('switch', '-c', 'config/first-feature');
  git('remote', 'add', 'origin', 'git@github.com:wsekta/orm-cxx.git');
  t.after(() => rmSync(folder, { recursive: true, force: true }));
  return { folder, git, head: git('rev-parse', 'HEAD') };
}

test('pre-push registration identifies an explicit real worktree and rejects stale or detached HEAD', t => {
  const { folder, git, head } = gitFixture(t);
  assert.deepEqual(inspectWorktree(folder, { repository, branch: 'config/first-feature', sha: head }),
    { repository, branch: 'config/first-feature', sha: head, worktree: realpathSync(folder) });
  assert.throws(() => inspectWorktree(folder, { repository, branch: 'config/second-feature', sha: head }), /branch/);
  assert.throws(() => inspectWorktree(folder, { repository, branch: 'config/first-feature', sha }), /HEAD/);
  assert.throws(() => inspectWorktree(folder, { repository: 'other/repository', branch: 'config/first-feature', sha: head }), /Origin/);
  git('checkout', '--detach');
  assert.throws(() => inspectWorktree(folder, { repository, branch: 'config/first-feature', sha: head }), /Detached HEAD/);
});

test('origin identity accepts supported GitHub remotes without accepting credentials or lookalike hosts', () => {
  for (const remote of ['git@github.com:wsekta/orm-cxx.git', 'https://github.com/wsekta/orm-cxx.git', 'ssh://git@github.com/wsekta/orm-cxx']) {
    assert.equal(githubRepository(remote), repository);
  }
  for (const remote of ['https://github.com.evil.invalid/wsekta/orm-cxx', 'https://token@github.com/wsekta/orm-cxx', 'ssh://other@github.com/wsekta/orm-cxx', 'https://github.com/wsekta/orm-cxx?x=1']) {
    assert.throws(() => githubRepository(remote), /Origin/);
  }
});

test('duplicate or missing arguments are rejected before any registration mutation', () => {
  assert.throws(() => parseOptions(['--sha', sha, '--sha', sha], ['sha']), /duplicate/);
  assert.throws(() => parseOptions(['--thread'], ['thread']), /Invalid/);
  assert.throws(() => parseOptions([], ['thread'], ['thread']), /Missing --thread/);
  let opened = false;
  assert.throws(() => runRegister(['--config', 'private.json', '--worktree', '/work', '--branch', 'feature', '--sha', sha, '--thread', thread], {
    configLoader: () => ({ repository, journalPath: 'private.sqlite' }),
    inspect: () => { throw new Error('Wrong branch'); },
    openJournal: () => { opened = true; }
  }), /Wrong branch/);
  assert.equal(opened, false);
});

test('registration reports exact stored route without claiming Desktop proof', () => {
  let closed = false;
  const record = { repository, branch: 'feature', sha, thread, worktree: '/work' };
  const result = runRegister(['--config', 'private.json', '--worktree', '/work', '--branch', 'feature', '--sha', sha, '--thread', thread], {
    configLoader: () => ({ repository, journalPath: 'private.sqlite' }),
    inspect: () => record,
    openJournal: () => ({ register: (...args) => {
      assert.deepEqual(args, [repository, 'feature', sha, thread, '/work']);
      return record;
    }, close: () => { closed = true; } })
  });
  assert.deepEqual(result, { registered: true, registration: record, desktopDeliveryVerified: false });
  assert.equal(closed, true);
});

test('late adoption verifies exact historical push evidence without requiring current worktree HEAD', () => {
  const hash = createHash('sha256').update('main').digest('hex');
  const notice = { id: `${sha}:branch:${hash}:failure:123:2`, repository, branch: 'main', sha, outcome: 'failure', url: 'https://github.com/wsekta/orm-cxx/actions/runs/123', workflow: 'Quality', runs: { Quality: { id: 123, attempt: 2 } } };
  const run = { id: 123, run_attempt: 2, name: 'Quality', event: 'push', head_branch: 'main', head_sha: sha, repository: { full_name: repository } };
  const execute = (file, args) => {
    assert.equal(file, 'gh');
    assert.equal(args[0], 'api');
    assert.equal(args[1], 'repos/wsekta/orm-cxx/actions/runs/123/attempts/2');
    return JSON.stringify(run);
  };
  const evidence = verifyWaitingPush(notice, execute);
  assert.deepEqual(JSON.parse(evidence).runs, [{ id: 123, attempt: 2, workflow: 'Quality' }]);
  for (const changed of [{ ...run, event: 'pull_request' }, { ...run, head_branch: 'other' }, { ...run, head_sha: 'b'.repeat(40) }, { ...run, id: 124 }, { ...run, run_attempt: 3 }]) {
    assert.throws(() => verifyWaitingPush(notice, () => JSON.stringify(changed)), /No matching GitHub push run attempt/);
  }
  let adopted = false;
  const result = runManage(['adopt', '--config', 'private.json', '--event', notice.id, '--thread', thread], {
    configLoader: () => ({ repository, journalPath: 'private.sqlite' }),
    verifyPush: value => { assert.equal(value, notice); return evidence; },
    openJournal: () => ({
      waiting: () => ({ notice, status: 'waiting_for_route' }),
      adopt: (id, destination, proof) => { adopted = true; assert.equal(id, notice.id); assert.equal(destination, thread); assert.equal(proof, evidence); return { adopted: true }; },
      close() {}
    })
  });
  assert.equal(adopted, true);
  assert.deepEqual(result, { adopted: true });
});

test('aggregate adoption checks every fingerprint run and exact attempt', () => {
  const hash = createHash('sha256').update('feature').digest('hex');
  const notice = { id: `${sha}:branch:${hash}:success:123:1-124:2`, repository, branch: 'feature', sha, outcome: 'success',
    url: 'https://github.com/wsekta/orm-cxx/actions', runs: { GCC: { id: 123, attempt: 1 }, Quality: { id: 124, attempt: 2 } } };
  const calls = [];
  const execute = (_file, args) => {
    calls.push(args[1]);
    const id = args[1].includes('/123/') ? 123 : 124;
    return JSON.stringify({ id, run_attempt: id === 123 ? 1 : 2, name: id === 123 ? 'GCC' : 'Quality', event: 'push', head_branch: 'feature', head_sha: sha, repository: { full_name: repository } });
  };
  assert.equal(JSON.parse(verifyWaitingPush(notice, execute)).runs.length, 2);
  assert.deepEqual(calls, ['repos/wsekta/orm-cxx/actions/runs/123/attempts/1', 'repos/wsekta/orm-cxx/actions/runs/124/attempts/2']);
  assert.throws(() => verifyWaitingPush({ ...notice, id: `${sha}:branch:${hash}:success:123:1-124:3` }, execute), /state disagrees/);
});

test('failed late-adoption evidence leaves the route unchanged and closes the journal', () => {
  let adopted = false, closed = false;
  assert.throws(() => runManage(['adopt', '--config', 'private.json', '--event', 'id', '--thread', thread], {
    configLoader: () => ({ repository, journalPath: 'private.sqlite' }),
    verifyPush: () => { throw new Error('No exact push'); },
    openJournal: () => ({
      waiting: () => ({ notice: { repository }, status: 'waiting_for_route' }),
      adopt: () => { adopted = true; },
      close: () => { closed = true; }
    })
  }), /No exact push/);
  assert.equal(adopted, false);
  assert.equal(closed, true);
});

test('reconciliation refuses an active dispatcher without changing the delivery', () => {
  let reconciled = false, closed = false;
  assert.throws(() => runManage(['reconcile', '--config', 'private.json', '--event', 'id', '--status', 'received', '--evidence', 'Confirmed absent'], {
    configLoader: () => ({ repository, journalPath: 'private.sqlite' }),
    openJournal: () => ({ path: '/private/journal.sqlite', reconcile: () => { reconciled = true; }, close: () => { closed = true; } }),
    lockFactory: path => { assert.equal(path, '/private/journal.sqlite'); throw new Error('Another dispatcher owns this delivery journal'); }
  }), /Another dispatcher/);
  assert.equal(reconciled, false);
  assert.equal(closed, true);
});

test('reconciliation closes the journal before releasing its exclusive recovery lock', () => {
  const operations = [];
  const result = runManage(['reconcile', '--config', 'private.json', '--event', 'id', '--status', 'accepted', '--evidence', 'Confirmed arrived'], {
    configLoader: () => ({ repository, journalPath: 'private.sqlite' }),
    openJournal: () => ({ path: '/private/journal.sqlite', reconcile: () => { operations.push('reconcile'); return { accepted: true }; }, close: () => operations.push('close') }),
    lockFactory: () => { operations.push('lock'); return { release: () => operations.push('release') }; }
  });
  assert.deepEqual(result, { accepted: true });
  assert.deepEqual(operations, ['lock', 'reconcile', 'close', 'release']);
});
