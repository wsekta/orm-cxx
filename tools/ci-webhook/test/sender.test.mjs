import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createSender } from '../bridge/sender.mjs';
import { loadConfig } from '../bridge/main.mjs';

function withReceiver(handler) {
  return new Promise(resolve => {
    const received = [];
    const server = createServer((req, res) => {
      const chunks = [];
      req.on('data', chunk => chunks.push(chunk));
      req.on('end', () => {
        const body = Buffer.concat(chunks).toString('utf8');
        received.push({ method: req.method, body: body ? JSON.parse(body) : null });
        const status = handler(received.length);
        res.writeHead(status, { 'content-type': 'application/json' });
        res.end('{}');
      });
    });
    server.listen(0, '127.0.0.1', () => {
      resolve({ server, received, url: `http://127.0.0.1:${server.address().port}/hook` });
    });
  });
}

function writeConfig(contents) {
  const folder = mkdtempSync(join(tmpdir(), 'sender-'));
  const path = join(folder, 'bridge.json');
  writeFileSync(path, JSON.stringify(contents));
  return { path, cleanup: () => rmSync(folder, { recursive: true, force: true }) };
}

test('a webhook target delivers a notice without any Codex Desktop install', async () => {
  const { server, received, url } = await withReceiver(() => 200);
  try {
    const sender = createSender({ repository: 'wsekta/orm-cxx', notifyUrls: [url] });
    assert.equal(await sender.probe(), true);
    await sender.send('thread-1', 'hello');
    assert.equal(received.length, 1);
    assert.equal(received[0].method, 'POST');
    assert.equal(received[0].body.repository, 'wsekta/orm-cxx');
    assert.equal(received[0].body.thread, 'thread-1');
    assert.equal(received[0].body.text, 'hello');
  } finally {
    server.close();
  }
});

test('every configured webhook target receives the notice', async () => {
  const first = await withReceiver(() => 200);
  const second = await withReceiver(() => 200);
  try {
    const sender = createSender({ repository: 'wsekta/orm-cxx', notifyUrls: [first.url, second.url] });
    await sender.send('thread-2', 'fan out');
    assert.equal(first.received.length, 1);
    assert.equal(second.received.length, 1);
  } finally {
    first.server.close();
    second.server.close();
  }
});

test('one failing target does not swallow a delivery that another target accepted', async () => {
  const healthy = await withReceiver(() => 200);
  const brokenUrl = 'http://127.0.0.1:1/hook';
  try {
    const lines = [];
    const sender = createSender({ repository: 'wsekta/orm-cxx', notifyUrls: [brokenUrl, healthy.url] },
      line => lines.push(line));
    await sender.send('thread-3', 'partial');
    assert.equal(healthy.received.length, 1);
    assert.equal(lines.length, 1);
    assert.match(lines[0], /webhook delivery failed/);
  } finally {
    healthy.server.close();
  }
});

test('a delivery fails when every target rejects it, so the journal keeps it uncertain', async () => {
  const { server, url } = await withReceiver(() => 500);
  try {
    const sender = createSender({ repository: 'wsekta/orm-cxx', notifyUrls: [url] });
    await assert.rejects(() => sender.send('thread-4', 'rejected'), /answered 500/);
  } finally {
    server.close();
  }
});

test('a configuration with no delivery target is refused instead of dropping notices', () => {
  const { path, cleanup } = writeConfig({
    repository: 'wsekta/orm-cxx',
    endpoint: 'https://example.workers.dev',
    journalPath: join(tmpdir(), 'journal.sqlite'),
    tokenFile: join(tmpdir(), 'token.txt')
  });
  try {
    assert.throws(() => loadConfig(path), /No delivery target configured/);
  } finally {
    cleanup();
  }
});

test('a webhook-only configuration loads without codexPath or appServer', () => {
  const { path, cleanup } = writeConfig({
    repository: 'wsekta/orm-cxx',
    endpoint: 'https://example.workers.dev',
    notifyUrls: ['https://example.com/hook'],
    journalPath: join(tmpdir(), 'journal.sqlite'),
    tokenFile: join(tmpdir(), 'token.txt')
  });
  try {
    const config = loadConfig(path);
    assert.deepEqual(config.notifyUrls, ['https://example.com/hook']);
  } finally {
    cleanup();
  }
});
