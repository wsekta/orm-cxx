import { request } from 'node:https';
import { request as httpRequest } from 'node:http';
import { command } from './core.mjs';

/**
 * A delivery target that never needs Codex Desktop.
 *
 * Each URL is POSTed a small JSON body. The shape is deliberately generic: a
 * plain webhook receiver, a chat relay or a local script endpoint can all read
 * it, and nothing here depends on an app-server socket or a Codex binary.
 */
function postJson(url, payload, timeoutMs) {
  return new Promise((resolve, reject) => {
    const target = new URL(url);
    const body = JSON.stringify(payload);
    const send = target.protocol === 'https:' ? request : httpRequest;
    const req = send(target, {
      method: 'POST',
      headers: { 'content-type': 'application/json', 'content-length': Buffer.byteLength(body) },
      timeout: timeoutMs
    }, response => {
      const chunks = [];
      response.on('data', chunk => chunks.push(chunk));
      response.on('end', () => {
        if (response.statusCode >= 200 && response.statusCode < 300) resolve(Buffer.concat(chunks).toString('utf8'));
        else reject(new Error(`Webhook ${target.host} answered ${response.statusCode}: ${Buffer.concat(chunks).toString('utf8').slice(0, 200)}`));
      });
    });
    req.on('timeout', () => req.destroy(new Error(`Webhook ${target.host} timed out after ${timeoutMs}ms`)));
    req.on('error', reject);
    req.end(body);
  });
}

/**
 * Build the sender used by the bridge and the drain command.
 *
 * `config.notifyUrls` is the primary transport and needs no Codex install. The
 * Codex queue path stays available as an opt-in extra (`config.codexPath` plus
 * `config.appServer`); when neither is configured the bridge refuses to start
 * rather than silently dropping notices.
 */
export function createSender(config, log = () => {}) {
  const notifyUrls = Array.isArray(config.notifyUrls) ? config.notifyUrls : [];
  const timeoutMs = config.notifyTimeoutMs ?? 10000;
  const codex = config.codexPath && config.appServer
    ? {
      probe: () => import('./app-server.mjs').then(({ probeAppServer }) => probeAppServer(config.appServer)),
      send: (thread, text) => command(config.codexPath, ['queue', '--remote', config.appServer, '--thread', thread, '--message', text])
    }
    : null;
  if (notifyUrls.length === 0 && !codex)
    throw new Error('No delivery target configured: set notifyUrls, or both codexPath and appServer');

  return {
    probe: async () => {
      if (notifyUrls.length > 0) return true;
      return codex.probe();
    },
    async send(thread, text, notice) {
      const deliveries = notifyUrls.map(url => postJson(url, {
        repository: config.repository,
        thread,
        text,
        notice: notice ?? null
      }, timeoutMs));
      const results = await Promise.allSettled(deliveries);
      const failures = results.filter(result => result.status === 'rejected');
      if (deliveries.length > 0 && failures.length === deliveries.length) {
        throw new Error(failures.map(failure => failure.reason?.message ?? String(failure.reason)).join('; '));
      }
      for (const failure of failures) log(`webhook delivery failed: ${failure.reason?.message ?? failure.reason}`);
      if (codex) await codex.send(thread, text);
    }
  };
}
