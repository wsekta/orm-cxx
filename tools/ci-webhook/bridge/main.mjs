import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import WebSocket from 'ws';
import { Journal, UncertainDelivery, command, deliverNotice, validateNotice } from './core.mjs';
import { parseAppServer, probeAppServer } from './app-server.mjs';

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
  let stopped = false, socket, retry, heartbeat, backoff = 1000, lastPong = Date.now();
  const stop = () => { stopped = true; clearTimeout(retry); clearInterval(heartbeat); socket?.close(); };
  const connect = () => {
    if (stopped) return;
    const token = readFileSync(config.tokenFile, 'utf8').trim();
    socket = new WebSocket(endpoint, { headers: { Authorization: `Bearer ${token}` }, maxPayload: 256 * 1024, handshakeTimeout: 15000 });
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
      if (data.toString() === 'pong') { lastPong = Date.now(); return; }
      chain = chain.then(async () => {
        const parsed = JSON.parse(data.toString());
        if (parsed.type === 'acked') { backoff = 1000; log(JSON.stringify({ event: 'acknowledged', id: parsed.id })); return; }
        const notice = validateNotice(parsed, config.repository);
        await deliverNotice(notice, journal, config.defaultThread, sender);
        if (current.readyState === WebSocket.OPEN) current.send(JSON.stringify({ type: 'ack', id: notice.id }));
        log(JSON.stringify({ event: 'delivered', id: notice.id, repository: notice.repository }));
      }).catch(error => {
        log(JSON.stringify({ event: 'delivery-error', reason: error.message }));
        if (error instanceof UncertainDelivery) { stop(); process.exitCode = 2; }
        else current.terminate();
      });
    });
    current.on('error', error => log(JSON.stringify({ event: 'connection-error', reason: error.message })));
    current.on('close', (code) => {
      clearInterval(heartbeat);
      if (code === 4001) { stop(); return; } // Another bridge replaced this connection.
      if (!stopped) {
        retry = setTimeout(connect, backoff + Math.floor(Math.random() * 500));
        backoff = Math.min(backoff * 2, 60000);
      }
    });
  };
  connect();
  return { stop };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const index = process.argv.indexOf('--config');
  if (index < 0 || !process.argv[index + 1]) throw new Error('Usage: node bridge/main.mjs --config <bridge.local.json>');
  const config = loadConfig(process.argv[index + 1]);
  const journal = new Journal(config.journalPath);
  const bridge = connectBridge(config, journal, codexSender(config));
  for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, () => { bridge.stop(); journal.close(); });
}
