import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { mkdtempSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { homedir, tmpdir } from 'node:os';
import { parseAppServer, probeAppServer } from '../bridge/app-server.mjs';
import { codexSender } from '../bridge/main.mjs';
import { Journal, deliverNotice } from '../bridge/core.mjs';

async function httpFixture(t, handler) {
  const server = createServer(handler);
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  t.after(() => new Promise((resolve, reject) => {
    server.close(error => error ? reject(error) : resolve());
    server.closeAllConnections();
  }));
  return `ws://127.0.0.1:${server.address().port}/`;
}

test('retains default and explicit Unix control sockets', () => {
  const defaultSocket = parseAppServer('unix://');
  assert.equal(defaultSocket.kind, 'unix');
  assert.equal(defaultSocket.path, join(process.env.CODEX_HOME || join(homedir(), '.codex'), 'app-server-control', 'app-server-control.sock'));
  assert.deepEqual(parseAppServer('unix:///tmp/codex.sock'), { kind: 'unix', path: '/tmp/codex.sock' });
});

test('accepts only exact literal loopback WebSocket addresses with explicit ports', () => {
  assert.deepEqual(parseAppServer('ws://127.0.0.1:4500/'), { kind: 'websocket', readinessUrl: 'http://127.0.0.1:4500/readyz' });
  assert.deepEqual(parseAppServer('ws://[::1]:4500'), { kind: 'websocket', readinessUrl: 'http://[::1]:4500/readyz' });
  assert.equal(parseAppServer('ws://127.0.0.1:80/').kind, 'websocket');
  for (const endpoint of [
    undefined, '', 'http://127.0.0.1:4500/', 'wss://127.0.0.1:4500/',
    'ws://localhost:4500/', 'ws://example.com:4500/', 'ws://0.0.0.0:4500/',
    'ws://127.0.0.2:4500/', 'ws://127.0.0.1.example.com:4500/',
    'ws://127.1:4500/', 'ws://2130706433:4500/', 'ws://0x7f000001:4500/',
    'ws://127.000.000.001:4500/', 'ws://[0:0:0:0:0:0:0:1]:4500/',
    'ws://127.0.0.1/', 'ws://127.0.0.1:0/', 'ws://127.0.0.1:65536/',
    'ws://127.0.0.1:04500/', 'ws://127.0.0.1:1e3/',
    'ws://user:password@127.0.0.1:4500/', 'ws://127.0.0.1:4500/?token=secret',
    'ws://127.0.0.1:4500/?', 'ws://127.0.0.1:4500/#',
    'ws://127.0.0.1:4500/other', 'ws://127.0.0.1:4500/../',
    'ws://127.0.0.1:4500/%2f', 'ws://127.0.0.1:4500/\n',
    ' ws://127.0.0.1:4500/', 'unix://\0'
  ]) assert.throws(() => parseAppServer(endpoint), undefined, String(endpoint));
});

test('loopback WebSocket readiness makes a direct GET /readyz request', async t => {
  const requests = [];
  const endpoint = await httpFixture(t, (request, response) => {
    requests.push({ method: request.method, url: request.url });
    response.writeHead(200).end('ready');
  });
  await probeAppServer(endpoint);
  assert.deepEqual(requests, [{ method: 'GET', url: '/readyz' }]);
});

test('readiness rejects redirects without contacting their destination', async t => {
  let followed = 0;
  const destination = await httpFixture(t, (_request, response) => {
    followed++;
    response.writeHead(200).end();
  });
  const endpoint = await httpFixture(t, (_request, response) => {
    response.writeHead(302, { Location: destination.replace('ws:', 'http:') }).end();
  });
  await assert.rejects(probeAppServer(endpoint), /HTTP 302/);
  assert.equal(followed, 0);
});

test('failed WS readiness leaves the real journal notice pending before any send', async t => {
  const endpoint = await httpFixture(t, (_request, response) => response.writeHead(503).end());
  const folder = mkdtempSync(join(tmpdir(), 'ci-app-server-'));
  const journal = new Journal(join(folder, 'journal.sqlite'));
  t.after(() => { journal.close(); rmSync(folder, { recursive: true }); });
  const sha = 'a'.repeat(40);
  const notice = { id: `${sha}:failure:1:1`, repository: 'wsekta/orm-cxx', sha,
    outcome: 'failure', url: 'https://github.com/wsekta/orm-cxx/actions/runs/1' };
  const sender = { ...codexSender({ appServer: endpoint }), send: async () => assert.fail('Readiness must precede claiming and sending') };
  await assert.rejects(deliverNotice(notice, journal, '11111111-1111-1111-1111-111111111111', sender), /HTTP 503/);
  assert.equal(journal.receive(notice), 'received');
});

test('readiness requires HTTP 200 rather than any successful response', async t => {
  const endpoint = await httpFixture(t, (_request, response) => response.writeHead(204).end());
  await assert.rejects(probeAppServer(endpoint), /HTTP 204/);
});

test('readiness has a bounded total timeout when the endpoint does not answer', async t => {
  const endpoint = await httpFixture(t, () => {});
  await assert.rejects(probeAppServer(endpoint, { timeoutMs: 50 }), /abort/i);
});
