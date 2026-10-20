import { readFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { createHmac } from 'node:crypto';
import { loadConfig } from '../bridge/main.mjs';
const arg = name => process.argv[process.argv.indexOf(name) + 1];
if (!['--config', '--secret-file'].every(name => process.argv.includes(name))) throw new Error('Usage: node scripts/configure-github.mjs --config <file> --secret-file <private file>');
const config = loadConfig(arg('--config'));
const endpoint = new URL('/webhooks/github', config.endpoint).href;
if (new URL(endpoint).protocol !== 'https:') throw new Error('A production GitHub webhook requires a public HTTPS receiver');
const secret = readFileSync(arg('--secret-file'), 'utf8').trim();
if (secret.length < 32) throw new Error('Webhook secret must contain at least 32 random characters');
const health = await fetch(new URL('/health', config.endpoint), { signal: AbortSignal.timeout(15000) });
if (!health.ok || (await health.json()).service !== 'codex-ci-webhook') throw new Error('Deploy and verify the receiver before enabling GitHub delivery');
const probe = JSON.stringify({ zen: 'CI webhook activation probe' });
const check = await fetch(endpoint, { method: 'POST', body: probe, signal: AbortSignal.timeout(15000), headers: {
  'X-GitHub-Event': 'ping', 'X-GitHub-Delivery': crypto.randomUUID(),
  'X-Hub-Signature-256': 'sha256=' + createHmac('sha256', secret).update(probe).digest('hex')
} });
if (!check.ok || !(await check.json()).ok) throw new Error('Receiver did not accept this webhook secret; GitHub configuration was not changed');
const gh = args => {
  const result = spawnSync('gh', args, { encoding: 'utf8', windowsHide: true });
  if (result.status !== 0) throw new Error(result.stderr || result.error?.message || 'gh failed');
  return JSON.parse(result.stdout);
};
const hooks = gh(['api', '--paginate', '--slurp', `repos/${config.repository}/hooks`]).flat();
const existing = hooks.filter(hook => hook.config?.url === endpoint);
if (existing.length > 1) throw new Error('Multiple matching webhooks found; inspect before updating');
const input = JSON.stringify({ name: 'web', active: true, events: ['push', 'workflow_job', 'workflow_run'], config: { url: endpoint, content_type: 'json', insecure_ssl: '0', secret } });
const route = `repos/${config.repository}/hooks` + (existing.length ? `/${existing[0].id}` : '');
// Secret travels on stdin, never in process arguments or output.
const response = spawnSync('gh', ['api', '--method', existing.length ? 'PATCH' : 'POST', route, '--input', '-'], { input, encoding: 'utf8', windowsHide: true });
if (response.status !== 0) throw new Error('GitHub webhook configuration failed; verify gh permissions');
const hook = JSON.parse(response.stdout);
console.log(JSON.stringify({ id: hook.id, active: hook.active, events: hook.events, receiver: endpoint }));
