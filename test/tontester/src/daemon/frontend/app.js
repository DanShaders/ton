const OVERLAY_MAX = 5;

const fmt = (iso) => (iso ? new Date(iso).toLocaleString() : '');

const grafanaBase = (info) =>
  info.grafana_url ? `${info.grafana_url}/d/ton-overview` : null;

// Build URL manually — URLSearchParams collapses repeated keys, and
// Grafana's multi-select needs ``var-datasource`` to appear once per run.
const buildUrl = (info, pairs) => {
  const base = grafanaBase(info);
  if (!base) return '#';
  const qs = pairs.map(([k, v]) => `${encodeURIComponent(k)}=${encodeURIComponent(v)}`).join('&');
  return `${base}?${qs}`;
};

// Keyed by run_id so row updates upsert in place.
const state = {
  info: null,
  runs: new Map(),
  selected: new Set(),
};

function singleRunPairs(run) {
  const pairs = [['var-datasource', run.run_id]];
  if (run.status === 'dormant' && run.end_time) {
    pairs.push(['from', String(new Date(run.start_time).getTime())]);
    pairs.push(['to', String(new Date(run.end_time).getTime())]);
  } else {
    pairs.push(['refresh', '5s']);
  }
  return pairs;
}

function overlayPairs(runs) {
  // One ``var-datasource`` per selected run; window covers the union of
  // their lifetimes when every selection is dormant, else tracks "now"
  // so live scrapers keep flowing. The 1% pad on each end gives lines a
  // bit of breathing room on the panel edges — otherwise the first/last
  // samples render right against the axes.
  const pairs = runs.map((r) => ['var-datasource', r.run_id]);
  const allDormant = runs.every((r) => r.status === 'dormant' && r.end_time);
  if (allDormant) {
    const from = Math.min(...runs.map((r) => new Date(r.start_time).getTime()));
    const to = Math.max(...runs.map((r) => new Date(r.end_time).getTime()));
    const pad = Math.max(1, Math.round((to - from) * 0.01));
    pairs.push(['from', String(from - pad)]);
    pairs.push(['to', String(to + pad)]);
  } else {
    pairs.push(['refresh', '5s']);
  }
  return pairs;
}

function rowHtml(info, run, selected) {
  const runHref = buildUrl(info, singleRunPairs(run));
  const promHref = `/runs/${run.run_id}/prom/`;

  const nodeLinks = run.nodes
    .map((n) => {
      const nodeHref = buildUrl(info, [
        ...singleRunPairs(run),
        ['var-node', n.name],
      ]);
      return `<a href="${nodeHref}" target="_blank" rel="noopener">${n.name}</a>
              <span class="muted">@ ${n.address}</span>`;
    })
    .join('<br>');

  const checked = selected ? 'checked' : '';
  return `
    <td class="select-col">
      <input type="checkbox" class="run-select" data-run-id="${run.run_id}" ${checked} />
    </td>
    <td>
      <code>${run.run_id}</code><br>
      <a class="run-link" href="${runHref}" target="_blank" rel="noopener">
        Grafana →
      </a><br>
      <a class="run-link" href="${promHref}" target="_blank" rel="noopener">
        Prometheus →
      </a>
    </td>
    <td>${run.status}</td>
    <td>${fmt(run.start_time)}</td>
    <td>${fmt(run.end_time)}</td>
    <td>${nodeLinks}</td>
    <td>${run.description}</td>
    <td>${run.git_branch} ${run.git_commit_id ? '(' + run.git_commit_id.slice(0, 8) + ')' : ''
    }</td>
  `;
}

// Newest first — matches server-side ``list_runs ORDER BY start_time DESC``.
const byStartDesc = (a, b) =>
  new Date(b.start_time).getTime() - new Date(a.start_time).getTime();

function sortedRuns() {
  return [...state.runs.values()].sort(byStartDesc);
}

