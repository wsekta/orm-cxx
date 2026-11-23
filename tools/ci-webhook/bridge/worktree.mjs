import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { realpathSync } from 'node:fs';
import { isAbsolute, resolve } from 'node:path';

export function parseOptions(argv, allowed, required = []) {
  const values = {};
  for (let index = 0; index < argv.length; index += 2) {
    const flag = argv[index], value = argv[index + 1];
    if (!flag?.startsWith('--') || !allowed.includes(flag.slice(2)) || Object.hasOwn(values, flag.slice(2)) ||
        typeof value !== 'string' || value.length === 0 || value.startsWith('--')) {
      throw new Error('Invalid or duplicate option; use the documented command syntax');
    }
    values[flag.slice(2)] = value;
  }
  for (const name of required) if (!Object.hasOwn(values, name)) throw new Error(`Missing --${name}`);
  return values;
}

function execute(file, args) {
  return execFileSync(file, args, { encoding: 'utf8', windowsHide: true, timeout: 30000, maxBuffer: 1024 * 1024 }).trim();
}

export function githubRepository(remote) {
  const scp = /^git@github\.com:([\w.-]+\/[\w.-]+?)(?:\.git)?$/.exec(remote);
  if (scp) return scp[1];
  let url;
  try { url = new URL(remote); } catch { throw new Error('Origin must identify the configured GitHub repository'); }
  if (url.hostname !== 'github.com' || url.port || url.search || url.hash ||
      !['https:', 'ssh:'].includes(url.protocol) ||
      (url.protocol === 'https:' && (url.username || url.password)) ||
      (url.protocol === 'ssh:' && (url.username !== 'git' || url.password))) {
    throw new Error('Origin must identify the configured GitHub repository');
  }
  const match = /^\/([\w.-]+\/[\w.-]+?)(?:\.git)?$/.exec(url.pathname);
  if (!match) throw new Error('Origin must identify the configured GitHub repository');
  return match[1];
}

export function inspectWorktree(path, expected, run = execute) {
  if (typeof path !== 'string' || !isAbsolute(path)) throw new Error('--worktree must be an absolute worktree root');
  // Native Windows resolution expands short (8.3) names that Git normalizes.
  const worktree = realpathSync.native(path);
  const git = (...args) => run('git', ['-C', worktree, ...args]).trim();
  const top = realpathSync.native(git('rev-parse', '--show-toplevel'));
  const comparable = value => process.platform === 'win32' ? value.toLowerCase() : value;
  if (comparable(resolve(top)) !== comparable(resolve(worktree))) {
    throw new Error(`--worktree must name the worktree root (requested=${JSON.stringify(worktree)}, git=${JSON.stringify(top)})`);
  }
  const repository = githubRepository(git('remote', 'get-url', 'origin'));
  let branch;
  try { branch = git('symbolic-ref', '--quiet', '--short', 'HEAD'); }
  catch { throw new Error('Detached HEAD cannot register a pre-push destination'); }
  const sha = git('rev-parse', 'HEAD');
  if (repository.toLowerCase() !== expected.repository.toLowerCase()) throw new Error('Origin does not match the configured repository');
  if (branch !== expected.branch) throw new Error('Worktree branch does not match --branch');
  if (sha !== expected.sha) throw new Error('Worktree HEAD does not match --sha; register the current full commit');
  return { repository: expected.repository, branch, sha, worktree };
}

export function verifyWaitingPush(notice, run = execute) {
  const fingerprint = /^([a-f0-9]{40}):(?:branch:([a-f0-9]{64}):)?(failure|success):([0-9]+:[0-9]+(?:-[0-9]+:[0-9]+)*)$/.exec(notice.id);
  const branch = notice.branch ?? 'main';
  const branchHash = createHash('sha256').update(branch).digest('hex');
  if (!fingerprint || fingerprint[1] !== notice.sha || fingerprint[3] !== notice.outcome ||
      (Object.hasOwn(notice, 'branch') ? fingerprint[2] !== branchHash : fingerprint[2] !== undefined)) {
    throw new Error('Waiting event does not identify exact workflow attempts');
  }
  const pairs = fingerprint[4].split('-').map(value => value.split(':').map(Number));
  if (pairs.length > 100 || pairs.some(pair => pair.some(value => !Number.isSafeInteger(value) || value < 1)) ||
      new Set(pairs.map(value => value.join(':'))).size !== pairs.length ||
      (notice.outcome === 'failure' && pairs.length !== 1)) throw new Error('Invalid workflow-attempt fingerprint');
  const linkedRun = /\/actions\/runs\/([0-9]+)(?:\/|$)/.exec(new URL(notice.url).pathname)?.[1];
  if (linkedRun && !pairs.some(([id]) => String(id) === linkedRun)) throw new Error('Waiting event URL disagrees with its workflow fingerprint');
  const verified = pairs.map(([id, attempt]) => {
    const namedRun = notice.runs && typeof notice.runs === 'object' && !Array.isArray(notice.runs)
      ? Object.entries(notice.runs).find(([, value]) => value?.id === id && value.attempt === attempt) : undefined;
    if (notice.runs && !namedRun) throw new Error('Waiting event state disagrees with its workflow fingerprint');
    if (notice.outcome === 'failure' && notice.workflow && namedRun && namedRun[0] !== notice.workflow) {
      throw new Error('Waiting event workflow disagrees with its failure fingerprint');
    }
    const expectedWorkflow = namedRun?.[0] ?? notice.workflow;
    const actual = JSON.parse(run('gh', ['api', `repos/${notice.repository}/actions/runs/${id}/attempts/${attempt}`]));
    if (actual?.id !== id || actual.run_attempt !== attempt || actual.event !== 'push' ||
        actual.head_branch !== branch || actual.head_sha !== notice.sha ||
        actual.repository?.full_name?.toLowerCase() !== notice.repository.toLowerCase() ||
        (expectedWorkflow && actual.name !== expectedWorkflow)) {
      throw new Error('No matching GitHub push run attempt for the exact waiting event; registration unchanged');
    }
    return { id, attempt, workflow: actual.name };
  });
  return JSON.stringify({ source: 'github-push-run-attempt', repository: notice.repository, branch,
    sha: notice.sha, eventId: notice.id, runs: verified, verifiedAt: new Date().toISOString() });
}
