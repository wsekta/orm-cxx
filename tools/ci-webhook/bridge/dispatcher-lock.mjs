import { DatabaseSync } from 'node:sqlite';
import { mkdirSync, realpathSync } from 'node:fs';
import { basename, dirname, join, resolve } from 'node:path';

// A separate SQLite transaction is an OS-backed process-lifetime lock. Writers
// registering routes in the delivery journal never need this database.
export function acquireDispatcherLock(journalPath) {
  if (typeof journalPath !== 'string' || !journalPath) throw new Error('Missing dispatcher journal path');
  const absolute = resolve(journalPath);
  mkdirSync(dirname(absolute), { recursive: true });
  let canonical;
  try { canonical = realpathSync(absolute); }
  catch (error) {
    if (error.code !== 'ENOENT') throw error;
    canonical = join(realpathSync(dirname(absolute)), basename(absolute));
  }
  const database = new DatabaseSync(canonical + '.dispatcher-lock.sqlite');
  try {
    database.exec('PRAGMA busy_timeout=0; PRAGMA locking_mode=EXCLUSIVE; BEGIN EXCLUSIVE; CREATE TABLE IF NOT EXISTS dispatcher_lock (id INTEGER PRIMARY KEY);');
  } catch (error) {
    database.close();
    if (/locked|busy/i.test(error.message)) throw new Error('Another dispatcher owns this delivery journal', { cause: error });
    throw error;
  }
  let released = false;
  return {
    release() {
      if (released) return;
      released = true;
      try { database.exec('ROLLBACK'); } finally { database.close(); }
    }
  };
}
