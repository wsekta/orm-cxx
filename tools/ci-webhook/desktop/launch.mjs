import { createHash, randomUUID } from 'node:crypto';
import { createReadStream, existsSync, mkdirSync, openSync, readFileSync, realpathSync, writeFileSync, renameSync, unlinkSync } from 'node:fs';
import { dirname, isAbsolute, join, resolve } from 'node:path';
import { createServer } from 'node:net';
import { execFile, spawn } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { setTimeout as delay } from 'node:timers/promises';
import { parseAppServer, probeAppServer } from '../bridge/app-server.mjs';
import { connectRpc, inspectThread } from './rpc.mjs';

const directory = dirname(fileURLToPath(import.meta.url));
const runtimeFiles = ['codex.exe', 'codex-code-mode-host.exe', 'codex-windows-sandbox-setup.exe', 'codex-command-runner.exe'];
function writeJson(path, data) {
  const temporary = `${path}.${process.pid}.tmp`;
  writeFileSync(temporary, JSON.stringify(data, null, 2) + '\n', { mode: 0o600 });
  renameSync(temporary, path);
}
function execute(file, args, { timeout = 60000, jsonErrors = false, ...options } = {}) {
  return new Promise((accept, reject) => execFile(file, args, { windowsHide: true, timeout, maxBuffer: 1024 * 1024, ...options },
    (error, stdout, stderr) => {
      if (!error) { accept(stdout); return; }
      let detail = '';
      if (jsonErrors) { try { detail = JSON.parse(stderr.trim()).error ?? ''; } catch { /* Never copy arbitrary child output into status. */ } }
      reject(new Error(`Operation failed: ${file.split(/[\\/]/).at(-1)} (${error.code ?? 'unknown'}): ${detail}`));
    }));
}
async function digest(path, length) {
  const hash = createHash('sha256');
  if (length === 0) return hash.digest('hex');
  for await (const bytes of createReadStream(path, length === undefined ? {} : { end: length - 1 })) hash.update(bytes);
  return hash.digest('hex');
}
export function validateLaunchConfig(config) {
  for (const key of ['desktopPath', 'runtimeRoot', 'codexHome', 'sqliteHome', 'pythonPath', 'nodePath', 'bridgeConfig', 'stateDirectory']) {
    if (typeof config[key] !== 'string' || !isAbsolute(config[key])) throw new Error(`An absolute ${key} is required`);
  }
  if (config.bridgePaths !== undefined && (config.bridgePaths === null || typeof config.bridgePaths !== 'object'
    || Object.keys(config.bridgePaths).length !== 2 || !['tokenFile', 'journalPath'].every(key => typeof config.bridgePaths[key] === 'string' && isAbsolute(config.bridgePaths[key])))) {
    throw new Error('Invalid physical bridge storage paths');
  }
  if (!/^[a-f0-9-]{36}$/.test(config.thread ?? '')) throw new Error('Invalid destination thread');
  for (const key of ['desktopPid', 'serverPid']) if (!Number.isSafeInteger(config[key]) || config[key] <= 0) throw new Error(`Invalid ${key}`);
  for (const key of ['desktopCreated', 'serverCreated']) if (typeof config[key] !== 'string' || !/^[0-9]+$/.test(config[key])) throw new Error(`Invalid ${key}`);
  if (config.desktopPid === config.serverPid) throw new Error('Desktop and server PIDs must differ');
  if (parseAppServer(config.appServer).kind !== 'websocket') throw new Error('The pilot requires local WebSocket');
  if (!Array.isArray(config.serverArguments) || !config.serverArguments.every(value => typeof value === 'string') || config.serverArguments.some(value => value.startsWith('--listen') || value === '--stdio')) throw new Error('Invalid captured app-server arguments');
  if (!config.serverArguments.includes('app-server')) throw new Error('Captured arguments must start the app-server');
  if (!Array.isArray(config.ignoreBusyTurns ?? []) || !(config.ignoreBusyTurns ?? []).every(id => /^[a-f0-9-]{36}$/.test(id))) throw new Error('Invalid stale turn IDs');
  return config;
}
export async function resolveRuntime(config) {
  const resources = join(dirname(config.desktopPath), 'resources');
  const combined = createHash('sha256'), expected = new Map();
  for (const name of runtimeFiles) {
    const checksum = await digest(join(resources, name));
    expected.set(name, checksum);
    combined.update(name); combined.update('\0'); combined.update(checksum); combined.update('\0');
  }
  const root = join(config.runtimeRoot, combined.digest('hex').slice(0, 16));
  for (const [name, checksum] of expected) if (await digest(join(root, name)) !== checksum) throw new Error('Desktop/CLI runtime mismatch; stop rather than selecting another version');
  return { codexPath: join(root, 'codex.exe'), resources };
}
export async function verifyHistory(before, after) {
  if (before.thread !== after.thread || JSON.stringify(before.historyTables) !== JSON.stringify(after.historyTables)) throw new Error('Persisted conversation history changed during the switch');
  const attachments = history => history.contextTables?.filter(value => value.table === 'thread_attachments') ?? [];
  if (JSON.stringify(attachments(before)) !== JSON.stringify(attachments(after))) throw new Error('Conversation attachments changed during the switch');
  for (const original of before.rollouts) {
    const current = after.rollouts.find(value => value.path === original.path);
    if (!current || current.bytes < original.bytes || await digest(current.path, original.bytes) !== original.sha256) throw new Error('Original conversation rollout prefix was not preserved');
  }
}
async function reserveCheck(endpoint) {
  const url = new URL(endpoint);
  await new Promise((accept, reject) => {
    const probe = createServer();
    probe.once('error', reject);
    probe.listen(Number(url.port || 80), url.hostname === '[::1]' ? '::1' : url.hostname, () => probe.close(accept));
  });
}
export async function runPilot(config, { scheduledTask } = {}) {
  validateLaunchConfig(config);
  if (process.platform !== 'win32' || !/^CodexCiWebhook-DesktopPilot-[a-f0-9-]{36}$/i.test(scheduledTask ?? '')) {
    throw new Error('The Windows pilot must be started by its independent scheduled task; use --arm');
  }
  mkdirSync(config.stateDirectory, { recursive: true });
  const statePath = join(config.stateDirectory, 'status.json');
  const lockPath = join(config.stateDirectory, 'launcher.lock');
  const lock = openSync(lockPath, 'wx', 0o600);
  // The open descriptor and file prevent a concurrent launcher from switching again.
  const state = { version: 1, thread: config.thread, nonce: randomUUID(), phase: 'waiting-for-idle', launcherPid: process.pid, appServer: config.appServer, scheduledTask };
  const save = phase => { state.phase = phase; state.updatedAt = new Date().toISOString(); writeJson(statePath, state); };
  const native = async (command, ...args) => JSON.parse(await execute(config.pythonPath, [join(directory, 'windows.py'), command, ...args], { jsonErrors: true }));
  const storage = ['--home', config.codexHome, '--sqlite-home', config.sqliteHome, '--thread', config.thread];
  let desktop, server, desktopExit, serverExit, originalClosed = false;
  let client;
  const cleanEnvironment = { ...process.env, CODEX_HOME: config.codexHome, CODEX_SQLITE_HOME: config.sqliteHome };
  delete cleanEnvironment.CODEX_APP_SERVER_WS_URL;
  delete cleanEnvironment.CODEX_APP_SERVER_FORCE_CLI;
  delete cleanEnvironment.CODEX_APP_TOOLS_PIPE_PATH;
  const startDesktop = env => spawn(config.desktopPath, [`codex://threads/${config.thread}?hostId=local`], { env, cwd: dirname(config.desktopPath), stdio: 'ignore', windowsHide: false });
  try {
    save('waiting-for-idle');
    const runtime = await resolveRuntime(config);
    state.codexPath = runtime.codexPath;
    await reserveCheck(config.appServer);
    // Delayed, bounded operational idle check. This never polls GitHub Actions.
    const deadline = Date.now() + 180000;
    for (;;) {
      const idle = await native('idle', '--sqlite-home', config.sqliteHome, '--thread', config.thread, ...(config.ignoreBusyTurns ?? []).flatMap(id => ['--ignore-turn', id]));
      if (idle.busyThreadIds.length === 0) break;
      if (Date.now() > deadline) throw new Error('Desktop still has active turns; restart was not attempted');
      await delay(1000);
    }
    const oldServer = await native('shutdown-plan', '--pid', String(config.serverPid));
    if (oldServer.requestedCreationTime !== config.serverCreated) throw new Error('Original app-server process identity changed');
    save('closing-original-desktop');
    await native('close', '--pid', String(config.desktopPid), '--expected-created', config.desktopCreated, '--timeout', '30');
    originalClosed = true;
    const stopDeadline = Date.now() + 30000;
    while ((await native('alive', '--pid', String(config.serverPid))).alive) {
      if (Date.now() > stopDeadline) throw new Error('Original app-server did not exit; no replacement was started');
      await delay(500);
    }
    const backupDirectory = join(config.stateDirectory, 'backups', state.nonce);
    save('snapshotting');
    const snapshot = await native('snapshot', ...storage, '--destination', backupDirectory);
    state.backupDirectory = snapshot.destination;
    const before = snapshot.history;
    save('starting-desktop-client');
    desktop = startDesktop({ ...cleanEnvironment, CODEX_APP_SERVER_WS_URL: config.appServer });
    desktopExit = new Promise(accept => desktop.once('exit', accept));
    await new Promise((accept, reject) => { desktop.once('spawn', accept); desktop.once('error', reject); });
    state.desktopPid = desktop.pid;
    let pipe;
    const pipeDeadline = Date.now() + 30000;
    while (!pipe) {
      try { pipe = await native('pipe', '--pid', String(desktop.pid)); }
      catch { if (Date.now() > pipeDeadline || desktop.exitCode !== null) throw new Error('New Desktop tool channel was not found'); await delay(200); }
    }
    const serverLog = openSync(join(config.stateDirectory, 'app-server.log'), 'a', 0o600);
    server = spawn(runtime.codexPath, [...config.serverArguments, '--listen', config.appServer], {
      cwd: config.codexHome, stdio: ['ignore', serverLog, serverLog], windowsHide: true,
      env: { ...cleanEnvironment, LOG_FORMAT: 'json', RUST_LOG: 'warn', CODEX_INTERNAL_ORIGINATOR_OVERRIDE: 'Codex Desktop', CODEX_MCP_NODE_PATH: config.nodePath, CODEX_CLI_PATH: runtime.codexPath, CODEX_APP_RUNTIME_CLI_PATH: runtime.codexPath, CODEX_ELECTRON_RESOURCES_PATH: runtime.resources, CODEX_APP_TOOLS_PIPE_PATH: pipe.pipePath }
    });
    serverExit = new Promise(accept => server.once('exit', accept));
    await new Promise((accept, reject) => { server.once('spawn', accept); server.once('error', reject); });
    state.serverPid = server.pid;
    save('waiting-for-shared-server');
    const readyDeadline = Date.now() + 45000;
    for (;;) {
      try { await probeAppServer(config.appServer); break; }
      catch { if (Date.now() > readyDeadline || server.exitCode !== null) throw new Error('Shared app-server did not become ready'); await delay(500); }
    }
    client = await connectRpc(config.appServer);
    const resumeDeadline = Date.now() + 60000;
    let thread;
    for (;;) {
      try { thread = await inspectThread(client, config.thread); break; }
      catch { if (Date.now() > resumeDeadline) throw new Error('Desktop did not resume the same conversation'); await delay(1000); }
    }
    await verifyHistory(before, await native('history', ...storage));
    state.historyVerified = true;
    state.turns = thread.turns;
    const originalBridge = readFileSync(config.bridgeConfig, 'utf8');
    writeFileSync(join(config.stateDirectory, `bridge-before-${state.nonce}.json`), originalBridge, { mode: 0o600 });
    const bridge = JSON.parse(originalBridge);
    writeJson(config.bridgeConfig, { ...bridge, ...config.bridgePaths, codexPath: runtime.codexPath, appServer: config.appServer });
    save('sending-idle-wake-probe');
    client.close(); client = undefined;
    // A single authorized probe. Do not activate CI based on queue acceptance.
    const message = `Wznowienie po zaakceptowanej próbie transportu Desktop. UUID tej rozmowy: ${config.thread}. Nonce: ${state.nonce}. Odczytaj ${statePath}; porównanie zapisanej historii przeszło. Zweryfikuj rzeczywiste narzędzia Desktop, uprawnienia i ten nonce w tej samej rozmowie. Dopiero po potwierdzeniu idle wake dokończ bridge i aktywację webhooka zgodnie z pierwotnym zleceniem. Samo przyjęcie tej wiadomości przez queue nie oznacza pełnego sukcesu. Nie uruchamiaj ponownie tego launchera.`;
    await execute(runtime.codexPath, ['queue', '--remote', config.appServer, '--thread', config.thread, '--message', message], { timeout: 30000, env: cleanEnvironment });
    state.notificationsActive = false;
    save('awaiting-conversation-verification');
    // Keep ownership of the server; no CI polling and no automatic activation.
    const exited = await Promise.race([desktopExit.then(() => 'desktop'), serverExit.then(() => 'server')]);
    if (exited === 'server') throw new Error('Shared app-server exited unexpectedly');
    save('desktop-exited');
    if ((await native('alive', '--pid', String(server.pid))).alive) await native('close', '--pid', String(server.pid), '--timeout', '30');
  } catch (error) {
    state.error = error.message;
    save('rolling-back');
    client?.close();
    let replacementExited = true;
    for (const child of [desktop, server]) {
      if (child?.pid && child.exitCode === null) {
        try { await native('close', '--pid', String(child.pid), '--timeout', '30'); }
        catch { replacementExited = false; }
      }
    }
    // A failed shutdown helper may still have closed Desktop. Observe the actual
    // result before deciding whether the previous startup needs restoring.
    const oldDesktopAlive = (await native('alive', '--pid', String(config.desktopPid))).alive;
    const oldServerAlive = (await native('alive', '--pid', String(config.serverPid))).alive;
    if (!oldDesktopAlive && replacementExited && !oldServerAlive) {
      const previousBridge = join(config.stateDirectory, `bridge-before-${state.nonce}.json`);
      if (existsSync(previousBridge)) writeFileSync(config.bridgeConfig, readFileSync(previousBridge));
      const restored = startDesktop(cleanEnvironment);
      const restoredExit = new Promise(accept => restored.once('exit', accept));
      try { await new Promise((accept, reject) => { restored.once('spawn', accept); restored.once('error', reject); }); }
      catch { save('rollback-blocked'); throw new Error('Normal Desktop could not be restarted; inspect the saved status'); }
      state.restoredDesktopPid = restored.pid;
      save('rolled-back');
      // Task Scheduler may own the process tree too. Keep its action alive until
      // the restored Desktop exits instead of terminating its job underneath it.
      process.exitCode = 1;
      await restoredExit;
    } else save(!oldDesktopAlive || originalClosed ? 'rollback-blocked' : 'aborted-before-restart');
    process.exitCode = 1;
  } finally {
    client?.close();
    // Deliberately retain the status and backups, never restore transcript files.
    const { closeSync } = await import('node:fs');
    closeSync(lock);
    if (state.phase !== 'rollback-blocked') unlinkSync(lockPath);
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const index = process.argv.indexOf('--config');
  if (index < 0 || !process.argv[index + 1]) throw new Error('Usage: node desktop/launch.mjs --config <private file> --arm|--switch');
  const configPath = realpathSync.native(resolve(process.argv[index + 1]));
  const config = validateLaunchConfig(JSON.parse(readFileSync(configPath, 'utf8')));
  if (process.argv.includes('--arm')) {
    if (process.platform !== 'win32') throw new Error('This launcher requires Windows');
    // detached/unref do not escape an inherited Windows Job Object. The OS
    // scheduler owns this on-demand task independently of the closing Desktop.
    const powershell = join(process.env.SystemRoot ?? 'C:\\Windows', 'System32', 'WindowsPowerShell', 'v1.0', 'powershell.exe');
    const output = await execute(powershell, ['-NoProfile', '-NonInteractive', '-WindowStyle', 'Hidden', '-File', join(directory, 'arm.ps1'),
      '-ConfigPath', configPath, '-NodePath', process.execPath, '-LauncherPath', fileURLToPath(import.meta.url)], { timeout: 45000 });
    console.log(JSON.stringify(JSON.parse(output.trim())));
  } else if (process.argv.includes('--switch')) {
    const taskIndex = process.argv.indexOf('--scheduled-task');
    await runPilot(config, { scheduledTask: taskIndex < 0 ? undefined : process.argv[taskIndex + 1] });
  }
  else throw new Error('Explicit --arm or --switch is required');
}