function renderRow(run) {
  const tbody = document.querySelector('#runs tbody');
  let tr = document.getElementById(`run-${run.run_id}`);
  const fresh = tr === null;
  if (fresh) {
    tr = document.createElement('tr');
    tr.id = `run-${run.run_id}`;
  }
  tr.className = run.status;
  tr.innerHTML = rowHtml(state.info, run, state.selected.has(run.run_id));
  if (fresh) {
    // Insert in start_time-desc order rather than always-prepend, so a
    // late-arriving update for an older run doesn't jump to the top.
    const runs = sortedRuns();
    const idx = runs.findIndex((r) => r.run_id === run.run_id);
    const after = runs[idx + 1];
    const anchor = after ? document.getElementById(`run-${after.run_id}`) : null;
    tbody.insertBefore(tr, anchor);
  }
}

function renderAll() {
  const tbody = document.querySelector('#runs tbody');
  tbody.innerHTML = '';
  // Drop selections for runs that no longer exist (shouldn't happen,
  // runs are append-only, but be defensive).
  for (const id of [...state.selected]) {
    if (!state.runs.has(id)) state.selected.delete(id);
  }
  for (const run of sortedRuns()) {
    const tr = document.createElement('tr');
    tr.id = `run-${run.run_id}`;
    tr.className = run.status;
    tr.innerHTML = rowHtml(state.info, run, state.selected.has(run.run_id));
    tbody.appendChild(tr);
  }
  updateToolbar();
}

function updateToolbar() {
  const n = state.selected.size;
  const btn = document.getElementById('overlay-btn');
  const count = document.getElementById('overlay-count');
  const overflow = n > OVERLAY_MAX;
  btn.disabled = n === 0 || overflow;
  btn.title = overflow ? `At most ${OVERLAY_MAX} runs can be overlaid at once` : '';
  count.textContent = overflow
    ? `${n} selected — max ${OVERLAY_MAX}`
    : `${n} selected`;
  count.classList.toggle('warn', overflow);
  const selectAll = document.getElementById('select-all');
  selectAll.checked = n > 0 && n === state.runs.size;
  selectAll.indeterminate = n > 0 && n < state.runs.size;
}

function handleMessage(msg) {
  if (msg.type === 'initial') {
    state.info = msg.info;
    state.runs = new Map(msg.runs.map((r) => [r.run_id, r]));
    renderAll();
  } else if (msg.type === 'run_update') {
    state.runs.set(msg.run.run_id, msg.run);
    renderRow(msg.run);
    updateToolbar();
  }
}

document.addEventListener('click', (e) => {
  const t = e.target;
  if (t.matches('.run-select')) {
    const id = t.dataset.runId;
    if (t.checked) state.selected.add(id);
    else state.selected.delete(id);
    updateToolbar();
  } else if (t.id === 'select-all') {
    if (t.checked) {
      for (const id of state.runs.keys()) state.selected.add(id);
    } else {
      state.selected.clear();
    }
    // Sync every row checkbox without a full re-render.
    for (const cb of document.querySelectorAll('.run-select')) {
      cb.checked = state.selected.has(cb.dataset.runId);
    }
    updateToolbar();
  } else if (t.id === 'overlay-btn') {
    if (state.selected.size === 0 || state.selected.size > OVERLAY_MAX) return;
    const runs = [...state.selected].map((id) => state.runs.get(id)).filter(Boolean);
    window.open(buildUrl(state.info, overlayPairs(runs)), '_blank', 'noopener');
  }
});

function connect() {
  const wsProto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const ws = new WebSocket(`${wsProto}//${location.host}/ws/dashboard`);
  ws.onmessage = (e) => {
    try {
      handleMessage(JSON.parse(e.data));
    } catch (err) {
      console.error(err);
    }
  };
  ws.onclose = () => setTimeout(connect, 1000);
  ws.onerror = () => ws.close();
}

connect();
