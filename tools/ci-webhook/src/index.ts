import { DurableObject } from "cloudflare:workers";
import { timingSafeEqual } from "node:crypto";
import { normalize, reduce, validBranch, workflowsForBranch, type Commit, type Event, type Notice, type RepositoryPolicy } from "./state";

type Bindings = Env & { GITHUB_WEBHOOK_SECRET: string; BRIDGE_TOKEN: string; LEGACY_COMMIT_BRANCHES?: string };
type Configuration = Record<string, RepositoryPolicy>;
const encoder = new TextEncoder();
const limit = 2 * 1024 * 1024;
function configuration(env: Bindings): Configuration { return JSON.parse(env.REPOSITORY_CONFIG); }

export function migrateLegacyCommits(storage: DurableObjectStorage, branchOverrides = "{}"): void {
  storage.transactionSync(() => {
    if (storage.sql.exec("SELECT name FROM schema_migrations WHERE name = ?", "branch-commits-v1").toArray().length) return;
    const overrides: unknown = JSON.parse(branchOverrides);
    if (!overrides || typeof overrides !== "object" || Array.isArray(overrides)
      || !Object.entries(overrides).every(([sha, branch]) => /^[a-f0-9]{40}$/.test(sha) && validBranch(branch))) {
      throw new Error("Invalid legacy commit branch overrides");
    }
    const encoded = JSON.stringify(overrides);
    storage.sql.exec(`INSERT OR IGNORE INTO commits_by_branch (branch, sha, data, updated)
      SELECT COALESCE(json_extract(?, '$."' || sha || '"'), 'main'), sha,
        json_set(data, '$.branch', COALESCE(json_extract(?, '$."' || sha || '"'), 'main')), updated FROM commits`, encoded, encoded);
    storage.sql.exec("INSERT INTO schema_migrations VALUES (?)", "branch-commits-v1");
  });
}

async function signature(secret: string, header: string | null, body: Uint8Array): Promise<boolean> {
  if (!secret || !header || !/^sha256=[a-f0-9]{64}$/.test(header)) return false;
  const key = await crypto.subtle.importKey("raw", encoder.encode(secret), { name: "HMAC", hash: "SHA-256" }, false, ["verify"]);
  const bytes = Uint8Array.from(header.slice(7).match(/../g)!, pair => parseInt(pair, 16));
  return crypto.subtle.verify("HMAC", key, bytes, body);
}
function authorized(request: Request, secret: string): boolean {
  if (!secret) return false;
  const actual = encoder.encode(request.headers.get("Authorization") ?? "");
  const expected = encoder.encode(`Bearer ${secret}`);
  return actual.length === expected.length && timingSafeEqual(actual, expected);
}
async function boundedBody(request: Request): Promise<Uint8Array | null> {
  if (Number(request.headers.get("Content-Length")) > limit) return null;
  const reader = request.body?.getReader();
  if (!reader) return new Uint8Array();
  const chunks: Uint8Array[] = [];
  let size = 0;
  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    size += value.length;
    if (size > limit) { await reader.cancel(); return null; }
    chunks.push(value);
  }
  const result = new Uint8Array(size);
  let offset = 0;
  for (const chunk of chunks) { result.set(chunk, offset); offset += chunk.length; }
  return result;
}

