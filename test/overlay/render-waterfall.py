#!/usr/bin/env python3
"""Render a per-node waterfall from a twostep-broadcast bench log.

Reads stderr produced by bench-broadcast-twostep run with -v 2, attributes each
twostep log line to its emitting node via the `local=<adnl_short>` tag (added to
the VLOG sites in overlay/broadcast-twostep.cpp), and writes a self-contained
HTML+SVG waterfall.

Usage:
  bench-broadcast-twostep ... -v 2 2> bench.log
  python3 test/overlay/render-waterfall.py bench.log [out.html]

Filled rectangles span ENCODE_BEGIN→ENCODE_END (sender) and DECODE_BEGIN→DECODE_END
(receiver) — wall-clock cost of the RaptorQ encode/decode call.
Dots: orange = sender events (START, SEND_CHUNK), blue = receiver events (START,
RECV_CHUNK), green = FINISH. Hover any dot for seqno / from / to.
"""

import re
import sys
from datetime import datetime
from pathlib import Path

if len(sys.argv) < 2 or len(sys.argv) > 3:
    print("usage: render-waterfall.py <bench.log> [out.html]", file=sys.stderr)
    sys.exit(2)

LOG = Path(sys.argv[1])
OUT = Path(sys.argv[2]) if len(sys.argv) == 3 else LOG.with_suffix(".html")

KV = re.compile(r"([A-Za-z_]+)=(\S+)")
ANSI = re.compile(r"\x1b\[[0-9;]*m")
LINE = re.compile(
    r"\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+)\].*?twostep (\S+) (\S+) (.+)$"
)


def parse_ts(s: str) -> float:
    return datetime.fromisoformat(s).timestamp()


events = []
for raw in LOG.read_text().splitlines():
    raw = ANSI.sub("", raw).replace("#011", "\t")
    m = LINE.search(raw)
    if not m:
        continue
    ts = parse_ts(m.group(1))
    ev = {"ts": ts, "event": m.group(2), "role": m.group(3)}
    for k, v in KV.findall(m.group(4)):
        ev[k] = v
    events.append(ev)

if not events:
    print("no twostep events parsed; remember to run the bench with -v 2", file=sys.stderr)
    sys.exit(1)

t0 = min(e["ts"] for e in events)
for e in events:
    e["t_ms"] = (e["ts"] - t0) * 1000.0

# Discover nodes via `local=` tag; sender (whoever emitted START sender) goes to row 0.
nodes_by_id: dict[str, int] = {}
for e in events:
    lid = e.get("local")
    if lid and lid not in nodes_by_id:
        nodes_by_id[lid] = len(nodes_by_id)

sender_id = next(
    (e["local"] for e in events if e["event"] == "START" and e["role"] == "sender"),
    None,
)
order = (
    [sender_id] + [nid for nid in nodes_by_id if nid != sender_id]
    if sender_id
    else list(nodes_by_id)
)
node_idx = {nid: i for i, nid in enumerate(order)}
n_nodes = len(order)

per_node: list[list[dict]] = [[] for _ in range(n_nodes)]
for e in events:
    lid = e.get("local")
    if lid in node_idx:
        per_node[node_idx[lid]].append(e)


def find_one(evs, **kw):
    for e in evs:
        if all(e.get(k) == v for k, v in kw.items()):
            return e
    return None


summaries = []
for evs in per_node:
    s = {"role": "—"}
    start_send = find_one(evs, event="START", role="sender")
    start_recv = find_one(evs, event="START", role="receiver")
    enc_begin = find_one(evs, event="ENCODE_BEGIN")
    enc_end = find_one(evs, event="ENCODE_END")
    dec_begin = find_one(evs, event="DECODE_BEGIN")
    dec_end = find_one(evs, event="DECODE_END")
    if start_send or enc_begin:
        s["role"] = "sender"
        if enc_begin and enc_end:
            s["work_start"] = enc_begin["t_ms"]
            s["work_end"] = enc_end["t_ms"]
            s["work_label"] = "encode"
    if start_recv:
        s["role"] = "receiver"
        if dec_begin and dec_end:
            s["work_start"] = dec_begin["t_ms"]
            s["work_end"] = dec_end["t_ms"]
            s["work_label"] = "decode"
    summaries.append(s)

t_min = min(e["t_ms"] for e in events)
t_max = max(e["t_ms"] for e in events)
span = max(t_max - t_min, 1.0)

ROW_H = 36
PAD_L = 110
PAD_R = 110
PAD_T = 50
PAD_B = 40
W = 1280
H = PAD_T + PAD_B + n_nodes * ROW_H
inner_w = W - PAD_L - PAD_R


def sx(t):
    return PAD_L + (t - t_min) / span * inner_w


def color_for_event(e):
    if e["event"] == "START" and e["role"] == "sender":
        return "#ffb454"
    if e["event"] == "SEND_CHUNK":
        return "#ffb454"
    if e["event"] == "START" and e["role"] == "receiver":
        return "#6aa9ff"
    if e["event"] == "RECV_CHUNK":
        return "#6aa9ff"
    if e["event"] == "FINISH":
        return "#5dd39e"
    if e["event"] in ("ENCODE_BEGIN", "ENCODE_END", "DECODE_BEGIN", "DECODE_END"):
        return None
    return "#8a93a8"


