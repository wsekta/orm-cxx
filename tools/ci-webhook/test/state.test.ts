import { describe, expect, it } from "vitest";
import { branchHash, normalize, reduce, validBranch, workflowsForBranch, type Commit, type Event, type RepositoryPolicy, type Run } from "../src/state";
const sha = "a".repeat(40);
const branch = "main";
const policy: RepositoryPolicy = { branches: "*", excludedBranches: ["gh-pages"], workflows: ["A", "B"], workflowsByBranch: { main: ["A", "B", "Docs"] } };
const run = (id: number, conclusion: string | null = "success", attempt = 1): Run => ({ id, attempt, status: conclusion ? "completed" : "in_progress", conclusion, url: `https://github.com/o/r/actions/runs/${id}` });
const event = (workflow: string, value: Run, branchName = branch): Event => ({ type: "run", branch: branchName, sha, workflow, run: value });
const state = (branchName = branch): Commit => ({ branch: branchName, sha, runs: {} });

describe("CI aggregation", () => {
  it("waits for every required workflow and deduplicates success", () => {
    const s = state();
    expect(reduce(s, event("A", run(1)), ["A", "B"], "o/r")).toBeNull();
    expect(reduce(s, event("B", run(2, null)), ["A", "B"], "o/r")).toBeNull();
    expect(reduce(s, event("B", run(2)), ["A", "B"], "o/r")?.outcome).toBe("success");
    expect(reduce(s, event("A", run(1)), ["A", "B"], "o/r")).toBeNull();
  });
  it.each(["failure", "cancelled", "timed_out", "action_required", "startup_failure"])("reports %s before sibling jobs finish", conclusion => {
    const s = state();
    reduce(s, event("A", run(1, null)), ["A", "B"], "o/r");
    expect(reduce(s, { type: "job", branch, sha, workflow: "A", job: "matrix/GCC", run: run(1, conclusion) }, ["A", "B"], "o/r")?.job).toBe("matrix/GCC");
    expect(reduce(s, event("A", run(1, conclusion)), ["A", "B"], "o/r")).toBeNull();
  });
  it("handles retries and ignores older attempts and backwards progress", () => {
    const s = state();
    expect(reduce(s, event("A", run(1, "failure")), ["A"], "o/r")?.outcome).toBe("failure");
    expect(reduce(s, event("A", run(1, null, 2)), ["A"], "o/r")).toBeNull();
    expect(reduce(s, event("A", run(1, "failure", 1)), ["A"], "o/r")).toBeNull();
    expect(reduce(s, event("A", run(1, "success", 2)), ["A"], "o/r")?.outcome).toBe("success");
    expect(reduce(s, event("A", run(1, null, 2)), ["A"], "o/r")).toBeNull();
    expect(s.runs.A.conclusion).toBe("success");
  });
  it("keeps commits independent and does not accept skipped as success", () => {
    const a = state(), b = state();
    reduce(a, event("A", run(1)), ["A", "B"], "o/r");
    expect(reduce(b, event("B", run(2)), ["A", "B"], "o/r")).toBeNull();
    expect(reduce(a, event("B", run(2, "skipped")), ["A", "B"], "o/r")).toBeNull();
    expect(reduce(a, event("unconfigured", run(3)), ["A", "B"], "o/r")).toBeNull();
  });
  it("filters non-push runs, branches, deletions and malformed events", () => {
    expect(normalize("push", { ref: "refs/heads/main", after: sha }, policy)?.sha).toBe(sha);
    expect(normalize("push", { ref: "refs/heads/main", after: sha, deleted: true }, policy)).toBeNull();
    expect(normalize("push", { ref: "refs/heads/gh-pages", after: sha }, policy)).toBeNull();
    expect(normalize("push", { ref: "refs/tags/main", after: sha }, policy)).toBeNull();
    expect(normalize("workflow_run", { workflow_run: { head_branch: "main", head_sha: sha, event: "pull_request" } }, policy)).toBeNull();
    expect(normalize("workflow_run", null, policy)).toBeNull();
  });
  it("selects complete workflow sets per exact branch and never treats inherited names as overrides", () => {
    expect(workflowsForBranch(policy, "main")).toEqual(["A", "B", "Docs"]);
    expect(workflowsForBranch(policy, "feature/connection")).toEqual(["A", "B"]);
    expect(workflowsForBranch(policy, "constructor")).toEqual(["A", "B"]);
    expect(workflowsForBranch(policy, "gh-pages")).toBeNull();
    expect(workflowsForBranch({ ...policy, branches: ["main", "release"] }, "feature/connection")).toBeNull();
  });
  it("does not combine the same SHA across branches and uses separate notification identities", () => {
    const a = state("feature/one"), b = state("feature/two");
    reduce(a, event("A", run(1), a.branch), ["A", "B"], "o/r");
    expect(reduce(b, event("B", run(2), b.branch), ["A", "B"], "o/r")).toBeNull();
    expect(reduce(a, event("B", run(2), b.branch), ["A", "B"], "o/r")).toBeNull();
    const one = reduce(a, event("B", run(2), a.branch), ["A", "B"], "o/r")!;
    const two = reduce(b, event("A", run(1), b.branch), ["A", "B"], "o/r")!;
    expect(one.id).not.toBe(two.id);
    expect(one.id).toBe(`${sha}:branch:${branchHash(a.branch)}:success:1:1-2:1`);
    expect(one.branch).toBe(a.branch);
    expect(new URL(one.url).searchParams.get("query")).toContain(`branch:${a.branch} event:push sha:${sha}`);
  });
  it("cannot let a newer unconfirmed PR job replace a push run", () => {
    const s = state("feature/one");
    reduce(s, event("A", run(1, null), s.branch), ["A", "B"], "o/r");
    const before = structuredClone(s);
    expect(reduce(s, { type: "job", branch: s.branch, sha, workflow: "A", job: "PR job", run: run(2, "failure") }, ["A", "B"], "o/r")).toBeNull();
    expect(s).toEqual(before);
  });
  it("accepts Unicode refs but rejects unsafe or malformed branch names", () => {
    expect(validBranch("feature/połączenie")).toBe(true);
    for (const name of ["", "@", "-option", "/leading", "trailing/", "trailing.", "a..b", "a//b", "a@{b", "a b", "a\nb", "a:b", "a*b", "a[b", "a\\b", ".hidden", "a/.hidden", "a.lock", "a/" + "x".repeat(1024)]) {
      expect(validBranch(name), name).toBe(false);
    }
  });
  it("normalizes feature pushes before a PR exists and rejects unsafe workflow URLs and IDs", () => {
    expect(normalize("push", { ref: "refs/heads/feature/one", after: sha }, policy)).toEqual({ type: "push", branch: "feature/one", sha });
    const item = { head_branch: "feature/one", head_sha: sha, event: "push", run_attempt: 1, id: 7, name: "A", status: "completed", conclusion: "success", html_url: "https://github.com/o/r/actions/runs/7" };
    const payload = { repository: { full_name: "o/r" }, workflow_run: item };
    expect(normalize("workflow_run", payload, policy)).toMatchObject({ branch: "feature/one", sha, workflow: "A" });
    for (const html_url of ["https://evil.example/o/r/actions/runs/7", "https://github.com/o/r/actions/runs/8", "https://user:pass@github.com/o/r/actions/runs/7"]) {
      expect(normalize("workflow_run", { ...payload, workflow_run: { ...item, html_url } }, policy)).toBeNull();
    }
    expect(normalize("workflow_run", { ...payload, workflow_run: { ...item, id: Number.MAX_SAFE_INTEGER + 1 } }, policy)).toBeNull();
  });
});
