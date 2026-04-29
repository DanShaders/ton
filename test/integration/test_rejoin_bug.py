# pyright: reportPrivateUsage=false
"""
Reproduces the post-isolation shard block generation failure described in
bug bounty report 1675/1676.

Setup: 4-validator local network. We isolate node1's UDP traffic for ~25s,
then restore it. Catchain lifetimes are pinned to absurdly large values so
the validator group cannot rotate out of the broken session — the node
truly cannot recover.

Architecture
------------
* The PARENT process stays in the host network namespace and runs an HTTP +
  Server-Sent-Events server on http://127.0.0.1:8080/ that streams a live
  dashboard (Plotly charts) to the browser.
* It spawns a CHILD subprocess via `unshare -Urn` so the validators run in
  an isolated user+net namespace where we own the firewall and can `nft
  drop` UDP packets to/from a chosen port without root.
* The child writes one JSON line per stats poll to its stdout; the parent
  forwards each line to all connected browsers.
"""

import asyncio
import json
import logging
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

from tonapi import ton_api
from tontester.install import Install
from tontester.network import FullNode, Network, StartOptions
from tontester.zerostate import SimplexConsensusConfig

# Re-exported so it stays imported for ad-hoc tweaks below (e.g. switching
# network.config.shard_consensus to SimplexConsensusConfig(...)).
__all__ = ["SimplexConsensusConfig"]

_NS_READY_ENV = "TON_REJOIN_TEST_NS_READY"
_CHILD_FLAG = "--child"


def _nft(*args: str):
    _ = subprocess.run(["nft", *args], check=True)


def _isolate_udp_ports(ports: list[int]):
    _nft("add", "table", "inet", "isolate")
    _nft(
        "add",
        "chain",
        "inet",
        "isolate",
        "input",
        "{ type filter hook input priority 0; }",
    )
    _nft(
        "add",
        "chain",
        "inet",
        "isolate",
        "output",
        "{ type filter hook output priority 0; }",
    )
    port_set = "{" + ",".join(str(p) for p in ports) + "}"
    _nft("add", "rule", "inet", "isolate", "input", "udp", "sport", port_set, "drop")
    _nft("add", "rule", "inet", "isolate", "input", "udp", "dport", port_set, "drop")
    _nft("add", "rule", "inet", "isolate", "output", "udp", "sport", port_set, "drop")
    _nft("add", "rule", "inet", "isolate", "output", "udp", "dport", port_set, "drop")


def _heal():
    _nft("delete", "table", "inet", "isolate")


_MC_SEQNO_RE = re.compile(r"\(-?\d+,[\da-fA-F]+,(\d+)\)")


def _parse_seqno(block_id: str) -> int | None:
    m = _MC_SEQNO_RE.search(block_id)
    if m is None:
        return None
    return int(m.group(1))


def _to_int(s: str | None) -> int | None:
    if s is None or not s.isdigit():
        return None
    return int(s)


def _fmt(v: int | None, width: int) -> str:
    return f"{v:>{width}}" if v is not None else "?".rjust(width)


async def _snapshot(node: FullNode) -> dict[str, str]:
    try:
        request = ton_api.Engine_validator_getStatsRequest()
        raw = await asyncio.wait_for(node.engine_console.request(request), timeout=5.0)
        stats = request.parse_result(raw)
    except asyncio.TimeoutError, Exception:
        return {}
    return {s.key: s.value for s in stats.stats}


async def _find_shard_validator(nodes: list[FullNode]) -> int:
    """Pick the node currently in a shard valgroup.

    With shard_validators=1 only one of the four nodes is actually collating
    the basechain at any moment; isolating any of the other three would not
    reproduce the catchain-state divergence.
    """
    deadline = time.monotonic() + 30.0
    while time.monotonic() < deadline:
        snaps = await asyncio.gather(*(_snapshot(n) for n in nodes))
        for i, kv in enumerate(snaps):
            avg = kv.get("active_validator_groups", "")
            m = re.search(r"shard:(\d+)", avg)
            if m and int(m.group(1)) > 0:
                return i
        await asyncio.sleep(0.5)
    raise RuntimeError("no node reports an active shard validator group")


def _emit_metric(payload: dict[str, object]):
    # JSON line on stdout — read by the parent process and forwarded to browsers.
    print(json.dumps(payload), flush=True)


_BASECHAIN_TOP_KEY = "shardclient.shard.(0,8000000000000000).seqno"


