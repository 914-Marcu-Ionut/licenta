const BASE = '/api';

export async function api(method, path, body) {
  const opts = {
    method,
    headers: { 'Content-Type': 'application/json' },
  };
  if (body) opts.body = JSON.stringify(body);
  const res = await fetch(BASE + path, opts);
  const data = await res.json();
  return { ok: res.ok, status: res.status, data };
}

export function createExamSocket(runId, teacherName, password, handlers) {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const ws = new WebSocket(`${proto}//${location.host}/ws`);

  ws.onopen = () => {
    handlers.onStatus?.('connected');
    ws.send(JSON.stringify({
      id: 1,
      method: 'teacher_login',
      params: { name: teacherName, password, run_id: runId },
    }));
  };

  ws.onmessage = (ev) => {
    try {
      const msg = JSON.parse(ev.data);
      handlers.onMessage?.(msg);
    } catch {}
  };

  ws.onclose = () => handlers.onStatus?.('disconnected');
  ws.onerror = () => handlers.onStatus?.('disconnected');

  return ws;
}
