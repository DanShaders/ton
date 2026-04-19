# tontester dashboard daemon

Long-running local process that boots Grafana + one Prometheus instance
**per test run** (via `podman`) and exposes a dashboard to browse live and
archived runs.

## Why per-run Prometheus?

A single shared Prometheus with `run_id` as a label blows up cardinality:
every new run creates a fresh series for every metric. Instead, each run
gets its own Prometheus container over its own data directory; runs are
isolated by container, not by label. When the run ends the container is
stopped (data stays on disk); when someone queries that run later the
daemon lazy-boots a fresh read-only container from the same data dir.

## Components

```
  test harness (tontester Network)
        │  websocket over UDS (handshake + heartbeat)
        ▼
  daemon ────► Compose ────► podman
   │            (primitives)    │
   │                            ├── tontester-grafana
   │                            ├── tontester-prom-<run_id>  (live)
   │                            └── tontester-prom-<run_id>  (lazy-booted archive)
   │
   ├── FastAPI app
   │     • /api/runs, /api/info   — dashboard JSON
   │     • /runs/{id}/prom/*      — proxy to live/archive Prometheus
   │     • WS /runs/{id}          — harness IPC (register + heartbeat)
   │     • POST /admin/shutdown   — graceful stop
   │
   └── Grafana provisioning
         • one datasource YAML per run (hot-reloaded)
         • one dashboard with a datasource-variable dropdown
```

### Files

- `compose.py` — the only file in the package that shells out to `podman`.
  `Service` is a plain pydantic model; `Compose` runs/stops/inspects
  containers and knows about the network. No tontester concepts leak in.
- `services.py` — factory functions `prometheus_service(...)`,
  `grafana_service(...)`, plus `GrafanaProvisioning` for on-disk YAMLs.
- `runs.py` — `RunsManager` owns the per-run lifecycle state machine
  (`LIVE` ↔ `DORMANT`) and handles crash recovery.
- `prom_proxy.py` — FastAPI router that resolves `/runs/{id}/prom/*` to
  whichever container is currently serving that run, lazy-booting archive
  containers with an idle TTL.
- `ipc.py` — the WS route; see "IPC" below.
- `api.py` — assembles all routers into one FastAPI app.
- `daemon.py` — lifecycle orchestrator: start Grafana, start uvicorn on
  both TCP and UDS, run until shutdown, archive everything cleanly on exit.
- `client.py` — harness-side client (`DashboardClient`) that opens the WS
  and keeps it alive with heartbeats.
- `storage.py` / `sqlite_storage.py` — on-disk SQLite at
  `.dashboard/runs.db`, single source of truth for run lifecycle.
- `config.py` — `DaemonConfig` (host, ports, Prometheus port range).
  Config is loaded once at startup; edits require a restart.

## Lifecycle & IPC

Test harness code:

```python
async with Network(install, working_dir, enable_dashboard=True) as network:
    ...
    await network.register_with_dashboard()  # after nodes are launched
```

Under the hood `DashboardClient` opens `ws+unix:<socket>:/runs/<id>`, sends
a register handshake with `TestMetadata`, and receives URLs back. Every
10 s it sends a heartbeat. The daemon moves the run to `DORMANT` when:

- the client closes the WS cleanly, or
- the heartbeat times out after 30 s, or
- the daemon itself shuts down.

Data stays queryable either way — the same URL pattern
(`/runs/{id}/prom/...`) serves both live and dormant runs; dormant ones
are lazy-booted from their data directory on demand.

## Crash recovery

Every persistent write is ordered: filesystem → SQLite → podman. On
startup the daemon reconciles all three:

- DB row `LIVE` with or without a running container → flip to `DORMANT`
  (no daemon ⇒ no active WS ⇒ nobody holds the run). Data is preserved
  and remains queryable via lazy-boot.
- Running `tontester-prom-*` container → stop it; lazy-boot recreates
  fresh when a query arrives.
- Datasource YAML without a DB row → delete it.

## CLI

```sh
uv run daemon start              # foreground, lifetime bound to terminal
uv run daemon start --daemonize  # background, survives shell exit
uv run daemon status             # hits GET /health over the UDS socket
uv run daemon info               # prints URLs and socket path
uv run daemon stop               # POST /admin/shutdown over the UDS socket
```

`start` (both modes) wraps the real daemon in a transient systemd user
unit via ``systemd-run``, so:

- Ctrl+C / SIGTERM / SIGHUP (terminal close) → clean graceful shutdown.
- SIGKILL / OOM / hard terminal death → systemd tears down the whole
  cgroup atomically, so podman children die with the daemon and no
  orphan containers survive.

This requires a working ``systemd --user`` session; on boxes without it,
``daemon start`` refuses to run with a clear error. `daemon status` /
`daemon stop` / `daemon info` do not depend on systemd — they talk to
the daemon over its UDS socket.

To follow logs of a detached daemon:

```sh
journalctl --user -fu 'tontester-dashboard-*'
```

## Default paths

```
test/integration/.dashboard/
├── config.json         # DaemonConfig
├── daemon.sock         # UDS (FastAPI + WS)
├── runs.db             # SQLite: run lifecycle
├── runs/<run_id>/
│   ├── data/           # Prometheus TSDB (live + archive)
│   └── config/         # prometheus.yml + targets.json
└── grafana/
    ├── data/           # Grafana state
    └── provisioning/   # datasources (per-run YAML) + dashboards
```

## Default config

```json
{
  "host": "127.0.0.1",
  "dashboard_port": 8080,
  "grafana_port": 3000,
  "prometheus_port_range": [9100, 9499]
}
```

## View metrics

- Dashboard: http://127.0.0.1:8080
- Grafana:   http://127.0.0.1:3000 (anonymous admin)

Every panel queries `${datasource}`, which resolves to the run you pick
from the dropdown. The dashboard has no `run_id` label matchers anywhere.