async def _monitor_sync(nodes: list[FullNode], log: logging.Logger, phase: list[str]):
    start = time.monotonic()
    while True:
        snapshots = await asyncio.gather(*(_snapshot(n) for n in nodes))
        elapsed = time.monotonic() - start
        cells: list[str] = []
        per_node: list[dict[str, int | None]] = []
        for i, kv in enumerate(snapshots):
            mc = _parse_seqno(kv.get("masterchainblock", ""))
            wc = _to_int(kv.get(_BASECHAIN_TOP_KEY))
            col_sh = kv.get("total.collated_blocks.shard", "")
            m_ok = re.search(r"ok:(\d+)", col_sh)
            m_err = re.search(r"error:(\d+)", col_sh)
            ok = int(m_ok.group(1)) if m_ok else None
            err = int(m_err.group(1)) if m_err else None
            cells.append(
                f"n{i}: mc={_fmt(mc, 3)} wc={_fmt(wc, 3)} col_sh={_fmt(ok, 0)}/{_fmt(err, 0)}"
            )
            per_node.append({"mc": mc, "wc": wc, "col_ok": ok, "col_err": err})
        log.info(f"[sync] t={elapsed:6.1f}s [{phase[0]:>10}]  " + "  ".join(cells))
        _emit_metric({"t": round(elapsed, 2), "phase": phase[0], "nodes": per_node})
        await asyncio.sleep(0.5)


async def _child_main():
    # We're already inside the unshared user+net namespace; bring up loopback so
    # the validators can talk to each other on 127.0.0.1.
    _ = subprocess.run(["ip", "link", "set", "lo", "up"], check=True)

    repo_root = Path(__file__).resolve().parents[2]
    working_dir = repo_root / "test/integration/.network-rejoin"
    shutil.rmtree(working_dir, ignore_errors=True)
    working_dir.mkdir(exist_ok=True)

    install = Install(repo_root / "build", repo_root)
    install.tonlibjson.client_set_verbosity_level(3)

    logging.basicConfig(
        level=logging.INFO,
        format="[%(levelname)s][%(asctime)s][%(name)s] %(message)s",
        datefmt="%Y-%m-%d %H-%M-%S",
        stream=sys.stderr,
    )
    l = logging.getLogger("test_rejoin_bug")

    async with Network(install, working_dir) as network:
        # All 4 validators participate in the shard catchain so node1 is guaranteed
        # to be in the shard valgroup whose state we want to break.
        # network.config.shard_validators = 4
        # Pin catchain lifetimes to a value the test will never reach so the
        # broken validator session cannot rotate out from under us.
        network.config.mc_valgroup_lifetime = 10**8
        network.config.mc_consensus = SimplexConsensusConfig(target_block_rate_ms=500)
        network.config.shard_valgroup_lifetime = 10**8
        network.config.shard_consensus = SimplexConsensusConfig(target_block_rate_ms=1500)

        dht = network.create_dht_node()

        nodes: list[FullNode] = []
        for _ in range(4):
            node = network.create_full_node()
            node.make_initial_validator()
            node.announce_to(dht)
            nodes.append(node)

        async with asyncio.TaskGroup() as start_group:
            _ = start_group.create_task(dht.run(StartOptions(console_verbosity=1)))
            for node in nodes:
                _ = start_group.create_task(node.run(StartOptions(console_verbosity=1)))

        l.info("Waiting for MC block")
        await network.wait_mc_block(seqno=1)
        l.info("Waiting for shard block")
        _ = await network.wait_block(workchain=0, shard=-(2**63), seqno=5)
        victim_idx = await _find_shard_validator(nodes)
        victim = nodes[victim_idx]
        l.info(f"Shard validator is node[{victim_idx}] ({victim.name}); isolating it")

        phase = ["warmup"]
        monitor = asyncio.create_task(_monitor_sync(nodes, l, phase))

        adnl_port = victim._addr.port
        # Simplex consensus uses QUIC on adnl_port + 1000 — drop both so the
        # isolation actually cuts the node off regardless of which transport is in use.
        quic_port = adnl_port + 1000
        ports = [adnl_port, quic_port]

        try:
            phase[0] = "isolated"
            _isolate_udp_ports(ports)
            try:
                l.info(f"{victim.name} UDP ports {ports} dropped; sleeping 25s")
                await asyncio.sleep(25)
            finally:
                _heal()
                l.info(f"{victim.name} firewall rules removed; observing recovery for 90s")

            phase[0] = "recovering"
            # Give the victim ample time to (try to) catch up. With the catchain
            # lifetimes we set, no rotation can mask the issue.
            await asyncio.sleep(1000)
        finally:
            _ = monitor.cancel()
            try:
                await monitor
            except asyncio.CancelledError:
                pass

        victim_log = victim.log_path.read_text(errors="replace")

        bug_signature = "unregistered chain of length"
        occurrences = victim_log.count(bug_signature)
        if occurrences:
            err_msg = (
                f"BUG REPRODUCED: {victim.name} log contains {occurrences}"
                f" occurrences of {bug_signature!r}"
            )
            l.error(err_msg)
        # Regression assertion: this should hold once the bug is fixed. While the
        # bug is present, the test fails here, demonstrating the repro.
        msg = (
            f"{victim.name} emitted {occurrences} 'unregistered chain of length'"
            " errors after rejoining the network — shard block generation is stuck."
        )
        assert occurrences == 0, msg


