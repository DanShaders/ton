/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <chrono>

#include "metrics/well-known.h"

namespace ton::quic {

struct ConnectionStats {
  metrics::Labeled<metrics::Counter<"bytes">, metrics::Direction> bytes;
  metrics::Labeled<metrics::Counter<"packets">, metrics::Direction> packets;
  metrics::Labeled<metrics::Counter<"stream_bytes">, metrics::Direction> stream_bytes;
  metrics::Counter<"bytes_lost"> bytes_lost;
  metrics::Counter<"packets_lost"> packets_lost;
  metrics::Gauge<"bytes_in_flight", td::uint64> bytes_in_flight;
  metrics::Gauge<"bytes_unacked", td::uint64> bytes_unacked;
  metrics::Gauge<"bytes_unsent", td::uint64> bytes_unsent;
  metrics::Counter<"sids"> sids;
  metrics::Gauge<"sids_current", td::uint64> sids_current;
  metrics::Gauge<"mean_rtt", std::chrono::duration<double>> mean_rtt;

  void collect(td::Badge<metrics::Context>, metrics::Context ctx) const {
    ctx.collect(bytes);
    ctx.collect(packets);
    ctx.collect(stream_bytes);
    ctx.collect(bytes_lost);
    ctx.collect(packets_lost);
    ctx.collect(bytes_in_flight);
    ctx.collect(bytes_unacked);
    ctx.collect(bytes_unsent);
    ctx.collect(sids);
    ctx.collect(sids_current);
    ctx.collect(mean_rtt);
  }
};

struct ConnectionStatsAggregate {
  metrics::Counter<"connections"> connections;
  metrics::Gauge<"connections_current", td::uint64> connections_current;
  ConnectionStats stats;

  static ConnectionStatsAggregate from_one(const ConnectionStats& stats) {
    return {
        .connections = 1,
        .connections_current = 1,
        .stats = stats,
    };
  }

  void combine(const ConnectionStatsAggregate& other) {
    auto our = connections_current.value();
    auto their = other.connections_current.value();
    auto conns = our + their;
    std::chrono::duration<double> new_mean_rtt{};
    if (conns > 0) {
      new_mean_rtt = (our * stats.mean_rtt.value() + their * other.stats.mean_rtt.value()) / conns;
    }

    connections += other.connections;
    connections_current.add(other.connections_current.value());
    stats.bytes += other.stats.bytes;
    stats.packets += other.stats.packets;
    stats.stream_bytes += other.stats.stream_bytes;
    stats.bytes_lost += other.stats.bytes_lost;
    stats.packets_lost += other.stats.packets_lost;
    stats.bytes_in_flight.add(other.stats.bytes_in_flight.value());
    stats.bytes_unacked.add(other.stats.bytes_unacked.value());
    stats.bytes_unsent.add(other.stats.bytes_unsent.value());
    stats.sids += other.stats.sids;
    stats.sids_current.add(other.stats.sids_current.value());
    stats.mean_rtt.set(new_mean_rtt);
  }

  ConnectionStatsAggregate& retire() {
    connections_current = 0;
    stats.bytes_in_flight = {};
    stats.bytes_unacked = {};
    stats.bytes_unsent = {};
    stats.sids_current = {};
    stats.mean_rtt = {};
    return *this;
  }

  void collect(td::Badge<metrics::Context>, metrics::Context ctx) const {
    ctx.collect(connections);
    ctx.collect(connections_current);
    ctx.collect(stats);
  }
};

struct TransportStats {
  metrics::Labeled<metrics::Counter<"dropped">, metrics::Direction, metrics::Reason> dropped = {};

  TransportStats& operator+=(const TransportStats& other) {
    dropped += other.dropped;
    return *this;
  }

  void collect(td::Badge<metrics::Context>, metrics::Context ctx) const {
    ctx.collect(dropped);
  }
};

struct ServerStats {
  struct Transport {
    ConnectionStatsAggregate summary;
    TransportStats stats;

    void combine(const Transport& other) {
      summary.combine(other.summary);
      stats += other.stats;
    }

    void collect(td::Badge<metrics::Context>, metrics::Context ctx) const {
      ctx.collect(summary);
      ctx.collect(stats);
    }
  };

  metrics::Prefixed<"wire", metrics::UdpWireStats> wire;
  metrics::Prefixed<"transport", Transport> transport;

  void combine(const ServerStats& other) {
    wire->combine(*other.wire);
    transport->combine(*other.transport);
  }

  void collect(td::Badge<metrics::Context>, metrics::Context ctx) const {
    ctx.collect(wire);
    ctx.collect(transport);
  }
};

}  // namespace ton::quic
