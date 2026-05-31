/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include "collectors.h"
#include "tl-traffic-bucket.h"

namespace ton::metrics {

#define TON_METRIC_DIRECTION_LIST(F) \
  F(in)                              \
  F(out)
TON_METRIC_DEFINE_LABEL(Direction, "direction", TON_METRIC_DIRECTION_LIST)
#undef TON_METRIC_DIRECTION_LIST

// Drop/reject reason class, shared across every transport. A reason exists only if an operator
// would respond differently: `invalid` (unparseable / failed signature / misaddressed input),
// `limited` (refused by a local size/rate/queue cap), `internal` (our own machinery failed).
#define TON_METRIC_REASON_LIST(F) \
  F(invalid)                      \
  F(limited)                      \
  F(internal)
TON_METRIC_DEFINE_LABEL(Reason, "reason", TON_METRIC_REASON_LIST)
#undef TON_METRIC_REASON_LIST

// Application message kind for the app traffic tier.
#define TON_METRIC_KIND_LIST(F) \
  F(message)                    \
  F(query)                      \
  F(answer)
TON_METRIC_DEFINE_LABEL(Kind, "kind", TON_METRIC_KIND_LIST)
#undef TON_METRIC_KIND_LIST

struct App {
  // bytes_total{kind, direction, tl}, messages_total{kind, direction, tl}
  Labeled<TlTrafficBucket, Kind, Direction> traffic;
  // dropped_total{direction, reason}
  Labeled<Counter<"dropped">, Direction, Reason> dropped;

  void record(Kind kind, Direction direction, td::Slice payload) {
    traffic.at(kind, direction).account(payload);
  }
  void record_dropped(Direction direction, Reason reason) {
    dropped.at(direction, reason).inc();
  }

  App &operator+=(const App &other) {
    traffic += other.traffic;
    dropped += other.dropped;
    return *this;
  }

  void collect(td::Badge<Context>, Context ctx) const {
    ctx.collect(traffic);
    ctx.collect(dropped);
  }
};

// Raw UDP socket counters for a single direction; the direction label is applied by the enclosing
// Labeled<UdpDirStats, Direction> in UdpWireStats.
struct UdpDirStats {
  Counter<"bytes"> bytes;
  Counter<"packets"> packets;
  Counter<"syscalls"> syscalls;
  // udp_dropped_total{reason}; direction comes from the outer Labeled. `limited` here means the kernel
  // dropped datagrams on a full socket queue (RX overflow / TX ENOBUFS).
  Labeled<Counter<"dropped">, Reason> dropped;

  UdpDirStats &operator+=(const UdpDirStats &other) {
    bytes += other.bytes;
    packets += other.packets;
    syscalls += other.syscalls;
    dropped += other.dropped;
    return *this;
  }

  void collect(td::Badge<Context>, Context ctx) const {
    ctx.collect(bytes);
    ctx.collect(packets);
    ctx.collect(syscalls);
    ctx.collect(dropped);
  }
};

// Wire tier shared by every UDP transport (adnl / quic), placed under a Prefixed<"wire", UdpWireStats>:
//   udp_bytes_total{direction}, udp_packets_total{direction}, udp_syscalls_total{direction},
//   udp_dropped_total{direction, reason}, listening_sockets (gauge).
struct UdpWireStats {
  Labeled<UdpDirStats, Direction> dir;
  Gauge<"listening_sockets"> listening_sockets;

  void record_dropped(Direction direction, Reason reason, td::uint64 delta = 1) {
    dir.at(direction).dropped.at(reason).inc(delta);
  }

  // Merge a per-socket/per-server snapshot into an aggregate. The listening_sockets gauge is summed
  // (each socket contributes its own count) rather than overwritten; this is deliberately a named
  // method, not operator+=, because gauges are not generally additive — only this count is.
  void combine(const UdpWireStats &other) {
    dir += other.dir;
    listening_sockets.set(listening_sockets.value() + other.listening_sockets.value());
  }

  void collect(td::Badge<Context>, Context ctx) const {
    ctx.with_name("udp").collect(dir);
    ctx.collect(listening_sockets);
  }
};

}  // namespace ton::metrics