# --------------------------------------------------------------------------- #
# Parent: HTTP + SSE dashboard
# --------------------------------------------------------------------------- #

_DASHBOARD_HTML = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>TON rejoin bug — sync</title>
<script src="https://cdn.plot.ly/plotly-2.32.0.min.js"></script>
<style>
  html, body { margin: 0; padding: 0; background: #0e0e10; color: #e6e6e6;
    font-family: ui-monospace, Menlo, Monaco, Consolas, monospace; }
  header { padding: 12px 18px; border-bottom: 1px solid #2a2a30; display: flex;
    align-items: center; gap: 24px; flex-wrap: wrap; }
  header h1 { margin: 0; font-size: 16px; font-weight: 600; }
  .phase { padding: 4px 10px; border-radius: 4px; background: #2a2a30; }
  .phase.warmup { background: #2d4a2d; }
  .phase.isolated { background: #6a2a2a; }
  .phase.recovering { background: #6a572a; }
  .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; padding: 8px; }
  .chart { background: #14141a; border-radius: 6px; padding: 6px; height: 360px; }
  footer { padding: 8px 18px; color: #888; font-size: 12px;
    border-top: 1px solid #2a2a30; }
  @media (max-width: 1100px) { .grid { grid-template-columns: 1fr; } }
</style>
</head>
<body>
<header>
  <h1>TON rejoin bug — live sync stats</h1>
  <span>phase: <span id="phase" class="phase">connecting…</span></span>
  <span>t: <span id="t">—</span></span>
</header>
<div class="grid">
  <div id="mc" class="chart"></div>
  <div id="wc" class="chart"></div>
  <div id="col_ok" class="chart"></div>
  <div id="col_err" class="chart"></div>
</div>
<footer>
  n1 is the isolated validator. mc = masterchain seqno seen by node;
  wc = basechain top seqno as seen by the shard client; col_ok/col_err = cumulative shard
  collations.
</footer>
<script>
const N = 4;
const COLORS = ['#5fa8d3', '#e06c75', '#7bdc8b', '#ffa451'];
const charts = ['mc', 'wc', 'col_ok', 'col_err'];
const titles = {
  mc: 'masterchain seqno',
  wc: 'basechain top seqno (shard-client view)',
  col_ok: 'shard collations OK (cumulative)',
  col_err: 'shard collation errors (cumulative)',
};
function blankTraces() {
  return Array.from({length: N}, (_, i) => ({
    x: [], y: [], name: 'n' + i, mode: 'lines',
    line: {color: COLORS[i], width: 2},
  }));
}
function layout(title) {
  return {
    title: {text: title, font: {color: '#e6e6e6', size: 14}, x: 0, xanchor: 'left'},
    paper_bgcolor: '#14141a', plot_bgcolor: '#14141a',
    font: {color: '#bbb'},
    margin: {t: 32, r: 12, b: 36, l: 40},
    legend: {orientation: 'h', y: -0.22},
    xaxis: {gridcolor: '#2a2a30', zerolinecolor: '#2a2a30', title: 't (s)'},
    yaxis: {gridcolor: '#2a2a30', zerolinecolor: '#2a2a30'},
  };
}
for (const c of charts) {
  Plotly.newPlot(c, blankTraces(), layout(titles[c]),
                 {displayModeBar: false, responsive: true});
}

const phaseEl = document.getElementById('phase');
const tEl = document.getElementById('t');
const lastPhase = {value: null, t: null};

function applyEvent(d) {
  phaseEl.textContent = d.phase;
  phaseEl.className = 'phase ' + d.phase;
  tEl.textContent = d.t.toFixed(1) + 's';
  if (lastPhase.value !== d.phase) {
    // Drop a vertical line at every phase transition so the chart shows
    // when isolation/recovery began.
    if (lastPhase.value !== null) {
      for (const c of charts) {
        const layoutUpdate = {
          shapes: ((Plotly.relayout, document.getElementById(c).layout.shapes) || []).concat([{
            type: 'line', x0: d.t, x1: d.t, yref: 'paper', y0: 0, y1: 1,
            line: {color: '#aaa', width: 1, dash: 'dot'},
          }]),
        };
        Plotly.relayout(c, layoutUpdate);
      }
    }
    lastPhase.value = d.phase;
    lastPhase.t = d.t;
  }
  for (let i = 0; i < N; i++) {
    const n = (d.nodes && d.nodes[i]) || {};
    if (n.mc != null) Plotly.extendTraces('mc', {x: [[d.t]], y: [[n.mc]]}, [i]);
    if (n.wc != null) Plotly.extendTraces('wc', {x: [[d.t]], y: [[n.wc]]}, [i]);
    if (n.col_ok != null) Plotly.extendTraces('col_ok', {x: [[d.t]], y: [[n.col_ok]]}, [i]);
    if (n.col_err != null) Plotly.extendTraces('col_err', {x: [[d.t]], y: [[n.col_err]]}, [i]);
  }
}

const ev = new EventSource('/events');
ev.onmessage = (e) => { try { applyEvent(JSON.parse(e.data)); } catch (_) {} };
ev.onerror = () => { phaseEl.textContent = '(disconnected)'; };
</script>
</body>
</html>
"""


async def _serve_dashboard(
    host: str,
    port: int,
    history: list[str],
    clients: set["asyncio.Queue[str | None]"],
):
    async def handle(reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
        try:
            request_line = await reader.readline()
            while True:
                hdr = await reader.readline()
                if hdr in (b"\r\n", b""):
                    break
            parts = request_line.decode("latin1").split()
            if len(parts) < 2:
                return
            path = parts[1]
            if path == "/" or path == "/index.html":
                body = _DASHBOARD_HTML.encode()
                headers = (
                    f"HTTP/1.1 200 OK\r\n"
                    f"Content-Type: text/html; charset=utf-8\r\n"
                    f"Content-Length: {len(body)}\r\n\r\n"
                ).encode()
                writer.write(headers + body)
                await writer.drain()
            elif path == "/events":
                headers = (
                    b"HTTP/1.1 200 OK\r\n"
                    b"Content-Type: text/event-stream\r\n"
                    b"Cache-Control: no-cache\r\n"
                    b"Connection: keep-alive\r\n\r\n"
                )
                writer.write(headers)
                await writer.drain()
                q: asyncio.Queue[str | None] = asyncio.Queue()
                clients.add(q)
                try:
                    # Replay history so a freshly-opened tab sees the full timeline.
                    for h in history:
                        writer.write(f"data: {h}\n\n".encode())
                    await writer.drain()
                    while True:
                        msg = await q.get()
                        if msg is None:
                            break
                        writer.write(f"data: {msg}\n\n".encode())
                        await writer.drain()
                finally:
                    clients.discard(q)
            else:
                writer.write(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n")
                await writer.drain()
        except ConnectionResetError, BrokenPipeError:
            pass
        finally:
            try:
                writer.close()
                await writer.wait_closed()
            except Exception:
                pass

    return await asyncio.start_server(handle, host, port)


async def _parent_main() -> int:
    host = os.environ.get("TON_REJOIN_BUG_HOST", "127.0.0.1")
    port = int(os.environ.get("TON_REJOIN_BUG_PORT", "8080"))

    history: list[str] = []
    clients: set["asyncio.Queue[str | None]"] = set()

    server = await _serve_dashboard(host, port, history, clients)

    print(f"\n  ➜  open http://{host}:{port}/ to view live charts\n", flush=True)

    new_env = {**os.environ, _NS_READY_ENV: "1"}
    child = await asyncio.create_subprocess_exec(
        "unshare",
        "-Urn",
        "--",
        sys.executable,
        sys.argv[0],
        _CHILD_FLAG,
        env=new_env,
        stdout=asyncio.subprocess.PIPE,
    )

    async def stream_metrics():
        assert child.stdout is not None
        async for raw in child.stdout:
            line = raw.decode("utf-8", errors="replace").strip()
            if not line or not line.startswith("{"):
                continue
            history.append(line)
            if len(history) > 20000:
                del history[:1000]
            for q in list(clients):
                try:
                    q.put_nowait(line)
                except asyncio.QueueFull:
                    pass

    streamer = asyncio.create_task(stream_metrics())

    rc: int
    try:
        rc = await child.wait()
    except KeyboardInterrupt, asyncio.CancelledError:
        try:
            child.terminate()
        except ProcessLookupError:
            pass
        rc = await child.wait()

    # Drain any remaining child output, then signal SSE clients to close.
    try:
        await asyncio.wait_for(streamer, timeout=2.0)
    except asyncio.TimeoutError:
        _ = streamer.cancel()
    for q in list(clients):
        try:
            q.put_nowait(None)
        except asyncio.QueueFull:
            pass

    server.close()
    try:
        await asyncio.wait_for(server.wait_closed(), timeout=3.0)
    except asyncio.TimeoutError:
        pass

    return rc


# --------------------------------------------------------------------------- #
# Entry point
# --------------------------------------------------------------------------- #


if __name__ == "__main__":
    if _CHILD_FLAG in sys.argv:
        asyncio.run(asyncio.wait_for(_child_main(), 30 * 60))
    else:
        sys.exit(asyncio.run(_parent_main()))