svg = [
    f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
    'style="font-family: ui-sans-serif, system-ui, sans-serif; background:#0f1115; color:#e6e9ef">'
]
svg.append(f'<rect width="{W}" height="{H}" fill="#0f1115"/>')
svg.append(
    f'<text x="{PAD_L}" y="22" fill="#e6e9ef" font-size="14" font-weight="600">'
    f"twostep broadcast waterfall — {n_nodes} nodes, span={span:.1f} ms</text>"
)
svg.append(
    f'<text x="{PAD_L}" y="38" fill="#8a93a8" font-size="11">'
    "rect = encode/decode duration · dot = SEND/RECV/FINISH event · ms relative to first event</text>"
)

n_ticks = 8
for i in range(n_ticks + 1):
    t = t_min + span * i / n_ticks
    x = sx(t)
    svg.append(
        f'<line x1="{x}" x2="{x}" y1="{PAD_T-4}" y2="{H-PAD_B+4}" stroke="#1d2230" stroke-dasharray="2,3"/>'
    )
    svg.append(
        f'<text x="{x}" y="{H-PAD_B+18}" fill="#8a93a8" font-size="10" text-anchor="middle">{t:.1f}ms</text>'
    )

for i in range(n_nodes):
    y = PAD_T + i * ROW_H + ROW_H / 2
    svg.append(f'<line x1="{PAD_L}" x2="{W-PAD_R}" y1="{y}" y2="{y}" stroke="#2a3142"/>')
    role = summaries[i].get("role", "—")
    short = order[i][:6] + "…" if i < len(order) and order[i] else ""
    label_color = "#ffb454" if role == "sender" else "#6aa9ff"
    svg.append(
        f'<text x="{PAD_L-8}" y="{y+4}" text-anchor="end" fill="{label_color}" font-size="11" '
        f'font-family="ui-monospace, Menlo">node {i} · {role}</text>'
    )
    svg.append(
        f'<text x="{PAD_L-8}" y="{y-8}" text-anchor="end" fill="#8a93a8" font-size="9" '
        f'font-family="ui-monospace, Menlo">{short}</text>'
    )

for i, s in enumerate(summaries):
    if "work_start" not in s:
        continue
    y_mid = PAD_T + i * ROW_H + ROW_H / 2
    rect_y = y_mid - 9
    rect_h = 18
    color = "#ffb454" if s["role"] == "sender" else "#6aa9ff"
    x1, x2 = sx(s["work_start"]), sx(s["work_end"])
    dur = s["work_end"] - s["work_start"]
    svg.append(
        f'<rect x="{x1}" y="{rect_y}" width="{max(x2-x1, 2)}" height="{rect_h}" '
        f'fill="{color}" fill-opacity="0.55" stroke="{color}" stroke-opacity="0.9"/>'
    )
    svg.append(
        f'<text x="{x1+3}" y="{rect_y-3}" fill="{color}" font-size="9">{s["work_label"]} ({dur:.1f}ms)</text>'
    )

for i, evs in enumerate(per_node):
    y = PAD_T + i * ROW_H + ROW_H / 2
    for e in evs:
        c = color_for_event(e)
        if c is None:
            continue
        x = sx(e["t_ms"])
        r = 5 if e["event"] == "FINISH" else (4 if e["event"] == "START" else 3)
        tip = f'{e["event"]} {e["role"]} t={e["t_ms"]:.2f}ms'
        if "seqno" in e:
            tip += f' seqno={e["seqno"]}'
        if "from" in e:
            tip += f' from={e["from"][:10]}…'
        if "to" in e:
            tip += f' to={e["to"][:10]}…'
        svg.append(
            f'<circle cx="{x}" cy="{y}" r="{r}" fill="{c}" stroke="#0f1115" stroke-width="0.5">'
            f"<title>{tip}</title></circle>"
        )

lx = PAD_L
ly = H - 4
svg.append('<g font-size="10">')
for color, label in [
    ("#ffb454", "sender: START / SEND_CHUNK (filled = encode)"),
    ("#6aa9ff", "receiver: START / RECV_CHUNK (filled = decode)"),
    ("#5dd39e", "FINISH"),
]:
    svg.append(f'<circle cx="{lx}" cy="{ly}" r="4" fill="{color}"/>')
    svg.append(f'<text x="{lx+10}" y="{ly+3}" fill="#8a93a8">{label}</text>')
    lx += 380
svg.append("</g>")
svg.append("</svg>")

newline = "\n"
html = (
    "<!doctype html><html><head><meta charset=\"utf-8\"><title>twostep waterfall</title>"
    "<style>body{margin:0;padding:8px;background:#0f1115;color:#e6e9ef;"
    "font-family:ui-sans-serif,system-ui,sans-serif}</style></head><body>"
    f"{newline.join(svg)}"
    "</body></html>"
)
OUT.write_text(html)
print(
    f"wrote {OUT}  ({len(events)} events, {n_nodes} nodes, span={span:.1f}ms)",
    file=sys.stderr,
)
