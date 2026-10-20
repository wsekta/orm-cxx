import { DurableObject } from "cloudflare:workers";
import { timingSafeEqual } from "node:crypto";
import { normalize, reduce, type Commit, type Event, type Notice } from "./state";

type Bindings = Env & { GITHUB_WEBHOOK_SECRET: string; BRIDGE_TOKEN: string };
type Configuration = Record<string, { branch: string; workflows: string[] }>;
const encoder = new TextEncoder();
const limit = 2 * 1024 * 1024;
function configuration(env: Bindings): Configuration { return JSON.parse(env.REPOSITORY_CONFIG); }

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
      CREATE TABLE IF NOT EXISTS outbox (seq INTEGER PRIMARY KEY AUTOINCREMENT, id TEXT UNIQUE NOT NULL, data TEXT NOT NULL);`);
  }
  ingest(repository: string, delivery: string, event: Event): { duplicate: boolean; notified: boolean } {
    let notice: Notice | null = null;
    let duplicate = false;
    this.ctx.storage.transactionSync(() => {
      if (this.ctx.storage.sql.exec("SELECT id FROM deliveries WHERE id = ?", delivery).toArray().length) { duplicate = true; return; }
      const stored = this.ctx.storage.sql.exec<{ data: string }>("SELECT data FROM commits WHERE sha = ?", event.sha).toArray()[0];
      // A job from an unrelated PR/fork must not start monitoring a new commit.
      if (event.type === "job" && !stored) return;
      const state: Commit = stored ? JSON.parse(stored.data) : { sha: event.sha, runs: {} };
      notice = reduce(state, event, configuration(this.env)[repository].workflows, repository);
      this.ctx.storage.sql.exec("INSERT INTO deliveries VALUES (?, ?)", delivery, Date.now());
      this.ctx.storage.sql.exec("INSERT OR REPLACE INTO commits VALUES (?, ?, ?)", state.sha, JSON.stringify(state), Date.now());
      if (notice) this.ctx.storage.sql.exec("INSERT OR IGNORE INTO outbox (id, data) VALUES (?, ?)", notice.id, JSON.stringify(notice));
      const cutoff = Date.now() - 30 * 86400000;
      this.ctx.storage.sql.exec("DELETE FROM deliveries WHERE received < ?", cutoff);
      this.ctx.storage.sql.exec("DELETE FROM commits WHERE updated < ? AND NOT EXISTS (SELECT 1 FROM outbox WHERE json_extract(outbox.data, '$.sha') = commits.sha)", cutoff);
    });
    if (notice) for (const socket of this.ctx.getWebSockets()) this.sendNext(socket);
    return { duplicate, notified: notice !== null };
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
      const event = normalize(kind, payload, config[repository].branch);
      if (!event) return Response.json({ ignored: true });
      const result = await env.REPOSITORIES.getByName(repository).ingest(repository, delivery, event);
      console.log(JSON.stringify({ event: kind, repository, sha: event.sha, ...result }));
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
