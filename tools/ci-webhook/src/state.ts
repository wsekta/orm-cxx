export type Run = {
  id: number; attempt: number; status: string; conclusion: string | null;
  url: string; failedJob?: string;
};
export type Commit = { sha: string; runs: Record<string, Run>; failureSent?: boolean; successSent?: string };
export type Event = {
  type: "push" | "run" | "job"; sha: string; workflow?: string;
  run?: Run; job?: string;
};
export type Notice = {
  id: string; repository: string; sha: string; outcome: "failure" | "success";
  workflow?: string; job?: string; url: string; runs: Record<string, Run>;
};

const bad = new Set(["failure", "timed_out", "cancelled", "action_required", "startup_failure", "stale"]);
export function reduce(state: Commit, event: Event, expected: string[], repository: string): Notice | null {
  if (event.run && event.workflow && expected.includes(event.workflow)) {
    const prior = state.runs[event.workflow];
    const next = event.run;
    if (prior && (next.id < prior.id || (next.id === prior.id && next.attempt < prior.attempt))) return null;
    const same = prior && next.id === prior.id && next.attempt === prior.attempt;
    if (same && prior.status === "completed" && (event.type === "job" || next.status !== "completed")) return null;
    if (prior && !same) state.failureSent = false;
    if (event.type === "job") {
      state.runs[event.workflow] = { ...(same ? prior : next), failedJob: event.job, conclusion: next.conclusion };
    } else {
      state.runs[event.workflow] = { ...next,
        conclusion: same && prior.failedJob && next.status !== "completed" ? prior.conclusion : next.conclusion,
        failedJob: same && next.conclusion !== "success" ? prior.failedJob : undefined };
    }
  }
  const failing = Object.entries(state.runs).find(([, r]) => bad.has(r.conclusion ?? ""));
  if (failing && !state.failureSent) {
    state.failureSent = true;
    const [workflow, run] = failing;
    return { id: `${state.sha}:failure:${run.id}:${run.attempt}`, repository, sha: state.sha,
      outcome: "failure", workflow, job: run.failedJob, url: run.url, runs: structuredClone(state.runs) };
  }
  if (!expected.length || !expected.every(name => state.runs[name]?.status === "completed" && state.runs[name]?.conclusion === "success")) return null;
  const fingerprint = expected.map(name => `${state.runs[name].id}:${state.runs[name].attempt}`).join("-");
  if (state.successSent === fingerprint) return null;
  state.successSent = fingerprint;
  return { id: `${state.sha}:success:${fingerprint}`, repository, sha: state.sha,
    outcome: "success", url: `https://github.com/${repository}/actions?query=sha%3A${state.sha}`, runs: structuredClone(state.runs) };
}

function record(value: unknown): Record<string, unknown> {
  return value && typeof value === "object" ? value as Record<string, unknown> : {};
}
export function normalize(kind: string, value: unknown, branch: string): Event | null {
  const body = record(value);
  if (kind === "push") {
    if (body.ref !== `refs/heads/${branch}` || body.deleted || !/^[a-f0-9]{40}$/.test(String(body.after))) return null;
    return { type: "push", sha: String(body.after) };
  }
  const item = record(body[kind]);
  if (item.head_branch !== branch || !/^[a-f0-9]{40}$/.test(String(item.head_sha))) return null;
  if (kind === "workflow_run" && item.event !== "push") return null;
  if (kind !== "workflow_run" && kind !== "workflow_job") return null;
  if (!Number.isSafeInteger(item.run_attempt) || Number(item.run_attempt) < 1) return null;
  const id = kind === "workflow_run" ? item.id : item.run_id;
  if (!Number.isSafeInteger(id) || Number(id) < 1) return null;
  const workflow = kind === "workflow_run" ? item.name : item.workflow_name;
  if (typeof workflow !== "string") return null;
  if (kind === "workflow_job" && (body.action !== "completed" || !bad.has(String(item.conclusion)))) return null;
  return { type: kind === "workflow_run" ? "run" : "job", sha: String(item.head_sha), workflow,
    job: kind === "workflow_job" ? String(item.name).slice(0, 200) : undefined,
    run: { id: Number(id), attempt: Number(item.run_attempt), status: kind === "workflow_job" ? "in_progress" : String(item.status),
      conclusion: typeof item.conclusion === "string" ? item.conclusion : null,
      url: String(item.html_url) } };
}
