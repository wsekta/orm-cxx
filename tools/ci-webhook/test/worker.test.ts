import { beforeEach, describe, expect, it } from "vitest";
import { env } from "cloudflare:workers";
import { evictDurableObject, runInDurableObject } from "cloudflare:test";
import worker from "../src/index";
const bindings = { ...env, GITHUB_WEBHOOK_SECRET: "test-webhook-secret", BRIDGE_TOKEN: "test-bridge-secret" };
const sha = "b".repeat(40), repository = "wsekta/orm-cxx";
const readPending = () => worker.fetch(new Request(`https://test/pending?repo=${repository}`, { headers: { Authorization: `Bearer ${bindings.BRIDGE_TOKEN}` } }), bindings);
async function deliver(kind: string, payload: unknown, delivery = crypto.randomUUID(), secret = bindings.GITHUB_WEBHOOK_SECRET) {
  const body = JSON.stringify(payload);
  const key = await crypto.subtle.importKey("raw", new TextEncoder().encode(secret), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  const digest = await crypto.subtle.sign("HMAC", key, new TextEncoder().encode(body));
  const signature = "sha256=" + Array.from(new Uint8Array(digest), n => n.toString(16).padStart(2, "0")).join("");
  return worker.fetch(new Request("https://test/webhooks/github", { method: "POST", body, headers: { "X-Hub-Signature-256": signature, "X-GitHub-Event": kind, "X-GitHub-Delivery": delivery } }), bindings);
}
const payload = (name: string, id: number, conclusion = "success") => ({ repository: { full_name: repository }, workflow_run: {
  event: "push", head_sha: sha, head_branch: "main", run_attempt: 1, id, name, status: "completed", conclusion, html_url: `https://github.com/${repository}/actions/runs/${id}`
} });
beforeEach(async () => {
  await runInDurableObject(env.REPOSITORIES.getByName(repository), async (_, state) => {
    state.storage.sql.exec("DELETE FROM deliveries; DELETE FROM commits; DELETE FROM outbox;");
  });
});
describe("signed webhook, SQLite outbox and WebSocket delivery", () => {
  it("rejects bad signatures, missing delivery ids, oversized bodies and unauthenticated clients", async () => {
    expect((await deliver("push", {}, crypto.randomUUID(), "wrong")).status).toBe(401);
    expect((await deliver("push", {}, "")).status).toBe(400);
    expect((await worker.fetch(new Request("https://test/webhooks/github", { method: "POST", body: "x", headers: { "Content-Length": "3000000" } }), bindings)).status).toBe(413);
    expect((await worker.fetch(new Request(`https://test/bridge?repo=${repository}`), bindings)).status).toBe(401);
    expect((await worker.fetch(new Request(`https://test/pending?repo=${repository}`), bindings)).status).toBe(401);
    expect((await readPending()).status).toBe(200);
  });
  it("persists events before delivery, deduplicates, replays offline and removes only acknowledged results", async () => {
    const workflows = ["GCC", "MSVC", "Quality", "Backend conformance", "Clang", "Packages", "Docs"];
    for (let i = 0; i < workflows.length - 1; i++) expect((await deliver("workflow_run", payload(workflows[i], i + 1))).status).toBe(200);
    expect(await (await readPending()).json()).toEqual([]);
    const last = payload("Docs", 7), delivery = crypto.randomUUID();
    expect(await (await deliver("workflow_run", last, delivery)).json()).toMatchObject({ notified: true });
    expect(await (await deliver("workflow_run", last, delivery)).json()).toMatchObject({ duplicate: true });
    const pending = await (await readPending()).json() as { id: string }[];
    expect(pending).toHaveLength(1);
    await evictDurableObject(env.REPOSITORIES.getByName(repository));
    expect(await (await readPending()).json()).toEqual(pending);
    await runInDurableObject(env.REPOSITORIES.getByName(repository), async (_, state) => {
      expect(state.storage.sql.exec("SELECT * FROM outbox").toArray()).toHaveLength(1);
      expect(state.storage.sql.exec("SELECT * FROM commits").toArray()).toHaveLength(1);
    });
    const response = await worker.fetch(new Request(`https://test/bridge?repo=${repository}`, { headers: { Authorization: `Bearer ${bindings.BRIDGE_TOKEN}`, Upgrade: "websocket" } }), bindings);
    const socket = response.webSocket!;
    socket.accept();
    const next = new Promise<string>(resolve => socket.addEventListener("message", event => resolve(String(event.data)), { once: true }));
    expect(JSON.parse(await next)).toMatchObject({ id: pending[0].id, outcome: "success", sha });
    await evictDurableObject(env.REPOSITORIES.getByName(repository));
    const acknowledged = new Promise<string>(resolve => socket.addEventListener("message", event => resolve(String(event.data)), { once: true }));
    socket.send(JSON.stringify({ type: "ack", id: pending[0].id }));
    expect(JSON.parse(await acknowledged)).toEqual({ type: "acked", id: pending[0].id });
    expect(await (await readPending()).json()).toEqual([]);
    socket.close();
  });
  it("reports a failed matrix job immediately while the run is still active", async () => {
    await deliver("push", { repository: { full_name: repository }, ref: "refs/heads/main", after: sha });
    await deliver("workflow_job", { repository: { full_name: repository }, action: "completed", workflow_job: {
      head_sha: sha, head_branch: "main", run_id: 9, run_attempt: 1, workflow_name: "Packages", name: "conan/windows/sqlite", status: "completed", conclusion: "failure", html_url: `https://github.com/${repository}/actions/runs/9/job/2`
    } });
    expect(await (await readPending()).json()).toMatchObject([{ outcome: "failure", workflow: "Packages", job: "conan/windows/sqlite" }]);
  });
  it("ignores unconfigured repositories and unrelated fork jobs", async () => {
    expect((await deliver("push", { repository: { full_name: "other/repo" }, ref: "refs/heads/main", after: sha })).status).toBe(403);
    await deliver("workflow_job", { repository: { full_name: repository }, action: "completed", workflow_job: {
      head_sha: sha, head_branch: "main", run_id: 9, run_attempt: 1, workflow_name: "Packages", name: "fork", status: "completed", conclusion: "failure"
    } });
    expect(await (await readPending()).json()).toEqual([]);
  });
});
