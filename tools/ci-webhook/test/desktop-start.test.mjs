import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

test('Windows desktop starter records early preparation failures without starting the application', {
  skip: process.platform !== 'win32', timeout: 90000
}, async t => {
  const folder = mkdtempSync(join(tmpdir(), 'desktop starter-'));
  let child, completion;
  t.after(async () => {
    if (child?.exitCode === null) child.kill();
    await completion;
    rmSync(folder, { recursive: true, force: true });
  });
  const profilePath = join(folder, 'start-profile.json');
  const stateRoot = join(folder, 'attempts');
  writeFileSync(profilePath, JSON.stringify({
    schemaVersion: 1, launcherDirectory: join(folder, 'missing-launcher'),
    nodePath: process.execPath, pythonPath: process.execPath,
    bridgeConfig: join(folder, 'unused-config.json'), stateRoot,
    thread: '11111111-1111-1111-1111-111111111111',
    appServer: 'ws://127.0.0.1:49175', ignoreBusyTurns: []
  }));
  const powershell = join(process.env.SystemRoot, 'System32', 'WindowsPowerShell', 'v1.0', 'powershell.exe');
  const starter = fileURLToPath(new URL('../scripts/start-desktop-webhooks.ps1', import.meta.url));
  // A successful Windows runner took 13.5 seconds; allow cold startup without
  // blocking node:test's own deadline or accepting a hung/interactive process.
  completion = new Promise(resolve => {
    child = execFile(powershell, ['-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
      '-File', starter, '-ProfilePath', profilePath, '-NoPause'],
    { encoding: 'utf8', timeout: 60000, windowsHide: true },
    (error, stdout, stderr) => resolve({ error, stdout, stderr }));
    child.stdin?.end();
  });
  const result = await completion;
  try {
    assert.equal(result.error?.code, 1, 'Starter must exit with the expected preparation error');
    assert.equal(result.error.killed, false, 'Starter must finish before its process deadline');
    assert.equal(result.error.signal, null, 'Starter must finish without a termination signal');
    const attempts = readdirSync(stateRoot);
    assert.equal(attempts.length, 1);
    const error = readFileSync(join(stateRoot, attempts[0], 'error.txt'), 'utf8');
    assert.match(error, /Brak wymaganego pliku/);
    assert.ok(readFileSync(join(stateRoot, attempts[0], 'start.log'), 'utf8').length > 0);
    assert.equal(readdirSync(join(stateRoot, attempts[0])).includes('desktop.local.json'), false);
  } catch (failure) {
    t.diagnostic(JSON.stringify({
      code: result.error?.code, signal: result.error?.signal,
      killed: result.error?.killed, message: result.error?.message,
      stdout: result.stdout, stderr: result.stderr
    }, null, 2));
    if (existsSync(stateRoot)) {
      for (const attempt of readdirSync(stateRoot)) {
        for (const name of ['start.log', 'error.txt']) {
          const path = join(stateRoot, attempt, name);
          if (existsSync(path)) t.diagnostic(`${attempt}/${name}:\n${readFileSync(path, 'utf8')}`);
        }
      }
    } else t.diagnostic('PowerShell did not create an attempt directory.');
    throw failure;
  }
});
