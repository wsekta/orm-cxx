import { formatMessage } from './core.mjs';

export function createDispatcher(journal, sender, {
  concurrency = 2, reconcileIntervalMs = 1000, retryDelayMs = 1000,
  maxRetryDelayMs = 60000, clock = Date.now, log = console.log, onFatal = () => {}
} = {}) {
  for (const [name, value] of Object.entries({ concurrency, reconcileIntervalMs, retryDelayMs, maxRetryDelayMs })) {
    if (!Number.isSafeInteger(value) || value < 1) throw new Error(`Invalid dispatcher ${name}`);
  }
  if (concurrency > 32) throw new Error('Dispatcher concurrency must not exceed 32');
  if (typeof clock !== 'function') throw new Error('Invalid dispatcher clock');
  journal.recoverDispatching();
  const active = new Map();
  let stopped = false, pumping = false, fault, shutdown;
  const emit = event => log(JSON.stringify(event));
  const fail = error => {
    if (fault) return;
    fault = error;
    stopped = true;
    clearInterval(timer);
    emit({ event: 'dispatcher-error', reason: error.message });
    onFatal(error);
  };
  async function dispatch(item) {
    try {
      // Readiness is checked before claim: these failures are safe to retry.
      await sender.probe(item.thread);
    } catch (error) {
      const delay = Math.min(maxRetryDelayMs, retryDelayMs * 2 ** Math.min(item.attempts ?? 0, 16));
      journal.retry(item.id, error.message, delay);
      emit({ event: 'delivery-deferred', id: item.id, thread: item.thread, reason: error.message, retryAfterMs: delay });
      return;
    }
    if (stopped || !journal.claim(item.id, item.thread)) return;
    try {
      await sender.send(item.thread, formatMessage(item.notice));
      journal.mark(item.id, 'accepted');
    } catch (error) {
      // This includes a failed local accepted write after a successful command.
      // Keep the claim uncertain instead of sending the same command again.
      journal.mark(item.id, 'uncertain', error.message);
      emit({ event: 'delivery-uncertain', id: item.id, thread: item.thread, reason: error.message });
      return;
    }
    emit({ event: 'delivered', id: item.id, thread: item.thread, repository: item.notice.repository });
  }
  function wake() {
    if (stopped || pumping) return;
    pumping = true;
    try {
      for (const item of journal.candidates(clock(), concurrency + active.size)) {
        if (stopped || active.size >= concurrency) break;
        if (active.has(item.thread)) continue;
        const promise = dispatch(item).catch(fail).finally(() => {
          active.delete(item.thread);
          if (!stopped) queueMicrotask(wake);
        });
        active.set(item.thread, promise);
      }
    } catch (error) { fail(error); }
    finally { pumping = false; }
  }
  // This timer only reconciles local SQLite work and safe retries. It never
  // queries GitHub or runs an agent turn while waiting for CI.
  const timer = setInterval(wake, reconcileIntervalMs);
  timer.unref?.();
  queueMicrotask(wake);
  return {
    wake,
    async idle() {
      for (;;) {
        if (!stopped) wake();
        const pending = [...active.values()];
        if (!pending.length) return;
        await Promise.allSettled(pending);
      }
    },
    stop() {
      if (!shutdown) {
        stopped = true;
        clearInterval(timer);
        shutdown = Promise.allSettled([...active.values()]).then(() => {});
      }
      return shutdown;
    },
    get fault() { return fault; }
  };
}
