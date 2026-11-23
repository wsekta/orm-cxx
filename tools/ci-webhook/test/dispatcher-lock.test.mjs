import { test } from 'node:test';
import assert from 'node:assert/strict';
import { DatabaseSync } from 'node:sqlite';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { acquireDispatcherLock } from '../bridge/dispatcher-lock.mjs';

function fixture(t) {
  const folder = mkdtempSync(join(tmpdir(), 'ci-dispatcher-lock-'));
  t.after(() => rmSync(folder, { recursive: true }));
  return join(folder, 'journal.sqlite');
}

test('dispatcher exclusivity does not lock out independent journal registration writers', t => {
  const path = fixture(t);
  const lock = acquireDispatcherLock(path);
  try {
    assert.throws(() => acquireDispatcherLock(path), /Another dispatcher/);
    const registration = new DatabaseSync(path);
    registration.exec('CREATE TABLE registration_test (id INTEGER PRIMARY KEY); INSERT INTO registration_test VALUES (1);');
    registration.close();
  } finally { lock.release(); }
  const replacement = acquireDispatcherLock(path);
  replacement.release();
});

test('a different process is excluded and process death releases the lock without stale-PID guessing', { timeout: 10000 }, async t => {
  const folder = mkdtempSync(join(tmpdir(), 'ci-dispatcher-lock-child-'));
  const path = join(folder, 'journal.sqlite');
  const moduleUrl = new URL('../bridge/dispatcher-lock.mjs', import.meta.url).href;
  const script = `import { acquireDispatcherLock } from ${JSON.stringify(moduleUrl)}; globalThis.dispatcherLock = acquireDispatcherLock(process.argv[1]); process.stdout.write('locked\\n'); process.stdin.resume();`;
  const child = spawn(process.execPath, ['--input-type=module', '-e', script, path], { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true });
  const exited = once(child, 'exit');
  t.after(async () => { if (child.exitCode === null) child.kill(); await exited; rmSync(folder, { recursive: true }); });
  await new Promise((resolveReady, rejectReady) => {
    let output = '', errors = '';
    child.stdout.on('data', data => { output += data; if (output.includes('locked')) resolveReady(); });
    child.stderr.on('data', data => { errors += data; });
    child.once('error', rejectReady);
    child.once('exit', code => { if (!output.includes('locked')) rejectReady(new Error(`Lock child exited ${code}: ${errors}`)); });
  });
  assert.throws(() => acquireDispatcherLock(path), /Another dispatcher/);
  child.kill();
  await exited;
  const replacement = acquireDispatcherLock(path);
  replacement.release();
});
