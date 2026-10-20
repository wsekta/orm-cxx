import { describe, expect, it } from "vitest";
import { normalize, reduce, type Commit, type Event, type Run } from "../src/state";
const sha = "a".repeat(40);
const run = (id: number, conclusion: string | null = "success", attempt = 1): Run => ({ id, attempt, status: conclusion ? "completed" : "in_progress", conclusion, url: `https://github.com/o/r/actions/runs/${id}` });
const event = (workflow: string, value: Run): Event => ({ type: "run", sha, workflow, run: value });
const state = (): Commit => ({ sha, runs: {} });

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
    expect(reduce(s, { type: "job", sha, workflow: "A", job: "matrix/GCC", run: run(1, conclusion) }, ["A", "B"], "o/r")?.job).toBe("matrix/GCC");
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
    expect(normalize("push", { ref: "refs/heads/main", after: sha }, "main")?.sha).toBe(sha);
    expect(normalize("push", { ref: "refs/heads/main", after: sha, deleted: true }, "main")).toBeNull();
    expect(normalize("push", { ref: "refs/heads/other", after: sha }, "main")).toBeNull();
    expect(normalize("workflow_run", { workflow_run: { head_branch: "main", head_sha: sha, event: "pull_request" } }, "main")).toBeNull();
    expect(normalize("workflow_run", null, "main")).toBeNull();
  });
});
