import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { appendFileSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { tmpdir } from 'node:os';
import { validateLaunchConfig, resolveRuntime, verifyHistory, runPilot } from '../desktop/launch.mjs';
import { inspectThread } from '../desktop/rpc.mjs';

const thread = '11111111-1111-1111-1111-111111111111';
const runtimeFiles = ['codex.exe', 'codex-code-mode-host.exe', 'codex-windows-sandbox-setup.exe', 'codex-command-runner.exe'];
const sha256 = value => createHash('sha256').update(value).digest('hex');

function fixture(t) {
  const folder = mkdtempSync(join(tmpdir(), 'ci-desktop-'));
  t.after(() => rmSync(folder, { recursive: true }));
  return {
    folder,
    config: {
      desktopPath: join(folder, 'desktop', 'ChatGPT.exe'), runtimeRoot: join(folder, 'runtime'),
      codexHome: join(folder, 'home'), sqliteHome: join(folder, 'home'),
      pythonPath: join(folder, 'python.exe'), nodePath: join(folder, 'node.exe'),
      bridgeConfig: join(folder, 'bridge.local.json'), stateDirectory: join(folder, 'pilot'),
      thread, desktopPid: 1001, serverPid: 1002, desktopCreated: '1234567890', serverCreated: '1234567891',
      appServer: 'ws://127.0.0.1:4500/',
      serverArguments: ['-c', 'features.code_mode_host=true', 'app-server', '--analytics-default-enabled']
    }
  };
}

function runtimeFixture(config) {
  const fingerprint = createHash('sha256');
  const contents = runtimeFiles.map(name => [name, Buffer.from(`runtime fixture: ${name}\n`)]);
  for (const [name, content] of contents) {
    fingerprint.update(name); fingerprint.update('\0'); fingerprint.update(sha256(content)); fingerprint.update('\0');
  }
  const runtime = join(config.runtimeRoot, fingerprint.digest('hex').slice(0, 16));
  const resources = join(dirname(config.desktopPath), 'resources');
  mkdirSync(runtime, { recursive: true });
  mkdirSync(resources, { recursive: true });
  for (const [name, content] of contents) {
    writeFileSync(join(runtime, name), content);
    writeFileSync(join(resources, name), content);
  }
  return { runtime, resources };
}

function historyFixture(folder) {
  const path = join(folder, 'conversation.jsonl');
  const content = '{"type":"session_meta"}\n{"type":"event_msg","payload":"original"}\n';
  writeFileSync(path, content);
  return {
    thread,
    historyTables: [{ database: join(folder, 'thread_history_1.sqlite'), table: 'thread_turns', count: 1, sha256: 'persisted-history' }],
    contextTables: [
      { database: join(folder, 'state_5.sqlite'), table: 'thread_attachments', count: 1, sha256: 'original-attachment' },
      { database: join(folder, 'state_5.sqlite'), table: 'thread_dynamic_tools', count: 1, sha256: 'old-tools' }
    ],
    rollouts: [{ path, bytes: Buffer.byteLength(content), sha256: sha256(content) }]
  };
}

test('pilot accepts captured local arguments but refuses competing transport flags', t => {
  const { config } = fixture(t);
  assert.equal(validateLaunchConfig(config), config);
  for (const flag of ['--listen', '--listen=stdio://', '--listen=ws://0.0.0.0:9000', '--stdio']) {
    assert.throws(() => validateLaunchConfig({ ...config, serverArguments: [...config.serverArguments, flag] }), /argument|transport|listen|stdio/i);
  }
});

test('pilot rejects a remote endpoint or ambiguous process identity before restart', t => {
  const { config } = fixture(t);
  assert.throws(() => validateLaunchConfig({ ...config, appServer: 'ws://example.com:4500/' }));
  assert.throws(() => validateLaunchConfig({ ...config, appServer: 'unix://' }));
  assert.throws(() => validateLaunchConfig({ ...config, serverPid: config.desktopPid }));
  assert.throws(() => validateLaunchConfig({ ...config, stateDirectory: 'relative-state' }));
});

test('direct pilot invocation cannot close Desktop without an independent scheduled task', async t => {
  const { config } = fixture(t);
  await assert.rejects(runPilot(config), /independent scheduled task|use --arm/i);
  await assert.rejects(runPilot(config, { scheduledTask: 'unrelated-task' }), /independent scheduled task|use --arm/i);
});

test('physical bridge paths can only replace the two storage locations', t => {
  const { config, folder } = fixture(t);
  const bridgePaths = { tokenFile: join(folder, 'token.txt'), journalPath: join(folder, 'journal.sqlite') };
  assert.doesNotThrow(() => validateLaunchConfig({ ...config, bridgePaths }));
  assert.throws(() => validateLaunchConfig({ ...config, bridgePaths: { ...bridgePaths, defaultThread: thread } }));
  assert.throws(() => validateLaunchConfig({ ...config, bridgePaths: { ...bridgePaths, journalPath: 'relative.sqlite' } }));
});

test('runtime selection verifies all four Desktop binaries rather than accepting an old CLI', async t => {
  const { config } = fixture(t);
  const { runtime, resources } = runtimeFixture(config);
  const stale = join(config.runtimeRoot, 'stale-runtime');
  mkdirSync(stale, { recursive: true });
  writeFileSync(join(stale, 'codex.exe'), 'unrelated old executable');
  assert.deepEqual(await resolveRuntime(config), { codexPath: join(runtime, 'codex.exe'), resources });
  writeFileSync(join(runtime, 'codex-code-mode-host.exe'), 'different code-mode host');
  await assert.rejects(resolveRuntime(config), /mismatch/i);
});

test('runtime selection does not fall back to a stale extraction after Desktop changes', async t => {
  const { config } = fixture(t);
  const { resources } = runtimeFixture(config);
  writeFileSync(join(resources, 'codex-windows-sandbox-setup.exe'), 'new Desktop sandbox runtime');
  await assert.rejects(resolveRuntime(config));
});

test('history verification accepts an appended rollout while preserving its original prefix', async t => {
  const { folder } = fixture(t);
  const before = historyFixture(folder);
  const addition = '{"type":"event_msg","payload":"resume metadata"}\n';
  appendFileSync(before.rollouts[0].path, addition);
  const after = structuredClone(before);
  after.rollouts[0].bytes += Buffer.byteLength(addition);
  after.rollouts[0].sha256 = 'a new full-file hash is expected after append';
  await verifyHistory(before, after);
});

test('history verification rejects truncation and same-length replacement of original content', async t => {
  const { folder } = fixture(t);
  const before = historyFixture(folder);
  const after = structuredClone(before);
  writeFileSync(before.rollouts[0].path, 'truncated');
  after.rollouts[0].bytes = Buffer.byteLength('truncated');
  await assert.rejects(verifyHistory(before, after), /preserv|rollout/i);
  writeFileSync(before.rollouts[0].path, 'x'.repeat(before.rollouts[0].bytes));
  after.rollouts[0].bytes = before.rollouts[0].bytes;
  after.rollouts[0].sha256 = before.rollouts[0].sha256;
  await assert.rejects(verifyHistory(before, after), /preserv|rollout/i);
});

test('history verification rejects changed persisted rows and a different destination thread', async t => {
  const { folder } = fixture(t);
  const before = historyFixture(folder);
  const changed = structuredClone(before);
  changed.historyTables[0].sha256 = 'altered conversation row';
  await assert.rejects(verifyHistory(before, changed), /history|conversation/i);
  await assert.rejects(verifyHistory(before, { ...before, thread: '22222222-2222-2222-2222-222222222222' }), /history|conversation/i);
});

test('history protects attachment references while allowing fresh Desktop tool definitions', async t => {
  const { folder } = fixture(t);
  const before = historyFixture(folder);
  const after = structuredClone(before);
  after.contextTables.find(value => value.table === 'thread_dynamic_tools').sha256 = 'new Desktop tools';
  await verifyHistory(before, after);
  after.contextTables.find(value => value.table === 'thread_attachments').sha256 = 'missing attachment';
  await assert.rejects(verifyHistory(before, after), /attachment|history|context|conversation/i);
});

test('RPC inspection reads every full-history page without resuming or writing the thread', async () => {
  const calls = [];
  const client = {
    request: async (method, params) => {
      calls.push({ method, params });
      if (method === 'thread/read') return { thread: { id: thread, status: { type: 'idle' } } };
      assert.equal(method, 'thread/turns/list', 'Inspection must never issue mutating RPC methods');
      return params.cursor ? { data: [{ id: 'third' }], nextCursor: null }
        : { data: [{ id: 'first' }, { id: 'second' }], nextCursor: 'next-page' };
    }
  };
  assert.deepEqual(await inspectThread(client, thread), { threadId: thread, turns: 3, status: 'idle' });
  assert.deepEqual(calls.map(value => value.method), ['thread/read', 'thread/turns/list', 'thread/turns/list']);
  assert.deepEqual(calls[0].params, { threadId: thread });
  for (const call of calls.slice(1)) {
    assert.equal(call.params.threadId, thread);
    assert.equal(call.params.itemsView, 'full');
    assert.equal(call.params.sortDirection, 'asc');
  }
  assert.equal(calls[2].params.cursor, 'next-page');
});

test('RPC inspection rejects another UUID or a thread Desktop has not resumed into idle', async () => {
  for (const returned of [
    { id: '22222222-2222-2222-2222-222222222222', status: { type: 'idle' } },
    ...['notLoaded', 'active', 'systemError'].map(type => ({ id: thread, status: { type } }))
  ]) {
    await assert.rejects(inspectThread({ request: async method => {
      assert.equal(method, 'thread/read', 'Invalid thread metadata must stop before history pagination');
      return { thread: returned };
    } }, thread), /thread|conversation|idle|resume/i);
  }
});

test('RPC inspection rejects empty history and repeated pagination cursors', async () => {
  const client = page => ({ request: async method => method === 'thread/read'
    ? { thread: { id: thread, status: { type: 'idle' } } } : page() });
  await assert.rejects(inspectThread(client(() => ({ data: [], nextCursor: null })), thread), /empty|history/i);
  let pages = 0;
  await assert.rejects(inspectThread(client(() => {
    pages++;
    return { data: [{ id: 'repeated-turn' }], nextCursor: 'same-cursor' };
  }), thread), /cursor|repeat/i);
  assert.equal(pages, 2);
});
