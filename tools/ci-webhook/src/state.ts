import { createHash } from "node:crypto";

export type RepositoryPolicy = {
  branches: "*" | string[]; excludedBranches: string[]; workflows: string[];
  workflowsByBranch?: Record<string, string[]>;
};
export type Run = {
  id: number; attempt: number; status: string; conclusion: string | null;
  url: string; failedJob?: string;
};
export type Commit = { branch: string; sha: string; runs: Record<string, Run>; failureSent?: boolean; successSent?: string };
export type Event = {
  type: "push" | "run" | "job"; branch: string; sha: string; workflow?: string;
  run?: Run; job?: string;
};
export type Notice = {
  id: string; repository: string; branch: string; sha: string; outcome: "failure" | "success";
  workflow?: string; job?: string; url: string; runs: Record<string, Run>;
};

const bad = new Set(["failure", "timed_out", "cancelled", "action_required", "startup_failure", "stale"]);

export function validBranch(value: unknown): value is string {
  return typeof value === "string" && value.length > 0 && value.length <= 1024
    && value !== "@" && !value.startsWith("-") && !value.startsWith("/")
    && !value.endsWith("/") && !value.endsWith(".")
    && !value.includes("..") && !value.includes("//") && !value.includes("@{")
    && !/[\x00-\x20\x7f~^:?*\[\\]/.test(value)
    && value.split("/").every(component => component.length > 0 && !component.startsWith(".") && !component.endsWith(".lock"));
}

export function workflowsForBranch(policy: RepositoryPolicy, branch: string): string[] | null {
  if (!validBranch(branch) || policy.excludedBranches.includes(branch)
    || (policy.branches !== "*" && !policy.branches.includes(branch))) return null;
  const overrides = policy.workflowsByBranch;
  return overrides && Object.hasOwn(overrides, branch) ? overrides[branch] : policy.workflows;
}

export function branchHash(branch: string): string { return createHash("sha256").update(branch).digest("hex"); }

export function reduce(state: Commit, event: Event, expected: string[], repository: string): Notice | null {
  if (state.sha !== event.sha || state.branch !== event.branch) return null;
  if (event.run && event.workflow && expected.includes(event.workflow)) {
    const prior = state.runs[event.workflow];
    const next = event.run;
    if (prior && (next.id < prior.id || (next.id === prior.id && next.attempt < prior.attempt))) return null;
    const same = prior && next.id === prior.id && next.attempt === prior.attempt;
    // Job payloads do not identify their trigger. Confirm this exact run with a
    // push workflow_run before allowing its failed jobs to change push state.
    if (event.type === "job" && !same) return null;
    if (same && prior.status === "completed" && (event.type === "job" || next.status !== "completed")) return null;
    if (prior && !same) state.failureSent = false;
    if (event.type === "job") {
      state.runs[event.workflow] = { ...prior!, failedJob: event.job, conclusion: next.conclusion, url: next.url };
    } else {
      state.runs[event.workflow] = { ...next,
        conclusion: same && prior.failedJob && next.status !== "completed" ? prior.conclusion : next.conclusion,
        failedJob: next.conclusion === "success" ? undefined : next.failedJob ?? (same ? prior.failedJob : undefined) };
    }
  }
  const prefix = `${state.sha}:branch:${branchHash(state.branch)}`;
  const failing = Object.entries(state.runs).find(([, r]) => bad.has(r.conclusion ?? ""));
  if (failing && !state.failureSent) {
    state.failureSent = true;
    const [workflow, run] = failing;
    return { id: `${prefix}:failure:${run.id}:${run.attempt}`, repository, branch: state.branch, sha: state.sha,
      outcome: "failure", workflow, job: run.failedJob, url: run.url, runs: structuredClone(state.runs) };
  }
  if (!expected.length || !expected.every(name => state.runs[name]?.status === "completed" && state.runs[name]?.conclusion === "success")) return null;
  const fingerprint = expected.map(name => `${state.runs[name].id}:${state.runs[name].attempt}`).join("-");
  if (state.successSent === fingerprint) return null;
  state.successSent = fingerprint;
  return { id: `${prefix}:success:${fingerprint}`, repository, branch: state.branch, sha: state.sha,
    outcome: "success", url: `https://github.com/${repository}/actions?query=${encodeURIComponent(`branch:${state.branch} event:push sha:${state.sha}`)}`, runs: structuredClone(state.runs) };
}

function record(value: unknown): Record<string, unknown> {
  return value && typeof value === "object" ? value as Record<string, unknown> : {};
}
function runUrl(value: unknown, repository: unknown, id: number, job: boolean): value is string {
  if (typeof value !== "string" || typeof repository !== "string" || !/^[\w.-]+\/[\w.-]+$/.test(repository)) return false;
  try {
    const url = new URL(value);
    return url.protocol === "https:" && url.hostname === "github.com" && !url.username && !url.password
      && !url.port && !url.search && !url.hash
      && (job ? new RegExp(`^/${repository.replaceAll(".", "\\.")}/actions/runs/${id}/job/[0-9]+$`).test(url.pathname)
        : url.pathname === `/${repository}/actions/runs/${id}`);
  } catch { return false; }
}

export function normalize(kind: string, value: unknown, policy: RepositoryPolicy): Event | null {
  const body = record(value);
  if (kind === "push") {
    if (typeof body.ref !== "string" || !body.ref.startsWith("refs/heads/") || body.deleted || !/^[a-f0-9]{40}$/.test(String(body.after))) return null;
    const branch = body.ref.slice("refs/heads/".length);
    if (!workflowsForBranch(policy, branch)) return null;
    return { type: "push", branch, sha: String(body.after) };
  }
  if (kind !== "workflow_run" && kind !== "workflow_job") return null;
  const item = record(body[kind]);
  const branch = item.head_branch;
  if (!validBranch(branch) || !/^[a-f0-9]{40}$/.test(String(item.head_sha))) return null;
  const expected = workflowsForBranch(policy, branch);
  if (!expected || (kind === "workflow_run" && item.event !== "push")) return null;
  if (!Number.isSafeInteger(item.run_attempt) || Number(item.run_attempt) < 1) return null;
  const id = kind === "workflow_run" ? item.id : item.run_id;
  if (!Number.isSafeInteger(id) || Number(id) < 1) return null;
  const workflow = kind === "workflow_run" ? item.name : item.workflow_name;
  if (typeof workflow !== "string" || !expected.includes(workflow)) return null;
  const job = kind === "workflow_job";
  if (job && (body.action !== "completed" || item.status !== "completed" || !bad.has(String(item.conclusion)) || typeof item.name !== "string")) return null;
  if (typeof item.status !== "string" || item.status.length > 40 || !runUrl(item.html_url, record(body.repository).full_name, Number(id), job)) return null;
  return { type: job ? "job" : "run", branch, sha: String(item.head_sha), workflow,
    job: job ? String(item.name).slice(0, 200) : undefined,
    run: { id: Number(id), attempt: Number(item.run_attempt), status: job ? "in_progress" : item.status,
      conclusion: typeof item.conclusion === "string" ? item.conclusion : null,
      url: item.html_url } };
}
