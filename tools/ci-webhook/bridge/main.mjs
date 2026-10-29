import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import WebSocket from 'ws';
import { Journal, command, validateNotice } from './core.mjs';
import { parseAppServer, probeAppServer } from './app-server.mjs';
import { createDispatcher } from './dispatcher.mjs';
import { acquireDispatcherLock } from './dispatcher-lock.mjs';

export function loadConfig(path) {
  const config = JSON.parse(readFileSync(path, 'utf8'));
  if (!/^[\w.-]+\/[\w.-]+$/.test(config.repository)) throw new Error('Invalid repository');
  const endpoint = new URL(config.endpoint);
  const local = ['localhost', '127.0.0.1', '[::1]'].includes(endpoint.hostname);
  if (endpoint.protocol !== 'https:' && !(local && endpoint.protocol === 'http:')) throw new Error('Receiver must use HTTPS');
  if (endpoint.username || endpoint.password || endpoint.search || endpoint.hash) throw new Error('Invalid receiver URL');
  // Use a shared, existing local app-server. Never auto-start another thread writer.
  parseAppServer(config.appServer);
  if (!config.codexPath || !config.journalPath || !config.tokenFile) throw new Error('Missing bridge configuration');
  return config;
}

export function codexSender(config) {
  return {
    probe: () => probeAppServer(config.appServer),
    send: (thread, text) => command(config.codexPath, ['queue', '--remote', config.appServer, '--thread', thread, '--message', text])
  };
}

export function connectBridge(config, journal, sender, log = console.log) {
  const endpoint = new URL('/bridge', config.endpoint);
  endpoint.protocol = endpoint.protocol === 'https:' ? 'wss:' : 'ws:';
  endpoint.searchParams.set('repo', config.repository);
  const lock = acquireDispatcherLock(journal.path ?? config.journalPath);
  let stopped = false, socket, retry, heartbeat, dispatcher, shutdown, failure;
  let backoff = 1000, lastPong = Date.now(), resolveClosed;
  const closed = new Promise(resolveClosedPromise => { resolveClosed = resolveClosedPromise; });
  const stop = error => {
    if (error && !failure) failure = error;
    if (!shutdown) {
      stopped = true;
      clearTimeout(retry);
      clearInterval(heartbeat);
      socket?.close();
      shutdown = Promise.resolve(dispatcher?.stop()).catch(shutdownError => {
        failure ??= shutdownError;
      }).then(() => {
        try { lock.release(); } catch (lockError) { failure ??= lockError; }
        resolveClosed({ error: failure });
      });
    }
    return shutdown;
  };
  try {
    dispatcher = createDispatcher(journal, sender, {
      concurrency: config.concurrency ?? 2,
      reconcileIntervalMs: config.reconcileIntervalMs ?? 1000,
      retryDelayMs: config.retryDelayMs ?? 1000,
      log, onFatal: error => { void stop(error); }
    });
  } catch (error) {
    lock.release();
    throw error;
  }
  const reconnect = () => {
    if (stopped) return;
    retry = setTimeout(connect, backoff + Math.floor(Math.random() * 500));
    backoff = Math.min(backoff * 2, 60000);
  };
  const connect = () => {
    if (stopped) return;
    try {
      const token = readFileSync(config.tokenFile, 'utf8').trim();
      if (!token) throw new Error('Missing receiver token');
      socket = new WebSocket(endpoint, { headers: { Authorization: `Bearer ${token}` }, maxPayload: 256 * 1024, handshakeTimeout: 15000 });
    } catch (error) {
      log(JSON.stringify({ event: 'connection-error', reason: error.message }));
      reconnect();
      return;
    }
    const current = socket;
    let chain = Promise.resolve();
    current.on('open', () => {
      log(JSON.stringify({ event: 'connected', repository: config.repository }));
      lastPong = Date.now();
      heartbeat = setInterval(() => {
        if (Date.now() - lastPong > 90000) current.terminate();
        else if (current.readyState === WebSocket.OPEN) current.send('ping');
      }, 30000);
    });
    current.on('message', data => {
      if (stopped) return;
      if (data.toString() === 'pong') { lastPong = Date.now(); return; }
      chain = chain.then(() => {
        if (stopped) return;
        const parsed = JSON.parse(data.toString());
        if (parsed.type === 'acked') {
          if (typeof parsed.id !== 'string') throw new Error('Invalid receiver acknowledgment');
          journal.acknowledged(parsed.id);
          backoff = 1000;
          log(JSON.stringify({ event: 'acknowledged', id: parsed.id }));
          return;
        }
        const notice = validateNotice(parsed, config.repository);
        // ACK is a custody handoff to committed local SQLite, not Desktop success.
        const receipt = journal.intake(notice);
        if (current.readyState === WebSocket.OPEN) current.send(JSON.stringify({ type: 'ack', id: notice.id }));
        log(JSON.stringify({ event: 'locally-received', id: notice.id, status: receipt.status, destination: receipt.destination }));
        dispatcher.wake();
      }).catch(error => {
        log(JSON.stringify({ event: 'receipt-error', reason: error.message }));
        current.terminate();
      });
    });
    current.on('error', error => { if (!stopped) log(JSON.stringify({ event: 'connection-error', reason: error.message })); });
    current.on('close', (code) => {
      log(JSON.stringify({ event: 'disconnected', code, repository: config.repository }));
      clearInterval(heartbeat);
      if (code === 4001) { void stop(new Error('Receiver connection was replaced; dispatcher stopped')); return; }
      reconnect();
    });
  };
  connect();
  return { stop, closed, wake: dispatcher.wake };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const index = process.argv.indexOf('--config');
  if (index < 0 || !process.argv[index + 1]) throw new Error('Usage: node bridge/main.mjs --config <bridge.local.json>');
  const config = loadConfig(process.argv[index + 1]);
  const journal = new Journal(config.journalPath);
  try {
    const bridge = connectBridge(config, journal, codexSender(config));
    for (const signal of ['SIGINT', 'SIGTERM']) process.once(signal, () => { void bridge.stop(); });
    const { error } = await bridge.closed;
    if (error) process.exitCode = 2;
  } finally { journal.close(); }
}
