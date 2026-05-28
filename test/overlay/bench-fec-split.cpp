/*
 * Stand-alone FEC split benchmark. Encodes a `--size` byte block with several
 * symbol sizes, then runs the decoder on the first K + slack chunks. Reports
 * encoder precalc / gen_symbol cost and decoder add_symbol + try_decode cost.
 *
 * The point: figure out the regime where td::raptorq is fast (≤10 ms encode +
 * decode for 8 MB) so we can pick a sensible K/part_size for two-step broadcast.
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "td/fec/raptorq/Decoder.h"
#include "td/fec/raptorq/Encoder.h"
#include "td/utils/OptionParser.h"
#include "td/utils/Random.h"
#include "td/utils/Time.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"

namespace {

struct RunResult {
  double encoder_create_ms = 0;
  double precalc_ms = 0;
  double gen_total_ms = 0;
  double decoder_create_ms = 0;
  double add_symbols_ms = 0;
  double try_decode_ms = 0;
  bool ok = false;
  size_t k = 0;
  size_t part_size = 0;
  size_t total_chunks = 0;
};

// `feed_offset` shifts the K symbols we feed the decoder; offset=0 means "feed
// 0..K-1" (all systematic — trivial decode), offset=K means "feed K..2K-1" (all
// repair — full solver). This is how we model "which subset of bundles the
// receiver collected first" in two-step broadcast.
RunResult bench_one(const td::BufferSlice &payload, size_t part_size, size_t overshoot_factor, size_t feed_offset,
                    bool verbose) {
  RunResult r;
  size_t data_size = payload.size();
  size_t k = (data_size + part_size - 1) / part_size;
  size_t total_chunks = k * overshoot_factor;
  r.k = k;
  r.part_size = part_size;
  r.total_chunks = total_chunks;

  // ---- Encoder ----
  auto t0 = td::Time::now();
  auto encR = td::raptorq::Encoder::create(part_size, payload.clone());
  auto t_after_create = td::Time::now();
  if (encR.is_error()) {
    if (verbose) std::fprintf(stderr, "  encoder create failed: %s\n", encR.error().message().c_str());
    return r;
  }
  auto encoder = encR.move_as_ok();
  r.encoder_create_ms = (t_after_create - t0) * 1000.0;

  auto t_precalc_begin = td::Time::now();
  encoder->precalc();
  auto t_precalc_end = td::Time::now();
  r.precalc_ms = (t_precalc_end - t_precalc_begin) * 1000.0;

  // Generate `total_chunks` symbols.
  td::BufferSlice generated(part_size * total_chunks);
  auto t_gen_begin = td::Time::now();
  for (size_t i = 0; i < total_chunks; ++i) {
    auto S = encoder->gen_symbol(static_cast<td::uint32>(i),
                                 generated.as_slice().substr(i * part_size, part_size));
    if (S.is_error()) {
      if (verbose) std::fprintf(stderr, "  gen_symbol(%zu) failed: %s\n", i, S.message().c_str());
      return r;
    }
  }
  auto t_gen_end = td::Time::now();
  r.gen_total_ms = (t_gen_end - t_gen_begin) * 1000.0;

  // ---- Decoder ----
  // Feed exactly K symbols starting at `feed_offset` (wrapping into [0, total_chunks)).
  size_t feed_count = k;
  if (feed_offset + feed_count > total_chunks) {
    feed_offset = total_chunks > feed_count ? total_chunks - feed_count : 0;
  }

  auto t_dec_begin = td::Time::now();
  auto decR = td::raptorq::Decoder::create({k, part_size, data_size});
  auto t_dec_create_end = td::Time::now();
  if (decR.is_error()) {
    if (verbose) std::fprintf(stderr, "  decoder create failed: %s\n", decR.error().message().c_str());
    return r;
  }
  auto decoder = decR.move_as_ok();
  r.decoder_create_ms = (t_dec_create_end - t_dec_begin) * 1000.0;

  auto t_add_begin = td::Time::now();
  for (size_t i = 0; i < feed_count; ++i) {
    size_t idx = feed_offset + i;
    td::BufferSlice sym =
        generated.from_slice(generated.as_slice().substr(idx * part_size, part_size));
    auto S = decoder->add_symbol({static_cast<td::uint32>(idx), std::move(sym)});
    if (S.is_error()) {
      if (verbose) std::fprintf(stderr, "  add_symbol(%zu) failed: %s\n", i, S.message().c_str());
      return r;
    }
  }
  auto t_add_end = td::Time::now();
  r.add_symbols_ms = (t_add_end - t_add_begin) * 1000.0;

  if (!decoder->may_try_decode()) {
    if (verbose) std::fprintf(stderr, "  decoder not ready after %zu symbols\n", feed_count);
    return r;
  }
  auto t_try_begin = td::Time::now();
  auto decoded = decoder->try_decode(false);
  auto t_try_end = td::Time::now();
  if (decoded.is_error()) {
    if (verbose) std::fprintf(stderr, "  try_decode failed: %s\n", decoded.error().message().c_str());
    return r;
  }
  r.try_decode_ms = (t_try_end - t_try_begin) * 1000.0;

  if (decoded.ok().data.as_slice() != payload.as_slice()) {
    std::fprintf(stderr, "  ERROR: decoded payload differs from original\n");
    return r;
  }
  r.ok = true;
  return r;
}

}  // namespace

int main(int argc, char *argv[]) {
  size_t data_size = 8 * 1024 * 1024;
  int runs = 3;
  int verbosity = 0;
  // 0 → all systematic (trivial decode); k → all repair (worst case).
  size_t feed_offset = 0;
  bool decode_worst_case = false;
  // Default scan: a handful of (part_size, K) pairs that bracket the two-step
  // broadcast PoC range (K=192 for 8 nodes; K_MULTIPLIER tunable).
  std::vector<size_t> part_sizes = {1024, 2048, 4096, 8192, 16384, 32768, 43000, 65536, 131072, 262144};
  size_t overshoot = 2;

  td::OptionParser p;
  p.add_option('s', "size", "data size in bytes (default 8 MiB)",
               [&](td::Slice s) { data_size = std::stoul(s.str()); });
  p.add_option('r', "runs", "runs per part_size (default 3)",
               [&](td::Slice s) { runs = std::stoi(s.str()); });
  p.add_option('o', "overshoot", "encode this many symbols = K * overshoot (default 2)",
               [&](td::Slice s) { overshoot = std::stoul(s.str()); });
  p.add_option('\0', "decoder-worst", "feed decoder K repair symbols (id>=K) instead of K systematic",
               [&](td::Slice) { decode_worst_case = true; });
  p.add_option('\0', "feed-offset", "feed K symbols starting at this ESI (default 0)",
               [&](td::Slice s) { feed_offset = std::stoul(s.str()); });
  p.add_option('v', "verbosity", "log verbosity offset (default 0)",
               [&](td::Slice s) { verbosity = std::stoi(s.str()); });
  p.add_checked_option('\0', "part-sizes", "comma-separated part sizes (overrides default scan)",
                       [&](td::Slice s) -> td::Status {
                         part_sizes.clear();
                         std::string cur;
                         for (char c : s) {
                           if (c == ',') {
                             if (!cur.empty()) part_sizes.push_back(std::stoul(cur));
                             cur.clear();
                           } else {
                             cur.push_back(c);
                           }
                         }
                         if (!cur.empty()) part_sizes.push_back(std::stoul(cur));
                         return td::Status::OK();
                       });
  auto S = p.run(argc, argv);
  if (S.is_error()) {
    std::fprintf(stderr, "%s\n", S.error().message().c_str());
    return 1;
  }
  SET_VERBOSITY_LEVEL(verbosity_WARNING + verbosity);

  td::BufferSlice payload(data_size);
  for (size_t i = 0; i < data_size; ++i) {
    payload.as_slice().begin()[i] = static_cast<char>(td::Random::fast_uint32() & 0xff);
  }

  std::fprintf(stderr,
               "# td::raptorq split bench — data_size=%zu B (%.2f MiB) overshoot=%zu runs=%d\n",
               data_size, data_size / 1024.0 / 1024.0, overshoot, runs);
  std::fprintf(stderr,
               "# %-9s %-6s  %-10s  %-10s  %-12s  %-10s  %-12s  %-10s  %-12s\n",
               "part_size", "K", "enc_create", "precalc",
               "gen_2K(ms)", "dec_create", "add_K(ms)", "try_dec",
               "total(ms)");

  for (size_t part_size : part_sizes) {
    if (part_size >= data_size) continue;
    std::vector<RunResult> ok_runs;
    ok_runs.reserve(runs);
    size_t k_for_offset = (data_size + part_size - 1) / part_size;
    size_t this_offset = decode_worst_case ? k_for_offset : feed_offset;
    for (int run = 0; run < runs; ++run) {
      auto r = bench_one(payload, part_size, overshoot, this_offset, run == 0 && verbosity > 0);
      if (r.ok) ok_runs.push_back(r);
    }
    if (ok_runs.empty()) {
      std::fprintf(stderr, "  %-9zu %-6s  FAILED\n", part_size, "-");
      continue;
    }
    auto best_by = [&](double RunResult::*field) {
      double best = ok_runs[0].*field;
      for (auto &r : ok_runs) best = std::min(best, r.*field);
      return best;
    };
    auto avg_by = [&](double RunResult::*field) {
      double sum = 0;
      for (auto &r : ok_runs) sum += r.*field;
      return sum / static_cast<double>(ok_runs.size());
    };
    size_t k = ok_runs.front().k;
    double total_avg = avg_by(&RunResult::encoder_create_ms) + avg_by(&RunResult::precalc_ms) +
                       avg_by(&RunResult::gen_total_ms) + avg_by(&RunResult::decoder_create_ms) +
                       avg_by(&RunResult::add_symbols_ms) + avg_by(&RunResult::try_decode_ms);
    std::fprintf(stderr,
                 "  %-9zu %-6zu  %-10.2f  %-10.2f  %-12.2f  %-10.2f  %-12.2f  %-10.2f  %-12.2f\n",
                 part_size, k,
                 best_by(&RunResult::encoder_create_ms),
                 best_by(&RunResult::precalc_ms),
                 best_by(&RunResult::gen_total_ms),
                 best_by(&RunResult::decoder_create_ms),
                 best_by(&RunResult::add_symbols_ms),
                 best_by(&RunResult::try_decode_ms),
                 total_avg);
  }
  return 0;
}
