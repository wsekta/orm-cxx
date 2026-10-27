import { request } from 'node:http';
import { createConnection } from 'node:net';
import { homedir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

export function parseAppServer(endpoint) {
  if (typeof endpoint !== 'string' || /[\0\r\n]/.test(endpoint)) throw new Error('Invalid Codex app-server endpoint');
  if (endpoint.startsWith('unix://')) {
    const path = endpoint.slice('unix://'.length) || join(process.env.CODEX_HOME || join(homedir(), '.codex'), 'app-server-control', 'app-server-control.sock');
    return { kind: 'unix', path };
  }
  // Validate the original spelling before URL parsing can normalize IP aliases.
  const match = /^ws:\/\/(127\.0\.0\.1|\[::1\]):([1-9][0-9]{0,4})\/?$/.exec(endpoint);
  if (!match || endpoint !== endpoint.trim() || Number(match[2]) > 65535) {
    throw new Error('Codex app-server requires unix:// or ws://127.0.0.1:<port>/ or ws://[::1]:<port>/');
  }
  return { kind: 'websocket', readinessUrl: `http://${match[1]}:${match[2]}/readyz` };
}

export function probeAppServer(endpoint, { timeoutMs = 5000 } = {}) {
  const target = parseAppServer(endpoint);
  if (target.kind === 'unix') {
    return new Promise((resolveProbe, reject) => {
      const socket = createConnection({ path: target.path });
      const timeout = setTimeout(() => { socket.destroy(new Error('Codex control socket timeout')); }, timeoutMs);
      socket.once('close', () => clearTimeout(timeout));
      socket.once('connect', () => { socket.destroy(); resolveProbe(); });
      socket.once('error', reject);
    });
  }
  // A direct HTTP request never follows redirects or uses an environment proxy.
  // Readiness is only a precondition: it does not establish thread delivery.
  return new Promise((resolveProbe, reject) => {
    const probe = request(target.readinessUrl, {
      method: 'GET', agent: false, signal: AbortSignal.timeout(timeoutMs)
    }, response => {
      response.destroy();
      if (response.statusCode !== 200) reject(new Error(`Codex app-server readiness returned HTTP ${response.statusCode}`));
      else resolveProbe();
    });
    probe.once('error', reject);
    probe.end();
  });
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  if (process.argv.length !== 4 || process.argv[2] !== '--probe') throw new Error('Usage: node bridge/app-server.mjs --probe <endpoint>');
  await probeAppServer(process.argv[3]);
  console.log('Codex app-server is ready; delivery remains unverified.');
}
