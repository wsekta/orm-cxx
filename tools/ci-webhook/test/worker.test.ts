import { beforeEach, describe, expect, it } from "vitest";
import { env } from "cloudflare:workers";
import { evictDurableObject, runInDurableObject } from "cloudflare:test";
import worker, { migrateLegacyCommits } from "../src/index";
const bindings = { ...env, GITHUB_WEBHOOK_SECRET: "test-webhook-secret", BRIDGE_TOKEN: "test-bridge-secret" };
const sha = "b".repeat(40), repository = "wsekta/orm-cxx";
const featureWorkflows = ["GCC", "MSVC", "Quality", "Backend conformance", "Clang", "Packages"];
const readPending = () => worker.fetch(new Request(`https://test/pending?repo=${repository}`, { headers: { Authorization: `Bearer ${bindings.BRIDGE_TOKEN}` } }), bindings);
async function deliver(kind: string, payload: unknown, delivery = crypto.randomUUID(), secret = bindings.GITHUB_WEBHOOK_SECRET) {
  const body = JSON.stringify(payload);
  const key = await crypto.subtle.importKey("raw", new TextEncoder().encode(secret), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  const digest = await crypto.subtle.sign("HMAC", key, new TextEncoder().encode(body));
  const signature = "sha256=" + Array.from(new Uint8Array(digest), n => n.toString(16).padStart(2, "0")).join("");
  return worker.fetch(new Request("https://test/webhooks/github", { method: "POST", body, headers: { "X-Hub-Signature-256": signature, "X-GitHub-Event": kind, "X-GitHub-Delivery": delivery } }), bindings);
}
const payload = (name: string, id: number, conclusion: string | null = "success", branch = "main", attempt = 1) => ({ repository: { full_name: repository }, workflow_run: {
  event: "push", head_sha: sha, head_branch: branch, run_attempt: attempt, id, name, status: conclusion ? "completed" : "in_progress", conclusion, html_url: `https://github.com/${repository}/actions/runs/${id}`
} });
const push = (branch = "main", head = sha) => ({ repository: { full_name: repository }, ref: `refs/heads/${branch}`, after: head });
const failedJob = (branch: string, id: number, attempt = 1, name = "conan/windows/sqlite") => ({ repository: { full_name: repository }, action: "completed", workflow_job: {
  head_sha: sha, head_branch: branch, run_id: id, run_attempt: attempt, workflow_name: "Packages", name,
  status: "completed", conclusion: "failure", html_url: `https://github.com/${repository}/actions/runs/${id}/job/2`
} });
beforeEach(async () => {
  await runInDurableObject(env.REPOSITORIES.getByName(repository), async (_, state) => {
    state.storage.sql.exec("DELETE FROM deliveries; DELETE FROM commits; DELETE FROM commits_by_branch; DELETE FROM pending_jobs; DELETE FROM outbox;");
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
      expect(state.storage.sql.exec("SELECT * FROM commits_by_branch").toArray()).toHaveLength(1);
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
    await deliver("push", push());
    await deliver("workflow_run", payload("Packages", 9, null));
    await deliver("workflow_job", failedJob("main", 9));
    expect(await (await readPending()).json()).toMatchObject([{ branch: "main", outcome: "failure", workflow: "Packages", job: "conan/windows/sqlite", url: `https://github.com/${repository}/actions/runs/9/job/2` }]);
  });
  it("ignores unconfigured repositories and unrelated fork jobs", async () => {
    expect((await deliver("push", { repository: { full_name: "other/repo" }, ref: "refs/heads/main", after: sha })).status).toBe(403);
    await deliver("workflow_job", failedJob("main", 9, 1, "fork"));
    expect(await (await readPending()).json()).toEqual([]);
  });
  it("separates the same SHA on feature branches and requires Docs only on main", async () => {
    const one = "feature/one", two = "feature/two";
    for (let i = 0; i < featureWorkflows.length; i++) {
      await deliver("workflow_run", payload(featureWorkflows[i], i + 1, "success", i < 3 ? one : two));
    }
    expect(await (await readPending()).json()).toEqual([]);
    for (let i = 3; i < featureWorkflows.length; i++) await deliver("workflow_run", payload(featureWorkflows[i], i + 1, "success", one));
    expect(await (await readPending()).json()).toMatchObject([{ branch: one, sha, outcome: "success" }]);
    for (let i = 0; i < 3; i++) await deliver("workflow_run", payload(featureWorkflows[i], i + 1, "success", two));
    const features = await (await readPending()).json() as { id: string; branch: string }[];
    expect(features).toHaveLength(2);
    expect(new Set(features.map(notice => notice.id)).size).toBe(2);
    for (let i = 0; i < featureWorkflows.length; i++) await deliver("workflow_run", payload(featureWorkflows[i], i + 1));
    expect(await (await readPending()).json()).toHaveLength(2);
    await deliver("workflow_run", payload("Docs", 7));
    expect(await (await readPending()).json()).toMatchObject([{ branch: one }, { branch: two }, { branch: "main" }]);
  });
  it("ignores the publication branch and non-push runs before writing commit state", async () => {
    expect(await (await deliver("push", push("gh-pages"))).json()).toEqual({ ignored: true });
    const pr = payload("Packages", 11, "failure", "feature/one");
    pr.workflow_run.event = "pull_request";
    expect(await (await deliver("workflow_run", pr)).json()).toEqual({ ignored: true });
    await runInDurableObject(env.REPOSITORIES.getByName(repository), async (_, state) => {
      expect(state.storage.sql.exec("SELECT * FROM commits_by_branch").toArray()).toHaveLength(0);
    });
  });
  it("persists an early failed job until its exact push run is confirmed", async () => {
    const branch = "feature/early-job";
    await deliver("push", push(branch));
    const job = failedJob(branch, 9);
    const delivery = crypto.randomUUID();
    expect(await (await deliver("workflow_job", job, delivery)).json()).toEqual({ duplicate: false, notified: false });
    expect(await (await deliver("workflow_job", job, delivery)).json()).toMatchObject({ duplicate: true });
    await evictDurableObject(env.REPOSITORIES.getByName(repository));
    expect(await (await readPending()).json()).toEqual([]);
    await deliver("workflow_run", payload("Packages", 8, null, branch));
    expect(await (await readPending()).json()).toEqual([]);
    await deliver("workflow_run", payload("Packages", 9, null, branch));
    expect(await (await readPending()).json()).toMatchObject([{ branch, workflow: "Packages", outcome: "failure", job: "conan/windows/sqlite" }]);
    await runInDurableObject(env.REPOSITORIES.getByName(repository), async (_, state) => {
      expect(state.storage.sql.exec("SELECT * FROM pending_jobs").toArray()).toHaveLength(0);
    });
  });
  it("does not let a failed PR job poison a push run for the same branch and SHA", async () => {
    const branch = "feature/with-pr";
    await deliver("workflow_run", payload("Packages", 9, null, branch));
    await deliver("workflow_job", failedJob(branch, 10, 1, "PR-only failure"));
    const pr = payload("Packages", 10, "failure", branch);
    pr.workflow_run.event = "pull_request";
    await deliver("workflow_run", pr);
    await deliver("workflow_run", payload("Packages", 9, "success", branch));
    expect(await (await readPending()).json()).toEqual([]);
    await runInDurableObject(env.REPOSITORIES.getByName(repository), async (_, state) => {
      const row = state.storage.sql.exec<{ data: string }>("SELECT data FROM commits_by_branch WHERE branch = ? AND sha = ?", branch, sha).one();
      expect(JSON.parse(row.data).runs.Packages).toMatchObject({ id: 9, status: "completed", conclusion: "success" });
      expect(state.storage.sql.exec("SELECT * FROM pending_jobs").toArray()).toHaveLength(1);
    });
  });
  it("promotes only the matching attempt and preserves a buffered job in a completed failure", async () => {
    const branch = "feature/retry";
    await deliver("push", push(branch));
    await deliver("workflow_job", failedJob(branch, 9, 2));
    await deliver("workflow_run", payload("Packages", 9, null, branch, 1));
    expect(await (await readPending()).json()).toEqual([]);
    await deliver("workflow_run", payload("Packages", 9, "failure", branch, 2));
    expect(await (await readPending()).json()).toMatchObject([{ branch, outcome: "failure", job: "conan/windows/sqlite", url: `https://github.com/${repository}/actions/runs/9/job/2` }]);
  });
  it("discards a buffered failed job when its completed push run is successful", async () => {
    const branch = "feature/authoritative-result";
    await deliver("push", push(branch));
    await deliver("workflow_job", failedJob(branch, 9));
    await deliver("workflow_run", payload("Packages", 9, "success", branch));
    expect(await (await readPending()).json()).toEqual([]);
    await runInDurableObject(env.REPOSITORIES.getByName(repository), async (_, state) => {
      expect(state.storage.sql.exec("SELECT * FROM pending_jobs").toArray()).toHaveLength(0);
    });
  });
  it("migrates legacy main state once, maps a bootstrap feature explicitly, and preserves queued legacy IDs", async () => {
    const oldMainSha = "c".repeat(40), feature = "codex/ci-webhook-feature-branches";
    const legacyNotice = { id: `${oldMainSha}:success:legacy`, repository, sha: oldMainSha, outcome: "success", url: `https://github.com/${repository}/actions`, runs: {} };
    await runInDurableObject(env.REPOSITORIES.getByName(repository), async (_, state) => {
      state.storage.sql.exec("DELETE FROM schema_migrations WHERE name = ?", "branch-commits-v1");
      state.storage.sql.exec("INSERT INTO commits VALUES (?, ?, ?)", oldMainSha, JSON.stringify({ sha: oldMainSha, runs: {}, successSent: "already-sent", failureSent: true }), Date.now());
      state.storage.sql.exec("INSERT INTO commits VALUES (?, ?, ?)", sha, JSON.stringify({ sha, runs: {}, successSent: "bootstrap-sent" }), Date.now());
      state.storage.sql.exec("INSERT INTO outbox (id, data) VALUES (?, ?)", legacyNotice.id, JSON.stringify(legacyNotice));
      migrateLegacyCommits(state.storage, JSON.stringify({ [sha]: feature }));
      const rows = state.storage.sql.exec<{ branch: string; sha: string; data: string }>("SELECT branch, sha, data FROM commits_by_branch ORDER BY branch").toArray();
      expect(rows).toHaveLength(2);
      expect(rows.find(row => row.sha === oldMainSha)?.branch).toBe("main");
      expect(JSON.parse(rows.find(row => row.sha === oldMainSha)!.data)).toMatchObject({ branch: "main", successSent: "already-sent", failureSent: true });
      expect(rows.find(row => row.sha === sha)?.branch).toBe(feature);
      expect(state.storage.sql.exec("SELECT * FROM commits_by_branch WHERE branch = 'main' AND sha = ?", sha).toArray()).toHaveLength(0);
      state.storage.sql.exec("DELETE FROM commits_by_branch WHERE branch = 'main' AND sha = ?", oldMainSha);
    });
    await evictDurableObject(env.REPOSITORIES.getByName(repository));
    expect(await (await readPending()).json()).toEqual([legacyNotice]);
    await runInDurableObject(env.REPOSITORIES.getByName(repository), async (_, state) => {
      expect(state.storage.sql.exec("SELECT * FROM commits_by_branch WHERE branch = 'main' AND sha = ?", oldMainSha).toArray()).toHaveLength(0);
      expect(state.storage.sql.exec("SELECT * FROM commits_by_branch WHERE branch = ? AND sha = ?", feature, sha).toArray()).toHaveLength(1);
      expect(state.storage.sql.exec("SELECT * FROM schema_migrations WHERE name = ?", "branch-commits-v1").toArray()).toHaveLength(1);
    });
  });
  it("expires orphan pending jobs and protects only the matching branch of an unacknowledged legacy notice", async () => {
    const branch = "feature/expired", old = Date.now() - 31 * 86400000;
    await runInDurableObject(env.REPOSITORIES.getByName(repository), async (_, state) => {
      for (const name of ["main", branch]) state.storage.sql.exec("INSERT INTO commits_by_branch VALUES (?, ?, ?, ?)", name, sha, JSON.stringify({ branch: name, sha, runs: {} }), old);
      const job = failedJob(branch, 9);
      state.storage.sql.exec("INSERT INTO pending_jobs VALUES (?, ?, ?, ?, ?, ?, ?)", branch, sha, "Packages", 9, 1, JSON.stringify(job), old);
      state.storage.sql.exec("INSERT INTO pending_jobs VALUES (?, ?, ?, ?, ?, ?, ?)", "feature/orphan", "d".repeat(40), "Packages", 10, 1, JSON.stringify(job), Date.now());
      state.storage.sql.exec("INSERT INTO outbox (id, data) VALUES (?, ?)", `${sha}:success:legacy`, JSON.stringify({ id: `${sha}:success:legacy`, sha }));
    });
    await deliver("push", push("feature/new", "e".repeat(40)));
    await runInDurableObject(env.REPOSITORIES.getByName(repository), async (_, state) => {
      expect(state.storage.sql.exec("SELECT branch FROM commits_by_branch WHERE sha = ?", sha).toArray()).toEqual([{ branch: "main" }]);
      expect(state.storage.sql.exec("SELECT * FROM pending_jobs").toArray()).toHaveLength(0);
    });
  });
});
