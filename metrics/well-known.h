/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include "collectors.h"
#include "tl-traffic-bucket.h"

namespace ton::metrics {

#define DIRECTION_LIST(F) \
  F(in)                   \
  F(out)
TON_METRIC_DEFINE_LABEL(Direction, "direction", DIRECTION_LIST)
#undef DIRECTION_LIST

#define KIND_LIST(F) \
  F(message)         \
  F(query)           \
  F(answer)
TON_METRIC_DEFINE_LABEL(Kind, "kind", KIND_LIST)
#undef KIND_LIST

#define STATELESS_DROP_REASON_LIST(F) \
  F(invalid)                          \
  F(limited)
TON_METRIC_DEFINE_LABEL(StatelessDropReason, "reason", STATELESS_DROP_REASON_LIST)
#undef STATELESS_DROP_REASON_LIST

#define CONNECTION_CLOSE_REASON_LIST(F) \
  F(completed)                          \
  F(idle)                               \
  F(protocol)                           \
  F(internal)
TON_METRIC_DEFINE_LABEL(ConnectionCloseReason, "reason", CONNECTION_CLOSE_REASON_LIST)
#undef CONNECTION_CLOSE_REASON_LIST

#define HANDSHAKE_FAILURE_REASON_LIST(F) \
  F(unresolved)                          \
  F(timeout)                             \
  F(rejected)
TON_METRIC_DEFINE_LABEL(HandshakeFailureReason, "reason", HANDSHAKE_FAILURE_REASON_LIST)
#undef HANDSHAKE_FAILURE_REASON_LIST

#define TRANSFER_OUTCOME_LIST(F) \
  F(completed)                   \
  F(timeout)                     \
  F(failed)                      \
  F(too_big)                     \
  F(invalid)
TON_METRIC_DEFINE_LABEL(TransferOutcome, "outcome", TRANSFER_OUTCOME_LIST)
#undef TRANSFER_OUTCOME_LIST

struct BytesAndPackets {
  Counter bytes;
  Counter packets;

  void record(size_t size) {
    bytes.inc(size);
    packets.inc();
  }

  BytesAndPackets &operator+=(const BytesAndPackets &other) {
    bytes += other.bytes;
    packets += other.packets;
    return *this;
  }

  void collect(Context ctx) const {
    ctx.collect(bytes, "bytes");
    ctx.collect(packets, "packets");
  }
};

struct UdpDirStats {
  BytesAndPackets data;
  Counter syscalls;
  Counter dropped;

  UdpDirStats &operator+=(const UdpDirStats &other) {
    data += other.data;
    syscalls += other.syscalls;
    dropped += other.dropped;
    return *this;
  }

  void collect(Context ctx) const {
    ctx.collect(data);
    ctx.collect(syscalls, "syscalls");
    ctx.collect(dropped, "dropped");
  }
};

struct UdpWireStats {
  Labeled<UdpDirStats, Direction> dir;
  Gauge<td::uint64> listening_sockets;

  void combine(const UdpWireStats &other) {
    dir += other.dir;
    listening_sockets.set(listening_sockets.value() + other.listening_sockets.value());
  }

  void collect(Context ctx) const {
    ctx.collect(dir);
    ctx.collect(listening_sockets, "listening_sockets");
  }
};

struct ConnectionStats {
  Gauge<td::uint64> active;

  // Connection failed to be established.
  Labeled<Counter, HandshakeFailureReason> failed;

  // Connection was closed after being successfully established.
  Labeled<Counter, ConnectionCloseReason> closed;

  void collect(Context ctx) const {
    ctx.collect(active, "active");
    ctx.collect(failed, "failed");
    ctx.collect(closed, "closed");
  }
};

// The data ladder this struct lives in; each branch is bytes peeling off the main flow
// (→ dropped, − framing/overhead stripped):
//
//   | → kernel queue overflow (wire_udp_dropped)
//  wire_{bytes,packets}
//   | → stateless (transport_stalessly_dropped_{bytes,packets})
//   | → stateful
//  transport_useful_{bytes,packets}
//   | − protocol overhead
//  transport_stream_bytes
//   | − ADNL sender wrapping
//  app_{bytes,messages}
struct TransportStatsBase {
  Counter internal_errors;

  Labeled<BytesAndPackets, StatelessDropReason> stateless_dropped;
  Labeled<BytesAndPackets, Direction> useful;
  Labeled<Counter, Direction> stream_bytes;

  void collect(Context ctx) const {
    ctx.collect(internal_errors, "internal_errors");
    ctx.collect(stateless_dropped, "stateless_dropped");
    ctx.collect(useful, "useful");
    ctx.collect(stream_bytes, "stream_bytes");
  }
};

struct TransferStats {
  Labeled<Counter, TransferOutcome> inactive;
  Gauge<td::uint64> active;
  Gauge<td::uint64> queries_active;

  void collect(Context ctx) const {
    ctx.collect(inactive, "inactive");
    ctx.collect(active, "active");
    ctx.collect(queries_active, "queries_active");
  }
};

struct App : Labeled<TlTrafficBucket, Kind, Direction> {};

}  // namespace ton::metrics
