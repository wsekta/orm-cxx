import WebSocket from 'ws';
import { parseAppServer } from '../bridge/app-server.mjs';

// This client only reads status/history. Desktop remains the interactive client.
export async function connectRpc(endpoint, { timeoutMs = 15000 } = {}) {
  if (parseAppServer(endpoint).kind !== 'websocket') throw new Error('Desktop launcher requires a local WebSocket');
  const socket = new WebSocket(endpoint, { handshakeTimeout: timeoutMs, perMessageDeflate: false, maxPayload: 128 * 1024 * 1024 });
  const pending = new Map();
  let sequence = 0;
  const fail = error => {
    for (const request of pending.values()) { clearTimeout(request.timer); request.reject(error); }
    pending.clear();
  };
  socket.on('error', fail);
  socket.on('close', () => fail(new Error('App-server connection closed')));
  socket.on('message', bytes => {
    let response;
    try { response = JSON.parse(bytes.toString()); } catch { fail(new Error('Invalid app-server response')); socket.terminate(); return; }
    if (response.method) return; // Notifications and requests belong to Desktop.
    const request = pending.get(response.id);
    if (!request) return;
    pending.delete(response.id); clearTimeout(request.timer);
    if (response.error) request.reject(new Error(`App-server RPC failed (${response.error.code})`));
    else request.resolve(response.result);
  });
  await new Promise((resolve, reject) => { socket.once('open', resolve); socket.once('error', reject); });
  const request = (method, params) => new Promise((resolve, reject) => {
    const id = ++sequence;
    const timer = setTimeout(() => { pending.delete(id); reject(new Error(`App-server request timed out: ${method}`)); }, timeoutMs);
    pending.set(id, { resolve, reject, timer });
    socket.send(JSON.stringify({ id, method, ...(params === undefined ? {} : { params }) }), error => {
      if (error) { clearTimeout(timer); pending.delete(id); reject(error); }
    });
  });
  try {
    await request('initialize', { clientInfo: { name: 'ci_webhook_desktop_launcher', version: '0.1.0' }, capabilities: { experimentalApi: true } });
    socket.send(JSON.stringify({ method: 'initialized' }));
    return { request, close: () => { fail(new Error('RPC client closed')); socket.close(); } };
  } catch (error) { socket.terminate(); throw error; }
}

export async function inspectThread(client, threadId) {
  const result = await client.request('thread/read', { threadId });
  if (result.thread?.id !== threadId) throw new Error('App-server returned a different thread');
  if (result.thread.status?.type !== 'idle') throw new Error('Desktop has not resumed the target thread into idle state');
  let cursor, turns = 0;
  const cursors = new Set();
  do {
    const page = await client.request('thread/turns/list', { threadId, limit: 50, sortDirection: 'asc', itemsView: 'full', ...(cursor ? { cursor } : {}) });
    if (!Array.isArray(page.data)) throw new Error('App-server did not return turn history');
    turns += page.data.length;
    cursor = page.nextCursor;
    if (cursor && cursors.has(cursor)) throw new Error('App-server history cursor repeated');
    cursors.add(cursor);
  } while (cursor);
  if (turns === 0) throw new Error('App-server returned empty target history');
  return { threadId, turns, status: result.thread.status.type };
}
