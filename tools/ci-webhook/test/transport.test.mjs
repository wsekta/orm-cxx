import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createHmac } from 'node:crypto';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createTestHarness } from 'wrangler';
import { Journal } from '../bridge/core.mjs';
import { connectBridge } from '../bridge/main.mjs';

test('real local Worker -> outbound WebSocket -> journal -> destination -> acknowledgment, including offline replay', { timeout: 30000 }, async t => {
  const repository = 'wsekta/orm-cxx';
  const secret = 'transport-test-webhook-secret', token = 'transport-test-bridge-token';
  const folder = mkdtempSync(join(tmpdir(), 'ci-webhook-transport-'));
  const tokenFile = join(folder, 'token.txt');
  writeFileSync(tokenFile, token);
  const server = createTestHarness({ workers: [{ configPath: './wrangler.jsonc', secrets: { GITHUB_WEBHOOK_SECRET: secret, BRIDGE_TOKEN: token }, vars: { REPOSITORY_CONFIG: JSON.stringify({ [repository]: { branch: 'main', workflows: ['GCC'] } }) } }] });
  const { url } = await server.listen();
  const journal = new Journal(join(folder, 'journal.sqlite'));
  let bridge;
  t.after(async () => { bridge?.stop(); await server.close(); journal.close(); rmSync(folder, { recursive: true }); });
  const config = { repository, endpoint: url.href, tokenFile, defaultThread: '11111111-1111-1111-1111-111111111111' };
  const messages = [];
  const sender = { probe: async () => {}, send: async (thread, message) => { messages.push({ thread, message }); } };
  async function completed(sha, id, conclusion) {
    const payload = JSON.stringify({ repository: { full_name: repository }, workflow_run: { id, run_attempt: 1, name: 'GCC', event: 'push', status: 'completed', conclusion, head_branch: 'main', head_sha: sha, html_url: `https://github.com/${repository}/actions/runs/${id}` } });
    const response = await server.fetch('/webhooks/github', { method: 'POST', body: payload, headers: { 'X-GitHub-Event': 'workflow_run', 'X-GitHub-Delivery': crypto.randomUUID(), 'X-Hub-Signature-256': 'sha256=' + createHmac('sha256', secret).update(payload).digest('hex') } });
    assert.equal(response.status, 200);
  }
  const pending = async () => (await server.fetch(`/pending?repo=${repository}`, { headers: { Authorization: `Bearer ${token}` } })).json();
  const replay = (count) => new Promise((resolve, reject) => {
    let acknowledged = 0;
    bridge = connectBridge(config, journal, sender, line => {
      const event = JSON.parse(line);
      if (event.event.endsWith('error')) reject(new Error(line));
      if (event.event === 'acknowledged' && ++acknowledged === count) resolve();
    });
  });
  await completed('d'.repeat(40), 11, 'failure');
  await completed('e'.repeat(40), 12, 'success');
  assert.equal((await pending()).length, 2);
  await replay(2);
  assert.equal(messages.length, 2);
  assert.equal(messages[0].thread, config.defaultThread);
  assert.match(messages[0].message, /"outcome":"failure"/);
  assert.match(messages[1].message, /"outcome":"success"/);
  assert.deepEqual(await pending(), []);
  bridge.stop();
  await completed('f'.repeat(40), 13, 'success');
  assert.equal((await pending()).length, 1);
  await replay(1);
  assert.equal(messages.length, 3);
  assert.deepEqual(await pending(), []);
});
