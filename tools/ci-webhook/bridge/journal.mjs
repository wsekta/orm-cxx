import { DatabaseSync } from 'node:sqlite';
import { existsSync, mkdirSync, realpathSync } from 'node:fs';
import { dirname, basename, join, resolve } from 'node:path';

const uuid = /^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/;
const repositoryPattern = /^[\w.-]+\/[\w.-]+$/;
const shaPattern = /^[a-f0-9]{40}$/;

export function validBranch(value) {
  return typeof value === 'string' && value.length > 0 && value.length <= 1024 && value !== '@' &&
    !value.startsWith('-') && !value.startsWith('/') && !value.endsWith('/') && !value.endsWith('.') &&
    !value.includes('..') && !value.includes('//') && !value.includes('@{') &&
    !/[\x00-\x20\x7f~^:?*\[\\]/.test(value) &&
    value.split('/').every(part => part.length > 0 && !part.startsWith('.') && !part.endsWith('.lock'));
}

export function noticeBranch(notice) { return Object.hasOwn(notice, 'branch') ? notice.branch : 'main'; }
function scope(repository, branch, sha, thread) {
  if (!repositoryPattern.test(repository) || !validBranch(branch) ||
      (sha !== undefined && !shaPattern.test(sha)) || (thread !== undefined && !uuid.test(thread))) {
    throw new Error('Invalid repository, branch, SHA or thread UUID');
  }
}
function canonical(value) {
  if (Array.isArray(value)) return value.map(canonical);
  if (value && typeof value === 'object') return Object.fromEntries(Object.keys(value).sort().map(key => [key, canonical(value[key])]));
  return value;
}
function samePayload(left, right) { return JSON.stringify(canonical(left)) === JSON.stringify(canonical(right)); }
function proof(evidence) {
  if (typeof evidence !== 'string' || evidence.trim().length < 3 || evidence.length > 16384) throw new Error('Recovery requires recorded evidence');
}

export class Journal {
  constructor(path) {
    const absolute = resolve(path);
    mkdirSync(dirname(absolute), { recursive: true });
    this.path = existsSync(absolute) ? realpathSync(absolute) : join(realpathSync(dirname(absolute)), basename(absolute));
    this.db = new DatabaseSync(this.path);
    try {
      this.db.exec(`PRAGMA busy_timeout=5000; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON;
        CREATE TABLE IF NOT EXISTS notices (id TEXT PRIMARY KEY, repository TEXT NOT NULL, sha TEXT NOT NULL, data TEXT NOT NULL, status TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS routes (repository TEXT NOT NULL, sha TEXT NOT NULL, thread TEXT NOT NULL, PRIMARY KEY (repository, sha));
        CREATE TABLE IF NOT EXISTS routes_by_branch (repository TEXT NOT NULL, branch TEXT NOT NULL, sha TEXT NOT NULL, thread TEXT NOT NULL, PRIMARY KEY (repository, branch, sha));
        CREATE TABLE IF NOT EXISTS bridge_migrations (name TEXT PRIMARY KEY);
        CREATE TABLE IF NOT EXISTS branch_owners (repository TEXT NOT NULL, branch TEXT NOT NULL, thread TEXT, state TEXT NOT NULL, worktree TEXT NOT NULL DEFAULT '', updated INTEGER NOT NULL, PRIMARY KEY (repository, branch));
        CREATE TABLE IF NOT EXISTS route_write_guard (id INTEGER PRIMARY KEY CHECK (id=1), enabled INTEGER NOT NULL);
        INSERT OR IGNORE INTO route_write_guard VALUES (1,0);
        CREATE TABLE IF NOT EXISTS delivery_details (id TEXT PRIMARY KEY REFERENCES notices(id), destination TEXT, target_unknown INTEGER NOT NULL DEFAULT 0,
          next_attempt INTEGER NOT NULL DEFAULT 0, attempts INTEGER NOT NULL DEFAULT 0, received INTEGER NOT NULL, receiver_released INTEGER NOT NULL DEFAULT 0, updated INTEGER NOT NULL,
          last_error TEXT NOT NULL DEFAULT '', evidence TEXT NOT NULL DEFAULT '');`);
      this.transaction(() => {
        if (!this.db.prepare('SELECT name FROM bridge_migrations WHERE name=?').get('routes_by_branch_v1')) {
          this.db.exec("INSERT OR IGNORE INTO routes_by_branch SELECT repository, 'main', sha, thread FROM routes;");
          this.db.prepare('INSERT INTO bridge_migrations VALUES (?)').run('routes_by_branch_v1');
        }
        if (!this.db.prepare('SELECT name FROM bridge_migrations WHERE name=?').get('parallel_delivery_v1')) {
          const owners = this.db.prepare('SELECT repository, branch, COUNT(DISTINCT thread) AS owners, MIN(thread) AS thread FROM routes_by_branch GROUP BY repository, branch').all();
          for (const row of owners) this.db.prepare('INSERT INTO branch_owners VALUES (?, ?, ?, ?, ?, ?)').run(row.repository, row.branch,
            row.owners === 1 ? row.thread : null, row.owners === 1 ? 'active' : 'conflict', '', Date.now());
          this.db.prepare('INSERT INTO bridge_migrations VALUES (?)').run('parallel_delivery_v1');
        }
        // Rows from v1 have no historical pinned destination. Never infer one
        // for an ambiguous send from a route that v1 could silently overwrite.
        const missing = this.db.prepare('SELECT n.* FROM notices n LEFT JOIN delivery_details d ON d.id=n.id WHERE d.id IS NULL ORDER BY n.rowid').all();
        for (const row of missing) {
          const ambiguous = ['dispatching', 'uncertain'].includes(row.status);
          const historicalTargetUnknown = ambiguous || row.status === 'accepted';
          const destination = historicalTargetUnknown ? null : this.thread(JSON.parse(row.data));
          this.db.prepare('INSERT INTO delivery_details (id,destination,target_unknown,received,updated) VALUES (?,?,?,?,?)')
            .run(row.id, destination ?? null, historicalTargetUnknown ? 1 : 0, Date.now(), Date.now());
          if (row.status === 'received') this.db.prepare('UPDATE notices SET status=? WHERE id=?').run(destination ? 'ready' : 'waiting_for_route', row.id);
        }
        // An old checkout's register.mjs uses INSERT OR REPLACE. Refuse it even
        // when it still points at this same journal after the runtime upgrade.
        this.db.exec(`CREATE TRIGGER IF NOT EXISTS routes_require_current_writer BEFORE INSERT ON routes_by_branch
          WHEN (SELECT enabled FROM route_write_guard WHERE id=1)!=1
          BEGIN SELECT RAISE(ABORT, 'Obsolete route writer refused; use the current installed registration tool'); END;
          CREATE TRIGGER IF NOT EXISTS routes_require_current_update BEFORE UPDATE ON routes_by_branch
          WHEN (SELECT enabled FROM route_write_guard WHERE id=1)!=1
          BEGIN SELECT RAISE(ABORT, 'Obsolete route writer refused; use the current installed registration tool'); END;`);
      });
    } catch (error) { this.db.close(); throw error; }
  }
  transaction(action) {
    this.db.exec('BEGIN IMMEDIATE');
    try { const result = action(); this.db.exec('COMMIT'); return result; }
    catch (error) { this.db.exec('ROLLBACK'); throw error; }
  }
  register(repository, branch, sha, thread, worktree = '') {
    scope(repository, branch, sha, thread);
    return this.transaction(() => this.registerInside(repository, branch, sha, thread, worktree));
  }
  registerInside(repository, branch, sha, thread, worktree = '') {
    const owner = this.db.prepare('SELECT * FROM branch_owners WHERE repository=? AND branch=?').get(repository, branch);
    if (owner && (owner.state !== 'active' || owner.thread !== thread)) {
      throw new Error(`Registration conflict for ${repository} ${branch}: owner=${owner.thread ?? owner.state}, state=${owner.state}; use explicit ownership transfer`);
    }
    const route = this.db.prepare('SELECT thread FROM routes_by_branch WHERE repository=? AND branch=? AND sha=?').get(repository, branch, sha);
    if (route && route.thread !== thread) throw new Error(`Exact commit registration conflict for ${branch} ${sha}: owner=${route.thread}`);
    if (!owner) this.db.prepare('INSERT INTO branch_owners VALUES (?,?,?,?,?,?)').run(repository, branch, thread, 'active', worktree, Date.now());
    else if (worktree) this.db.prepare('UPDATE branch_owners SET worktree=?,updated=? WHERE repository=? AND branch=?').run(worktree, Date.now(), repository, branch);
    this.db.exec('UPDATE route_write_guard SET enabled=1 WHERE id=1');
    this.db.prepare('INSERT OR IGNORE INTO routes_by_branch VALUES (?,?,?,?)').run(repository, branch, sha, thread);
    this.db.exec('UPDATE route_write_guard SET enabled=0 WHERE id=1');
    this.resolveWaiting();
    return { repository, branch, sha, thread, worktree: worktree || owner?.worktree || '', registered: true, desktopDeliveryVerified: false };
  }
  transfer(repository, branch, expectedOwner, newThread) {
    scope(repository, branch, undefined, newThread);
    return this.transaction(() => {
      const owner = this.db.prepare('SELECT * FROM branch_owners WHERE repository=? AND branch=?').get(repository, branch);
      const historical = this.db.prepare('SELECT DISTINCT thread FROM routes_by_branch WHERE repository=? AND branch=? ORDER BY thread').all(repository, branch).map(row => row.thread);
      const matches = Array.isArray(expectedOwner)
        ? owner?.state === 'conflict' && JSON.stringify([...expectedOwner].sort()) === JSON.stringify(historical)
        : owner?.thread === expectedOwner;
      if (!owner || !matches) throw new Error('Branch ownership changed; transfer refused');
      this.db.prepare("UPDATE branch_owners SET thread=?,state='active',worktree='',updated=? WHERE repository=? AND branch=?")
        .run(newThread, Date.now(), repository, branch);
      return { repository, branch, previousOwner: expectedOwner, thread: newThread, affects: 'future registrations only' };
    });
  }
  closeBranch(repository, branch, expectedThread) {
    scope(repository, branch, undefined, expectedThread);
    const result = this.db.prepare("UPDATE branch_owners SET state='closed',updated=? WHERE repository=? AND branch=? AND thread=? AND state='active'")
      .run(Date.now(), repository, branch, expectedThread);
    if (result.changes !== 1) throw new Error('Branch ownership changed or branch is not active; close refused');
    return { repository, branch, thread: expectedThread, state: 'closed', historicalRoutesPreserved: true };
  }
  thread(notice) {
    return this.db.prepare('SELECT thread FROM routes_by_branch WHERE repository=? AND branch=? AND sha=?')
      .get(notice.repository, noticeBranch(notice), notice.sha)?.thread;
  }
  intake(notice) {
    return this.transaction(() => {
      const existing = this.db.prepare('SELECT data FROM notices WHERE id=?').get(notice.id);
      if (existing && !samePayload(JSON.parse(existing.data), notice)) throw new Error(`Conflicting payload for CI event ${notice.id}`);
      if (!existing) {
        const destination = this.thread(notice);
        this.db.prepare('INSERT INTO notices VALUES (?,?,?,?,?)').run(notice.id, notice.repository, notice.sha, JSON.stringify(notice), destination ? 'ready' : 'waiting_for_route');
        this.db.prepare('INSERT INTO delivery_details (id,destination,received,updated) VALUES (?,?,?,?)').run(notice.id, destination ?? null, Date.now(), Date.now());
      }
      const row = this.waiting(notice.id);
      return { id: notice.id, status: row.status, destination: row.destination };
    });
  }
  receive(notice) { return this.intake(notice).status; }
  waiting(id) {
    const row = this.db.prepare('SELECT n.*,d.destination,d.target_unknown,d.next_attempt,d.attempts,d.last_error,d.evidence,n.rowid AS seq FROM notices n JOIN delivery_details d ON d.id=n.id WHERE n.id=?').get(id);
    return row ? { ...row, notice: JSON.parse(row.data), destination: row.destination ?? undefined } : undefined;
  }
  resolveWaiting() {
    const rows = this.db.prepare("SELECT n.id,n.data FROM notices n JOIN delivery_details d ON d.id=n.id WHERE n.status IN ('received','waiting_for_route') AND d.destination IS NULL AND d.target_unknown=0 ORDER BY n.rowid").all();
    for (const row of rows) {
      const destination = this.thread(JSON.parse(row.data));
      if (destination) {
        this.db.prepare('UPDATE delivery_details SET destination=?,updated=? WHERE id=?').run(destination, Date.now(), row.id);
        this.db.prepare("UPDATE notices SET status='ready' WHERE id=?").run(row.id);
      }
    }
  }
  candidates(now = Date.now(), limit = 2) {
    if (!Number.isInteger(limit) || limit <= 0) return [];
    return this.transaction(() => {
      this.resolveWaiting();
      const rows = this.db.prepare(`SELECT n.id,n.data,n.status,d.destination,d.next_attempt,d.attempts FROM notices n JOIN delivery_details d ON d.id=n.id
        WHERE n.status!='accepted' AND d.destination IS NOT NULL ORDER BY n.rowid`).all();
      const seen = new Set(), result = [];
      for (const row of rows) {
        if (seen.has(row.destination)) continue;
        seen.add(row.destination);
        if (['ready', 'received'].includes(row.status) && row.next_attempt <= now) result.push({ id: row.id, notice: JSON.parse(row.data), thread: row.destination, attempts: row.attempts });
        if (result.length >= limit) break;
      }
      return result;
    });
  }
  claim(id, thread) {
    return this.transaction(() => {
      const row = this.waiting(id);
      if (!row || !['ready', 'received'].includes(row.status) || !row.destination || (thread && row.destination !== thread) || row.next_attempt > Date.now()) return false;
      const prior = this.db.prepare(`SELECT n.id FROM notices n JOIN delivery_details d ON d.id=n.id WHERE d.destination=? AND n.status!='accepted' AND n.rowid<? LIMIT 1`).get(row.destination, row.seq);
      if (prior) return false;
      this.db.prepare("UPDATE notices SET status='dispatching' WHERE id=?").run(id);
      this.db.prepare('UPDATE delivery_details SET attempts=attempts+1,updated=? WHERE id=?').run(Date.now(), id);
      return true;
    });
  }
  mark(id, status, error = '') {
    if (!['accepted', 'uncertain', 'dispatching'].includes(status)) throw new Error('Invalid delivery state');
    this.transaction(() => {
      if (!this.waiting(id)) throw new Error('Unknown CI event');
      this.db.prepare('UPDATE notices SET status=? WHERE id=?').run(status, id);
      this.db.prepare('UPDATE delivery_details SET updated=?,last_error=? WHERE id=?').run(Date.now(), String(error).slice(0,4096), id);
    });
  }
  retry(id, error, delayMs) {
    this.transaction(() => {
      const row = this.waiting(id);
      if (!row || !['ready', 'received'].includes(row.status)) throw new Error('Only a pre-send failure is safely retryable');
      this.db.prepare("UPDATE notices SET status='ready' WHERE id=?").run(id);
      this.db.prepare('UPDATE delivery_details SET next_attempt=?,attempts=attempts+1,updated=?,last_error=? WHERE id=?').run(Date.now() + Math.max(0, delayMs), Date.now(), String(error).slice(0,4096), id);
    });
  }
  recoverDispatching() {
    this.transaction(() => {
      this.db.exec("UPDATE notices SET status='uncertain' WHERE status='dispatching'");
    });
    const unknown = this.db.prepare("SELECT n.id FROM notices n JOIN delivery_details d ON d.id=n.id WHERE n.status='uncertain' AND (d.target_unknown=1 OR d.destination IS NULL)").all();
    if (unknown.length) throw new Error(`Migration blocked: ${unknown.length} ambiguous legacy deliveries have unknown historical destinations; inspect and reconcile before starting the dispatcher`);
  }
  acknowledged(id) {
    const result = this.db.prepare('UPDATE delivery_details SET receiver_released=?,updated=? WHERE id=?').run(Date.now(), Date.now(), id);
    if (result.changes !== 1) throw new Error('Receiver acknowledged an event missing from the local inbox');
  }
  adopt(id, thread, evidence) {
    if (!uuid.test(thread)) throw new Error('Invalid thread UUID');
    proof(evidence);
    return this.transaction(() => {
      const row = this.waiting(id);
      if (!row || !['received', 'waiting_for_route'].includes(row.status) || row.destination) throw new Error('Only a selected unregistered waiting event can be adopted');
      const notice = row.notice;
      const registration = this.registerInside(notice.repository, noticeBranch(notice), notice.sha, thread);
      this.db.prepare('UPDATE delivery_details SET evidence=?,updated=? WHERE id=?').run(evidence, Date.now(), id);
      return registration;
    });
  }
  reconcile(id, status, evidence, target) {
    if (!['accepted', 'received'].includes(status)) throw new Error('Reconciliation must select accepted or received');
    proof(evidence);
    return this.transaction(() => {
      const row = this.waiting(id);
      if (!row || !['dispatching', 'uncertain'].includes(row.status)) throw new Error('Only ambiguous deliveries can be reconciled');
      const destination = row.destination ?? target;
      if (!uuid.test(destination ?? '') || (row.destination && target && row.destination !== target)) throw new Error('Supply the evidenced historical destination; a pinned destination cannot change');
      this.db.prepare('UPDATE delivery_details SET destination=?,target_unknown=0,next_attempt=0,evidence=?,updated=? WHERE id=?').run(destination, evidence, Date.now(), id);
      this.db.prepare('UPDATE notices SET status=? WHERE id=?').run(status === 'received' ? 'ready' : status, id);
      return { id, status: status === 'received' ? 'ready' : status, destination, evidence };
    });
  }
  status({ branch, thread } = {}) {
    const owners = this.db.prepare('SELECT * FROM branch_owners ORDER BY repository,branch').all().filter(row => (!branch || row.branch === branch) && (!thread || row.thread === thread));
    const routes = this.db.prepare('SELECT * FROM routes_by_branch ORDER BY repository,branch,sha').all().filter(row => (!branch || row.branch === branch) && (!thread || row.thread === thread));
    const all = this.db.prepare('SELECT n.id,n.data,n.status,d.destination,d.target_unknown,d.attempts,d.last_error,d.received,d.receiver_released,d.updated FROM notices n JOIN delivery_details d ON d.id=n.id ORDER BY n.rowid').all()
      .map(row => { const notice = JSON.parse(row.data); return { id: row.id, repository: notice.repository, branch: noticeBranch(notice), sha: notice.sha, status: row.status, destination: row.destination, targetUnknown: Boolean(row.target_unknown), attempts: row.attempts, lastError: row.last_error, localReceipt: row.received, receiverReleased: row.receiver_released || null, updated: row.updated }; })
      .filter(row => (!branch || row.branch === branch) && (!thread || row.destination === thread));
    const counts = {};
    for (const row of all) counts[row.status] = (counts[row.status] ?? 0) + 1;
    return { schemaVersion: 2, journalPath: this.path, owners, routes, counts, deliveries: all.slice(-100), deliveryCount: all.length,
      transportReceiptMeans: 'committed local inbox, not Desktop delivery', desktopDeliveryVerified: false };
  }
  close() { this.db.close(); }
}