export class RepositoryEvents extends DurableObject<Bindings> {
  constructor(ctx: DurableObjectState, env: Bindings) {
    super(ctx, env);
    ctx.storage.sql.exec(`CREATE TABLE IF NOT EXISTS deliveries (id TEXT PRIMARY KEY, received INTEGER NOT NULL);
      CREATE TABLE IF NOT EXISTS commits (sha TEXT PRIMARY KEY, data TEXT NOT NULL, updated INTEGER NOT NULL);
      CREATE TABLE IF NOT EXISTS commits_by_branch (branch TEXT NOT NULL, sha TEXT NOT NULL, data TEXT NOT NULL, updated INTEGER NOT NULL, PRIMARY KEY (branch, sha));
      CREATE TABLE IF NOT EXISTS pending_jobs (branch TEXT NOT NULL, sha TEXT NOT NULL, workflow TEXT NOT NULL, run_id INTEGER NOT NULL, attempt INTEGER NOT NULL, data TEXT NOT NULL, received INTEGER NOT NULL, PRIMARY KEY (branch, sha, workflow, run_id, attempt));
      CREATE TABLE IF NOT EXISTS schema_migrations (name TEXT PRIMARY KEY);
      CREATE TABLE IF NOT EXISTS outbox (seq INTEGER PRIMARY KEY AUTOINCREMENT, id TEXT UNIQUE NOT NULL, data TEXT NOT NULL);`);
    migrateLegacyCommits(ctx.storage, env.LEGACY_COMMIT_BRANCHES);
  }
  ingest(repository: string, delivery: string, event: Event): { duplicate: boolean; notified: boolean } {
    const expected = workflowsForBranch(configuration(this.env)[repository], event.branch);
    if (!expected) return { duplicate: false, notified: false };
    const notices: Notice[] = [];
    let duplicate = false;
    this.ctx.storage.transactionSync(() => {
      if (this.ctx.storage.sql.exec("SELECT id FROM deliveries WHERE id = ?", delivery).toArray().length) { duplicate = true; return; }
      const stored = this.ctx.storage.sql.exec<{ data: string }>("SELECT data FROM commits_by_branch WHERE branch = ? AND sha = ?", event.branch, event.sha).toArray()[0];
      // A job from an unrelated PR/fork must not start monitoring a new commit.
      if (event.type === "job" && !stored) return;
      const state: Commit = stored ? JSON.parse(stored.data) : { branch: event.branch, sha: event.sha, runs: {} };
      if (state.branch !== event.branch || state.sha !== event.sha) throw new Error("Stored commit scope mismatch");
      const apply = (next: Event) => { const notice = reduce(state, next, expected, repository); if (notice) notices.push(notice); };
      if (event.type === "job" && event.run && event.workflow) {
        const prior = state.runs[event.workflow];
        if (prior && prior.id === event.run.id && prior.attempt === event.run.attempt) apply(event);
        else if (!prior || event.run.id > prior.id || (event.run.id === prior.id && event.run.attempt > prior.attempt)) {
          // Keep the first failed matrix job until its trigger is confirmed.
          // A PR job has no matching push workflow_run and is never promoted.
          this.ctx.storage.sql.exec("INSERT OR IGNORE INTO pending_jobs VALUES (?, ?, ?, ?, ?, ?, ?)",
            event.branch, event.sha, event.workflow, event.run.id, event.run.attempt, JSON.stringify(event), Date.now());
        }
      } else if (event.type === "run" && event.run && event.workflow) {
        const pending = this.ctx.storage.sql.exec<{ data: string }>(
          "SELECT data FROM pending_jobs WHERE branch = ? AND sha = ? AND workflow = ? AND run_id = ? AND attempt = ?",
          event.branch, event.sha, event.workflow, event.run.id, event.run.attempt).toArray()[0];
        const job: Event | undefined = pending ? JSON.parse(pending.data) : undefined;
        // Completed success is authoritative. For a completed failure, retain
        // the buffered matrix job's name/link in the first notification.
        if (event.run.status === "completed" && event.run.conclusion !== "success" && job?.run) {
          apply({ ...event, run: { ...event.run, failedJob: job.job, url: job.run.url } });
        } else apply(event);
        if (job && event.run.status !== "completed") apply(job);
        this.ctx.storage.sql.exec(`DELETE FROM pending_jobs WHERE branch = ? AND sha = ? AND workflow = ?
          AND (run_id < ? OR (run_id = ? AND attempt <= ?))`, event.branch, event.sha, event.workflow, event.run.id, event.run.id, event.run.attempt);
      } else apply(event);
      this.ctx.storage.sql.exec("INSERT INTO deliveries VALUES (?, ?)", delivery, Date.now());
      this.ctx.storage.sql.exec("INSERT OR REPLACE INTO commits_by_branch VALUES (?, ?, ?, ?)", state.branch, state.sha, JSON.stringify(state), Date.now());
      for (const notice of notices) this.ctx.storage.sql.exec("INSERT OR IGNORE INTO outbox (id, data) VALUES (?, ?)", notice.id, JSON.stringify(notice));
      const cutoff = Date.now() - 30 * 86400000;
      this.ctx.storage.sql.exec("DELETE FROM deliveries WHERE received < ?", cutoff);
      this.ctx.storage.sql.exec(`DELETE FROM commits_by_branch WHERE updated < ? AND NOT EXISTS
        (SELECT 1 FROM outbox WHERE json_extract(outbox.data, '$.sha') = commits_by_branch.sha
          AND COALESCE(json_extract(outbox.data, '$.branch'), 'main') = commits_by_branch.branch)`, cutoff);
      this.ctx.storage.sql.exec(`DELETE FROM pending_jobs WHERE received < ? OR NOT EXISTS
        (SELECT 1 FROM commits_by_branch WHERE commits_by_branch.branch = pending_jobs.branch AND commits_by_branch.sha = pending_jobs.sha)`, cutoff);
    });
    if (notices.length) for (const socket of this.ctx.getWebSockets()) this.sendNext(socket);
    return { duplicate, notified: notices.length > 0 };
  }
  pending(): Notice[] {
    return this.ctx.storage.sql.exec<{ data: string }>("SELECT data FROM outbox ORDER BY seq LIMIT 100").toArray().map(row => JSON.parse(row.data));
  }
  fetch(request: Request): Response {
    if (request.headers.get("Upgrade")?.toLowerCase() !== "websocket") return new Response("WebSocket required", { status: 426 });
    for (const prior of this.ctx.getWebSockets()) prior.close(4001, "Bridge reconnected");
    const pair = new WebSocketPair();
    this.ctx.acceptWebSocket(pair[1]);
    this.ctx.setWebSocketAutoResponse(new WebSocketRequestResponsePair("ping", "pong"));
    this.sendNext(pair[1]);
    return new Response(null, { status: 101, webSocket: pair[0] });
  }
  private sendNext(socket: WebSocket): void {
    if (socket.deserializeAttachment()?.inflight) return;
    const next = this.pending()[0];
    if (!next) return;
    try {
      socket.serializeAttachment({ inflight: next.id });
      socket.send(JSON.stringify(next));
    } catch { socket.close(1011, "Reconnect for replay"); }
  }
  async webSocketMessage(socket: WebSocket, message: string | ArrayBuffer): Promise<void> {
    if (typeof message !== "string" || message.length > 2048) { socket.close(1008, "Invalid message"); return; }
    try {
      const ack = JSON.parse(message);
      if (ack.type !== "ack" || typeof ack.id !== "string") throw new Error("Invalid acknowledgment");
      if (socket.deserializeAttachment()?.inflight !== ack.id) throw new Error("Unexpected acknowledgment");
      this.ctx.storage.sql.exec("DELETE FROM outbox WHERE id = ?", ack.id);
      socket.serializeAttachment({});
      socket.send(JSON.stringify({ type: "acked", id: ack.id }));
      this.sendNext(socket);
    } catch { socket.close(1008, "Invalid acknowledgment"); }
  }
  webSocketClose(socket: WebSocket, code: number, reason: string): void { socket.close(code === 1005 || code === 1006 ? 1000 : code, reason); }
  webSocketError(socket: WebSocket): void { socket.close(1011, "Reconnect for replay"); }
}

