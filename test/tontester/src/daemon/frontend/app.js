const fmt = (iso) => (iso ? new Date(iso).toLocaleString() : '');

const grafanaBase = (info) =>
  info.grafana_url ? `${info.grafana_url}/d/ton-overview` : null;

const dashboardLink = (info, params) => {
  const base = grafanaBase(info);
  if (!base) return '#';
  const qs = new URLSearchParams(params).toString();
  return `${base}?${qs}`;
};

async function refresh() {
  try {
    const info = await fetch('/api/info').then((r) => r.json());
    document.getElementById('grafana-link').href = dashboardLink(info, { refresh: '5s' });

    const runs = await fetch('/api/runs').then((r) => r.json());
    const tbody = document.querySelector('#runs tbody');
    tbody.innerHTML = '';
    for (const run of runs) {
      const tr = document.createElement('tr');
      tr.className = run.status;

      const runParams = { 'var-datasource': run.run_id };
      if (run.status === 'dormant' && run.end_time) {
        // Pin the view to the run's window and stop auto-refresh — there
        // will be no new samples.
        runParams.from = String(new Date(run.start_time).getTime());
        runParams.to = String(new Date(run.end_time).getTime());
      } else {
        runParams.refresh = '5s';
      }
      const runHref = dashboardLink(info, runParams);
      const promHref = `/runs/${run.run_id}/prom/`;

      const nodeLinks = run.nodes
        .map((n) => {
          const nodeHref = dashboardLink(info, {
            ...runParams,
            'var-node': n.name,
          });
          return `<a href="${nodeHref}" target="_blank" rel="noopener">${n.name}</a>
                  <span class="muted">@ ${n.address}</span>`;
        })
        .join('<br>');

      tr.innerHTML = `
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
      tbody.appendChild(tr);
    }
  } catch (e) {
    console.error(e);
  }
}

refresh();
setInterval(refresh, 2000);
