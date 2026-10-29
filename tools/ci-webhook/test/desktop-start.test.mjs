import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

test('Windows desktop starter records early preparation failures without starting the application', {
  skip: process.platform !== 'win32', timeout: 20000
}, t => {
  const folder = mkdtempSync(join(tmpdir(), 'desktop starter-'));
  t.after(() => rmSync(folder, { recursive: true, force: true }));
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
  const result = spawnSync(powershell, ['-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', starter, '-ProfilePath', profilePath, '-NoPause'], { encoding: 'utf8', timeout: 15000, windowsHide: true });
  assert.equal(result.error, undefined);
  assert.equal(result.status, 1, result.stdout + result.stderr);
  const attempts = readdirSync(stateRoot);
  assert.equal(attempts.length, 1);
  const error = readFileSync(join(stateRoot, attempts[0], 'error.txt'), 'utf8');
  assert.match(error, /Brak wymaganego pliku/);
  assert.ok(readFileSync(join(stateRoot, attempts[0], 'start.log'), 'utf8').length > 0);
  assert.equal(readdirSync(join(stateRoot, attempts[0])).includes('desktop.local.json'), false);
});