export default {
  async fetch(request: Request, env: Bindings): Promise<Response> {
    const url = new URL(request.url);
    if (url.pathname === "/health" && request.method === "GET") return Response.json({ ok: true, service: "codex-ci-webhook" });
    const config = configuration(env);
    if (url.pathname === "/webhooks/github" && request.method === "POST") {
      const raw = await boundedBody(request);
      if (!raw) return new Response("Payload too large", { status: 413 });
      if (!await signature(env.GITHUB_WEBHOOK_SECRET, request.headers.get("X-Hub-Signature-256"), raw)) return new Response("Invalid signature", { status: 401 });
      const delivery = request.headers.get("X-GitHub-Delivery");
      if (!delivery || delivery.length > 128) return new Response("Missing delivery id", { status: 400 });
      const kind = request.headers.get("X-GitHub-Event") ?? "";
      if (kind === "ping") return Response.json({ ok: true });
      let payload;
      try { payload = JSON.parse(new TextDecoder().decode(raw)); } catch { return new Response("Invalid JSON", { status: 400 }); }
      const repository = payload?.repository?.full_name;
      if (typeof repository !== "string" || !Object.hasOwn(config, repository)) return new Response("Repository not configured", { status: 403 });
      const event = normalize(kind, payload, config[repository]);
      if (!event) return Response.json({ ignored: true });
      const result = await env.REPOSITORIES.getByName(repository).ingest(repository, delivery, event);
      console.log(JSON.stringify({ event: kind, repository, branch: event.branch, sha: event.sha, ...result }));
      return Response.json(result);
    }
    if ((url.pathname === "/bridge" || url.pathname === "/pending") && request.method === "GET") {
      if (!authorized(request, env.BRIDGE_TOKEN)) return new Response("Unauthorized", { status: 401 });
      const repository = url.searchParams.get("repo") ?? "";
      if (!Object.hasOwn(config, repository)) return new Response("Repository not configured", { status: 404 });
      const object = env.REPOSITORIES.getByName(repository);
      if (url.pathname === "/pending") return Response.json(await object.pending());
      if (request.headers.get("Upgrade")?.toLowerCase() !== "websocket") return new Response("WebSocket required", { status: 426 });
      // WebSocket upgrades must cross the fetch transport; RPC serializes Responses.
      return object.fetch(request);
    }
    return new Response("Not found", { status: 404 });
  }
};
