/*
    This file is part of TON Blockchain Library.

    TON Blockchain Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    TON Blockchain Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with TON Blockchain Library.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2017-2020 Telegram Systems LLP
*/

#include <blake3.h>

#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "adnl/adnl-node-id.hpp"
#include "auto/tl/ton_api.h"
#include "crypto/common/bitstring.h"
#include "keys/keys.hpp"
#include "td/actor/actor.h"
#include "td/fec/raptorq/Decoder.h"
#include "td/fec/raptorq/Encoder.h"
#include "td/utils/List.h"
#include "td/utils/Status.h"
#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/int_types.h"
#include "td/utils/port/Clocks.h"

#include "broadcast-twostep.hpp"
#include "overlay.hpp"

namespace ton {

namespace overlay {

constexpr int VERBOSITY_NAME(TWOSTEP_WARNING) = verbosity_WARNING;
constexpr int VERBOSITY_NAME(TWOSTEP_INFO) = verbosity_WARNING;
constexpr int VERBOSITY_NAME(TWOSTEP_DEBUG) = verbosity_DEBUG;

static constexpr size_t FEC_MIN_BYTES = 513;
static constexpr size_t FEC_MIN_OTHER_NODES = 5;

// Hash bundle parts with BLAKE3 instead of SHA-256. BLAKE3's SIMD backend
// runs ~5-8 GB/s on x86 vs OpenSSL's SHA-256 at ~1.85 GB/s, cutting per-bundle
// hash cost from ~2.5 ms to ~0.5 ms for a 2.4 MB bundle. Wire format
// unchanged (still a 32-byte digest in the same TL field). Sender and
// receiver MUST agree; this is a breaking change wired only into the
// twostep-bundle path.
static td::Bits256 blake3_bits256(td::Slice data) {
  blake3_hasher h;
  blake3_hasher_init(&h);
  blake3_hasher_update(&h, data.data(), data.size());
  td::Bits256 out;
  blake3_hasher_finalize(&h, out.as_slice().ubegin(), 32);
  return out;
}

// FEC data_hash used in broadcast_id derivation. Instead of an extra
// sha256(data) pass (~4 ms for 8 MB), we hash the K systematic ESIs as they
// stream out of the encoder during immediate-bundle generation — that's
// exactly the original data plus zero padding to K * part_size. Receiver
// reproduces this by BLAKE3'ing R.data || zeros after decode.
//
// Cheap reusable zero-page for padding the last symbol.
static constexpr std::array<uint8_t, 4096> kBlake3PadZeros{};

static void blake3_padded_finish(blake3_hasher *h, size_t pad_bytes, td::Bits256 *out) {
  while (pad_bytes > 0) {
    size_t chunk = std::min(pad_bytes, kBlake3PadZeros.size());
    blake3_hasher_update(h, kBlake3PadZeros.data(), chunk);
    pad_bytes -= chunk;
  }
  blake3_hasher_finalize(h, out->as_slice().ubegin(), 32);
}

static td::Bits256 blake3_padded(td::Slice data, size_t k, size_t part_size) {
  blake3_hasher h;
  blake3_hasher_init(&h);
  blake3_hasher_update(&h, data.data(), data.size());
  size_t total = k * part_size;
  size_t pad = total > data.size() ? total - data.size() : 0;
  td::Bits256 out;
  blake3_padded_finish(&h, pad, &out);
  return out;
}

// PoC: split each broadcast into FEC_K_MULTIPLIER more chunks than the original
// (N-1)/2 design. With ~8 nodes that pushes K from 3 to ~192, which lands in
// td::raptorq's sweet spot (≤10ms encode/decode vs ~120ms for tiny K). Each
// peer carries K/N_peers chunks instead of one. NOT backwards compatible —
// breaks broadcast_id dedup, the seqno≥N receiver check, and the one-chunk-
// per-peer invariant of the existing protocol. Bench-only.
static constexpr size_t FEC_K_MULTIPLIER = 64;

static constexpr size_t fec_k(size_t other_nodes) {
  LOG_CHECK(other_nodes > 2) << "other_nodes=" << other_nodes;
  return ((other_nodes - 1) / 2) * FEC_K_MULTIPLIER;
}

// Number of chunks the sender produces. Held at 2*K (sender output ≈ 2× data
// size) so the broadcast survives roughly half of the peers failing to
// rebroadcast: a receiver collects its own direct chunks (~2K/N_peers) plus
// one stride per still-rebroadcasting peer, and any ≥N_peers/2 strides total
// already give ≥K unique seqnos for decode.
static constexpr size_t fec_total_chunks(size_t other_nodes) {
  return fec_k(other_nodes) * 2;
}

struct BroadcastTwostepDebugInfo {
  adnl::AdnlNodeIdShort src_adnl_id;
  td::Bits256 data_hash;
  td::uint32 data_size{0};
  td::uint32 symbols_received{0};
  td::uint32 symbols_needed{0};
  td::Timestamp timestamp;
  std::set<adnl::AdnlNodeIdShort> chunk_senders;

  void print_senders(td::StringBuilder &sb) const {
    sb << "senders=" << chunk_senders;
  }

  double elapsed() const {
    return td::Timestamp::now().at() - timestamp.at();
  }
};

td::StringBuilder &operator<<(td::StringBuilder &sb, const BroadcastTwostepDebugInfo &d) {
  sb << "src=" << d.src_adnl_id << " data_hash=" << d.data_hash.to_hex() << " data_size=" << d.data_size;
  if (d.symbols_needed > 0) {
    sb << " symbols=" << d.symbols_received << "/" << d.symbols_needed;
  }
  if (!d.chunk_senders.empty()) {
    sb << " unique_senders=" << d.chunk_senders.size();
  }
  return sb;
}

struct BroadcastTwostep : td::ListNode {
  Overlay::BroadcastHash broadcast_id;
  td::uint32 date;
  std::unique_ptr<td::raptorq::Decoder> decoder;
  bool delivered = false;
  bool rebroadcasted_part = false;
  bool decode_dispatched = false;  // worker actor took the decoder
  std::set<td::uint32> seen_parts = {};
  // Cached raw systematic parts (id < k) indexed by ESI. As soon as all k of
  // them have arrived we can reconstruct the data with a pure memcpy and skip
  // the ~20 ms FEC solver entirely. Each entry is a refcounted clone of the
  // wire BufferSlice — practically free.
  std::vector<td::BufferSlice> systematic_parts;
  size_t systematic_filled = 0;
  size_t k = 0;
  size_t part_size = 0;
  size_t data_size = 0;
  BroadcastTwostepDebugInfo debug;
};

td::StringBuilder &operator<<(td::StringBuilder &sb, const BroadcastTwostep &b) {
  return sb << "broadcast_id=" << b.broadcast_id.to_hex() << " " << b.debug;
}

struct BroadcastTwostepDataSimple {
  td::Bits256 broadcast_id;
  td::uint32 flags;
  td::uint32 date;
  adnl::AdnlNodeIdShort src;
  std::vector<adnl::AdnlNodeIdShort> dsts;
  td::BufferSlice data;
  td::BufferSlice extra;
};

struct BroadcastTwostepDataFec {
  td::Bits256 broadcast_id;
  td::uint32 flags;
  td::uint32 date;
  adnl::AdnlNodeIdShort src;
  adnl::AdnlNodeIdShort dst;
  td::Bits256 data_hash;
  td::uint32 data_size;
  td::uint32 seqno;
  td::BufferSlice part;
  td::BufferSlice extra;
};

// Bundle: many FEC chunks for a single destination peer, packed contiguously
// and signed once. Wire payload per peer drops from K small messages (each
// carrying its own date/PublicKey/cert/signature) to one big message — and
// keyring sign calls drop from K to 1 per broadcast.
struct BroadcastTwostepDataFecBundle {
  td::Bits256 broadcast_id;
  td::uint32 flags;
  td::uint32 date;
  adnl::AdnlNodeIdShort src;
  adnl::AdnlNodeIdShort dst;
  td::Bits256 data_hash;
  td::uint32 data_size;
  td::uint32 part_size;
  td::uint32 seqno_from;
  td::BufferSlice parts;  // seqno_count = parts.size() / part_size, ESIs seqno_from..seqno_from+count-1
  td::BufferSlice extra;
};

BroadcastsTwostep::BroadcastsTwostep() = default;

BroadcastsTwostep::~BroadcastsTwostep() = default;

namespace {

// One bundle to be transmitted to one peer. Either contains only systematic
// ESIs (id < K, gen_symbol just memcpys from data_) or only repair ESIs
// (id >= K, requires precalc to have run). Mixed peer ranges are split at
// K into two BundlePlans so we can ship the systematic half immediately.
struct BundlePlan {
  adnl::AdnlNodeIdShort dst;
  size_t seqno_from;
  size_t count;
};

// Stuff that's identical across every bundle for a single broadcast — kept
// in one struct so the worker actor only carries one heap of state.
struct BundleContext {
  td::actor::ActorId<OverlayImpl> overlay_actor;
  td::actor::ActorId<keyring::Keyring> keyring_actor;
  PublicKeyHash send_as;
  td::Bits256 broadcast_id;
  td::Bits256 data_hash;
  td::uint32 data_size;
  td::uint32 part_size;
  td::uint32 flags;
  td::uint32 date;
  adnl::AdnlNodeIdShort src;
  td::BufferSlice extra;
};

// Generate one bundle's worth of symbols from `encoder`, hash the parts,
// build the sign request, and dispatch to keyring. The signed-callback
// continues into OverlayImpl::broadcast_twostep_signed_fec_bundle which
// runs the existing serialize+send path. Returns true on success.
bool dispatch_fec_bundle(td::raptorq::Encoder *encoder, const BundleContext &ctx, const BundlePlan &plan) {
  td::BufferSlice parts(ctx.part_size * plan.count);
  for (size_t j = 0; j < plan.count; ++j) {
    td::Status S = encoder->gen_symbol(static_cast<td::uint32>(plan.seqno_from + j),
                                       parts.as_slice().substr(j * ctx.part_size, ctx.part_size));
    if (S.is_error()) {
      VLOG(TWOSTEP_WARNING) << "cannot generate symbol: " << S;
      return false;
    }
  }
  td::Bits256 parts_hash = blake3_bits256(parts.as_slice());
  td::BufferSlice to_sign = create_serialize_tl_object<ton_api::overlay_broadcastTwostepFecBundle_toSign>(
      ctx.broadcast_id, static_cast<std::int32_t>(plan.seqno_from), td::BufferSlice(parts_hash.as_slice()));
  BroadcastTwostepDataFecBundle passdata{
      .broadcast_id = ctx.broadcast_id,
      .flags = ctx.flags,
      .date = ctx.date,
      .src = ctx.src,
      .dst = plan.dst,
      .data_hash = ctx.data_hash,
      .data_size = ctx.data_size,
      .part_size = ctx.part_size,
      .seqno_from = static_cast<td::uint32>(plan.seqno_from),
      .parts = std::move(parts),
      .extra = ctx.extra.clone(),
  };
  auto P = td::PromiseCreator::lambda([overlay = ctx.overlay_actor, data = std::move(passdata)](
                                          td::Result<std::pair<td::BufferSlice, PublicKey>> R) mutable {
    td::actor::send_closure(overlay, &OverlayImpl::broadcast_twostep_signed_fec_bundle, std::move(data), std::move(R));
  });
  td::actor::send_closure(ctx.keyring_actor, &keyring::Keyring::sign_add_get_public_key, ctx.send_as,
                          std::move(to_sign), std::move(P));
  return true;
}

// One-shot actor: owns the encoder + the bundle plans whose ESIs include
// repair symbols. start_up() runs precalc() (the ~22 ms gauss elim) on this
// actor's worker thread instead of the overlay's, then dispatches each
// deferred bundle through the normal sign+send path and stops itself. By
// the time precalc finishes, the systematic-only bundles sent from
// BroadcastsTwostep::send have already started flying.
class TwostepPrecalcWorker final : public td::actor::Actor {
 public:
  TwostepPrecalcWorker(std::unique_ptr<td::raptorq::Encoder> encoder, std::vector<BundlePlan> deferred,
                       BundleContext ctx)
      : encoder_(std::move(encoder)), deferred_(std::move(deferred)), ctx_(std::move(ctx)) {
  }

 private:
  void start_up() override {
    // Include local= so render-waterfall.py attributes these events to the
    // sender's row. The worker actor lives outside the overlay, so the only
    // handle to the sender's adnl id is via ctx_.src (which equals overlay's
    // local_id since we're broadcasting from this node).
    VLOG(TWOSTEP_INFO) << "twostep PRECALC_BEGIN sender broadcast_id=" << ctx_.broadcast_id.to_hex()
                       << " local=" << ctx_.src;
    encoder_->precalc();
    VLOG(TWOSTEP_INFO) << "twostep PRECALC_END sender broadcast_id=" << ctx_.broadcast_id.to_hex()
                       << " repair_bundles=" << deferred_.size() << " local=" << ctx_.src;
    for (auto &plan : deferred_) {
      dispatch_fec_bundle(encoder_.get(), ctx_, plan);
    }
    stop();
  }

  std::unique_ptr<td::raptorq::Encoder> encoder_;
  std::vector<BundlePlan> deferred_;
  BundleContext ctx_;
};

// One-shot actor that runs the FEC decoder's try_decode() off the receiver
// actor. While it works, the receiver actor keeps draining new bundles —
// if all K systematic parts arrive in the meantime, the fast-path in
// process_broadcast delivers via memcpy and this worker's result is just
// discarded.
class TwostepDecodeWorker final : public td::actor::Actor {
 public:
  TwostepDecodeWorker(std::unique_ptr<td::raptorq::Decoder> decoder,
                      td::actor::StartedTask<td::BufferSlice>::ExternalPromise promise,
                      td::Bits256 broadcast_id, adnl::AdnlNodeIdShort local_id)
      : decoder_(std::move(decoder))
      , promise_(std::move(promise))
      , broadcast_id_(broadcast_id)
      , local_id_(local_id) {
  }

 private:
  void start_up() override {
    VLOG(TWOSTEP_INFO) << "twostep DECODE_BEGIN receiver broadcast_id=" << broadcast_id_.to_hex()
                       << " local=" << local_id_;
    auto R = decoder_->try_decode(false);
    VLOG(TWOSTEP_INFO) << "twostep DECODE_END receiver broadcast_id=" << broadcast_id_.to_hex()
                       << " local=" << local_id_;
    if (R.is_error()) {
      promise_.set_error(R.move_as_error());
    } else {
      promise_.set_value(std::move(R.move_as_ok().data));
    }
    stop();
  }

  std::unique_ptr<td::raptorq::Decoder> decoder_;
  td::actor::StartedTask<td::BufferSlice>::ExternalPromise promise_;
  td::Bits256 broadcast_id_;
  adnl::AdnlNodeIdShort local_id_;
};

}  // namespace

void BroadcastsTwostep::send(OverlayImpl *overlay, PublicKeyHash send_as, td::BufferSlice data, td::BufferSlice extra,
                             td::uint32 flags) {
  size_t data_size = data.size();
  td::uint32 date = static_cast<td::uint32>(td::Clocks::system());
  std::vector<adnl::AdnlNodeIdShort> other_nodes;
  overlay->iterate_all_peers([&](const adnl::AdnlNodeIdShort &peer_id, OverlayPeer &peer) {
    if (overlay->is_persistent_node(peer_id) && peer_id != overlay->local_id()) {
      other_nodes.push_back(peer_id);
    }
  });
  td::Bits256 broadcast_id;
  td::Bits256 data_hash;
  bool use_fec = data_size >= FEC_MIN_BYTES && other_nodes.size() >= FEC_MIN_OTHER_NODES;
  if (use_fec) {
    size_t k = fec_k(other_nodes.size());
    size_t part_size = (data_size + k - 1) / k;
    CHECK(part_size < data_size);
    VLOG(TWOSTEP_INFO) << "twostep ENCODE_BEGIN sender data_size=" << data_size << " k=" << k
                       << " part_size=" << part_size << " local=" << overlay->local_id();
    auto R = td::raptorq::Encoder::create(part_size, data.clone());
    if (R.is_error()) {
      VLOG(TWOSTEP_WARNING) << "cannot create FEC encoder: " << R.move_as_error();
      return;
    }
    auto encoder = R.move_as_ok();
    size_t total_chunks = fec_total_chunks(other_nodes.size());
    // Block distribution: peer p gets contiguous ESIs [p*chunks_per_peer, ...).
    size_t n_peers = other_nodes.size();
    size_t chunks_per_peer = (total_chunks + n_peers - 1) / n_peers;

    // Split each peer's ESI range at the systematic/repair boundary (id=K).
    // Symbols with id<K are systematic — gen_symbol is a memcpy from data,
    // no precalc needed, so we dispatch those bundles immediately. Anything
    // touching id>=K requires precalc (~22 ms gauss elim) and is deferred
    // to a worker actor so the systematic sends start hitting the wire
    // while precalc runs in parallel.
    std::vector<BundlePlan> immediate_plans;
    std::vector<BundlePlan> deferred_plans;
    immediate_plans.reserve(n_peers);
    deferred_plans.reserve(n_peers);
    for (size_t p = 0; p < n_peers; p++) {
      size_t seqno_from = p * chunks_per_peer;
      size_t seqno_end = std::min(seqno_from + chunks_per_peer, total_chunks);
      if (seqno_from >= seqno_end) {
        break;
      }
      size_t cut = std::clamp<size_t>(k, seqno_from, seqno_end);
      if (cut > seqno_from) {
        immediate_plans.push_back({.dst = other_nodes[p], .seqno_from = seqno_from, .count = cut - seqno_from});
      }
      if (seqno_end > cut) {
        deferred_plans.push_back({.dst = other_nodes[p], .seqno_from = cut, .count = seqno_end - cut});
      }
    }

    // Phase 1: generate the systematic bundles up-front. While we copy each
    // chunk into its parts buffer we also stream the bytes through one
    // shared BLAKE3 hasher; once the loop finishes that hasher's output is
    // exactly BLAKE3(data || zero-padding) = our new data_hash. This
    // replaces the separate sha256(data) pass that used to cost ~4 ms for
    // 8 MB. immediate_plans iterate peer-by-peer over disjoint ESI ranges
    // that together exactly cover [0, K), so we feed the systematic data in
    // ESI order — the same order the receiver will reconstruct after decode.
    struct PreparedBundle {
      BundlePlan plan;
      td::BufferSlice parts;
      td::Bits256 parts_hash;
    };
    std::vector<PreparedBundle> prepared_immediate;
    prepared_immediate.reserve(immediate_plans.size());

    blake3_hasher data_hasher;
    blake3_hasher_init(&data_hasher);
    for (auto &plan : immediate_plans) {
      td::BufferSlice parts(part_size * plan.count);
      bool ok = true;
      for (size_t j = 0; j < plan.count; ++j) {
        td::Status S = encoder->gen_symbol(static_cast<td::uint32>(plan.seqno_from + j),
                                           parts.as_slice().substr(j * part_size, part_size));
        if (S.is_error()) {
          VLOG(TWOSTEP_WARNING) << "cannot generate symbol: " << S;
          ok = false;
          break;
        }
      }
      if (!ok) continue;
      blake3_hasher_update(&data_hasher, parts.as_slice().ubegin(), parts.size());
      td::Bits256 parts_hash = blake3_bits256(parts.as_slice());
      prepared_immediate.push_back({plan, std::move(parts), parts_hash});
    }
    blake3_hasher_finalize(&data_hasher, data_hash.as_slice().ubegin(), 32);

    broadcast_id = get_tl_object_sha_bits256(create_tl_object<ton_api::overlay_broadcastTwostep_id>(
        flags, date, send_as.bits256_value(), overlay->local_id().bits256_value(), data_hash,
        static_cast<td::int32>(data_size), static_cast<td::int32>(part_size), extra.clone()));

    VLOG(TWOSTEP_INFO) << "twostep START sender broadcast_id=" << broadcast_id.to_hex()
                       << " data_hash=" << data_hash.to_hex() << " data_size=" << data_size
                       << " recipients=" << other_nodes.size() << " mode=FEC"
                       << " local=" << overlay->local_id();

    BundleContext ctx{
        .overlay_actor = actor_id(overlay),
        .keyring_actor = overlay->keyring(),
        .send_as = send_as,
        .broadcast_id = broadcast_id,
        .data_hash = data_hash,
        .data_size = static_cast<td::uint32>(data_size),
        .part_size = static_cast<td::uint32>(part_size),
        .flags = flags,
        .date = date,
        .src = adnl::AdnlNodeIdShort(send_as),
        .extra = extra.clone(),
    };

    // Phase 2: dispatch sign+send for each pre-generated immediate bundle.
    for (auto &pb : prepared_immediate) {
      td::BufferSlice to_sign = create_serialize_tl_object<ton_api::overlay_broadcastTwostepFecBundle_toSign>(
          broadcast_id, static_cast<std::int32_t>(pb.plan.seqno_from), td::BufferSlice(pb.parts_hash.as_slice()));
      BroadcastTwostepDataFecBundle passdata{
          .broadcast_id = broadcast_id,
          .flags = flags,
          .date = date,
          .src = ctx.src,
          .dst = pb.plan.dst,
          .data_hash = data_hash,
          .data_size = static_cast<td::uint32>(data_size),
          .part_size = static_cast<td::uint32>(part_size),
          .seqno_from = static_cast<td::uint32>(pb.plan.seqno_from),
          .parts = std::move(pb.parts),
          .extra = extra.clone(),
      };
      auto P = td::PromiseCreator::lambda([overlay = ctx.overlay_actor, d = std::move(passdata)](
                                              td::Result<std::pair<td::BufferSlice, PublicKey>> R) mutable {
        td::actor::send_closure(overlay, &OverlayImpl::broadcast_twostep_signed_fec_bundle, std::move(d),
                                std::move(R));
      });
      td::actor::send_closure(ctx.keyring_actor, &keyring::Keyring::sign_add_get_public_key, send_as,
                              std::move(to_sign), std::move(P));
    }
    VLOG(TWOSTEP_INFO) << "twostep ENCODE_END sender broadcast_id=" << broadcast_id.to_hex()
                       << " systematic_bundles=" << prepared_immediate.size()
                       << " repair_bundles=" << deferred_plans.size() << " local=" << overlay->local_id();
    if (!deferred_plans.empty()) {
      // Worker actor runs precalc on its own scheduler thread and then
      // sign+sends the repair bundles; it owns the encoder and stops itself
      // when done. The overlay actor returns control to the scheduler
      // immediately — it doesn't block on precalc.
      td::actor::create_actor<TwostepPrecalcWorker>("twostep-precalc", std::move(encoder), std::move(deferred_plans),
                                                    std::move(ctx))
          .release();
    }
  } else {
    data_hash = blake3_bits256(data.as_slice());
    broadcast_id = get_tl_object_sha_bits256(create_tl_object<ton_api::overlay_broadcastTwostep_id>(
        flags, date, send_as.bits256_value(), overlay->local_id().bits256_value(), data_hash,
        static_cast<std::int32_t>(data_size), static_cast<std::int32_t>(data_size), extra.clone()));
    VLOG(TWOSTEP_INFO) << "twostep START sender broadcast_id=" << broadcast_id.to_hex()
                       << " data_hash=" << data_hash.to_hex() << " data_size=" << data_size
                       << " recipients=" << other_nodes.size() << " mode=simple"
                       << " local=" << overlay->local_id();
    td::BufferSlice to_sign =
        create_serialize_tl_object<ton_api::overlay_broadcastTwostepSimple_toSign>(broadcast_id, data.clone());
    BroadcastTwostepDataSimple passdata{
        .broadcast_id = broadcast_id,
        .flags = flags,
        .date = date,
        .src = adnl::AdnlNodeIdShort(send_as),
        .dsts = std::move(other_nodes),
        .data = data.clone(),
        .extra = extra.clone(),
    };
    auto P = td::PromiseCreator::lambda([overlay = actor_id(overlay), data = std::move(passdata)](
                                            td::Result<std::pair<td::BufferSlice, PublicKey>> R) mutable {
      td::actor::send_closure(overlay, &OverlayImpl::broadcast_twostep_signed_simple, std::move(data), std::move(R));
    });
    td::actor::send_closure(overlay->keyring(), &keyring::Keyring::sign_add_get_public_key, send_as, std::move(to_sign),
                            std::move(P));
  }
  if (!overlay->is_delivered(broadcast_id)) {
    overlay->get_broadcasts_limiter(send_as, overlay->get_certificate(send_as).get()).register_broadcast(data.size());
    overlay->register_delivered_broadcast(broadcast_id);
    overlay->deliver_broadcast(send_as, std::move(data), std::move(extra));
  }
}

static bool handle_error(const td::Result<std::pair<td::BufferSlice, PublicKey>> &R) {
  if (R.is_error()) {
    if (R.error().code() == ErrorCode::notready) {
      LOG(DEBUG) << "failed to send twostep broadcast: " << R.error();
    } else {
      LOG(WARNING) << "failed to send twostep broadcast: " << R.error();
    }
    return true;
  }
  return false;
}

void BroadcastsTwostep::signed_simple(OverlayImpl *overlay, BroadcastTwostepDataSimple &&data,
                                      td::Result<std::pair<td::BufferSlice, PublicKey>> &&R) {
  if (handle_error(R)) {
    return;
  }
  auto V = R.move_as_ok();
  VLOG(TWOSTEP_INFO) << "twostep SEND_SIMPLE sender broadcast_id=" << data.broadcast_id.to_hex()
                     << " data_size=" << data.data.size() << " recipients=" << data.dsts.size()
                     << " local=" << overlay->local_id();
  auto cert = overlay->get_certificate(data.src.pubkey_hash());
  td::BufferSlice broadcast = create_serialize_tl_object<ton_api::overlay_broadcastTwostepSimple>(
      data.flags, data.date, V.second.tl(), overlay->local_id().bits256_value(),
      cert ? cert->tl() : Certificate::empty_tl(), std::move(data.data), std::move(data.extra), std::move(V.first));
  for (auto &dst : data.dsts) {
    td::actor::send_closure(overlay->overlay_manager(), &Overlays::send_message_via, dst, overlay->local_id(),
                            overlay->overlay_id(), broadcast.clone(), sender_);
  }
  overlay->get_broadcasts_limiter(data.src.pubkey_hash(), cert.get())
      .register_out_traffic(broadcast.size() * data.dsts.size());
}

void BroadcastsTwostep::signed_fec(OverlayImpl *overlay, BroadcastTwostepDataFec &&data,
                                   td::Result<std::pair<td::BufferSlice, PublicKey>> &&R) {
  if (handle_error(R)) {
    return;
  }
  auto V = R.move_as_ok();
  VLOG(TWOSTEP_INFO) << "twostep SEND_CHUNK sender broadcast_id=" << data.broadcast_id.to_hex()
                     << " data_hash=" << data.data_hash.to_hex() << " data_size=" << data.data_size
                     << " seqno=" << data.seqno << " part_size=" << data.part.size() << " to=" << data.dst
                     << " local=" << overlay->local_id();
  auto cert = overlay->get_certificate(data.src.pubkey_hash());
  td::BufferSlice broadcast = create_serialize_tl_object<ton_api::overlay_broadcastTwostepFec>(
      data.flags, data.date, V.second.tl(), overlay->local_id().bits256_value(),
      cert ? cert->tl() : Certificate::empty_tl(), data.data_hash, data.data_size, data.seqno, std::move(data.part),
      std::move(data.extra), std::move(V.first));
  overlay->get_broadcasts_limiter(data.src.pubkey_hash(), cert.get()).register_out_traffic(broadcast.size());
  td::actor::send_closure(overlay->overlay_manager(), &Overlays::send_message_via, data.dst, overlay->local_id(),
                          overlay->overlay_id(), std::move(broadcast), sender_);
}

void BroadcastsTwostep::signed_fec_bundle(OverlayImpl *overlay, BroadcastTwostepDataFecBundle &&data,
                                          td::Result<std::pair<td::BufferSlice, PublicKey>> &&R) {
  if (handle_error(R)) {
    return;
  }
  auto V = R.move_as_ok();
  size_t seqno_count = data.parts.size() / data.part_size;
  VLOG(TWOSTEP_INFO) << "twostep SEND_BUNDLE sender broadcast_id=" << data.broadcast_id.to_hex()
                     << " data_hash=" << data.data_hash.to_hex() << " data_size=" << data.data_size
                     << " seqno_from=" << data.seqno_from << " seqno_count=" << seqno_count
                     << " part_size=" << data.part_size << " to=" << data.dst
                     << " local=" << overlay->local_id();
  auto cert = overlay->get_certificate(data.src.pubkey_hash());
  td::BufferSlice broadcast = create_serialize_tl_object<ton_api::overlay_broadcastTwostepFecBundle>(
      data.flags, data.date, V.second.tl(), overlay->local_id().bits256_value(),
      cert ? cert->tl() : Certificate::empty_tl(), data.data_hash, data.data_size, data.part_size, data.seqno_from,
      std::move(data.parts), std::move(data.extra), std::move(V.first));
  overlay->get_broadcasts_limiter(data.src.pubkey_hash(), cert.get()).register_out_traffic(broadcast.size());
  td::actor::send_closure(overlay->overlay_manager(), &Overlays::send_message_via, data.dst, overlay->local_id(),
                          overlay->overlay_id(), std::move(broadcast), sender_);
}

static td::Result<BroadcastCheckResult> check_source(OverlayImpl *overlay, const PublicKeyHash &src_keyhash,
                                                     const Certificate *certificate, td::uint32 data_size,
                                                     adnl::AdnlNodeIdShort message_from) {
  auto r = overlay->check_source_eligible(src_keyhash, certificate, data_size, true, message_from);
  if (r == BroadcastCheckResult::Forbidden) {
    return td::Status::Error(ErrorCode::error, "broadcast is forbidden");
  }
  return r;
}

td::uint64 BroadcastsTwostep::rebroadcast(OverlayImpl *overlay, const adnl::AdnlNodeIdShort &bcast_src_adnl_id,
                                          const td::BufferSlice &data) {
  td::uint64 total_size = 0;
  overlay->iterate_all_peers([&](const adnl::AdnlNodeIdShort &peer_id, OverlayPeer &) {
    if (peer_id != bcast_src_adnl_id && peer_id != overlay->local_id()) {
      total_size += data.size();
      td::actor::send_closure(overlay->overlay_manager(), &Overlays::send_message_via, peer_id, overlay->local_id(),
                              overlay->overlay_id(), data.clone(), sender_);
    }
  });
  return total_size;
}

static td::actor::Task<> check_and_deliver(OverlayImpl *overlay, PublicKeyHash src, BroadcastCheckResult check_result,
                                           td::BufferSlice data, td::BufferSlice extra) {
  if (check_result != BroadcastCheckResult::Allowed) {
    auto [task, promise] = td::actor::StartedTask<>::make_bridge();
    overlay->check_broadcast(src, data.clone(), std::move(promise));
    co_await std::move(task);
  }
  overlay->deliver_broadcast(src, std::move(data), std::move(extra));
  co_return {};
}

td::actor::Task<> BroadcastsTwostep::process_broadcast(
    OverlayImpl *overlay, adnl::AdnlNodeIdShort src_peer_id,
    tl_object_ptr<ton_api::overlay_broadcastTwostepSimple> broadcast) {
  CO_TRY(overlay->check_date(broadcast->date_));
  PublicKey src_key(broadcast->src_);
  PublicKeyHash src_keyhash(src_key.compute_short_id());
  adnl::AdnlNodeIdShort bcast_src_adnl_id{broadcast->src_adnl_id_};
  td::Bits256 data_hash = blake3_bits256(broadcast->data_.as_slice());
  td::Bits256 broadcast_id = get_tl_object_sha_bits256(create_tl_object<ton_api::overlay_broadcastTwostep_id>(
      broadcast->flags_, broadcast->date_, src_keyhash.bits256_value(), bcast_src_adnl_id.bits256_value(), data_hash,
      static_cast<std::int32_t>(broadcast->data_.size()), static_cast<std::int32_t>(broadcast->data_.size()),
      broadcast->extra_.clone()));
  if (overlay->is_delivered(broadcast_id)) {
    VLOG(TWOSTEP_DEBUG) << "twostep DUPLICATE receiver broadcast_id=" << broadcast_id.to_hex();
    co_return td::Status::Error(ErrorCode::notready, "duplicate broadcast");
  }
  bool will_rebroadcast = src_peer_id == bcast_src_adnl_id;
  VLOG(TWOSTEP_INFO) << "twostep RECV_SIMPLE receiver broadcast_id=" << broadcast_id.to_hex()
                     << " data_hash=" << data_hash.to_hex() << " data_size=" << broadcast->data_.size()
                     << " from=" << src_peer_id << " will_rebroadcast=" << will_rebroadcast
                     << " local=" << overlay->local_id();

  td::BufferSlice to_sign = create_serialize_tl_object<ton_api::overlay_broadcastTwostepSimple_toSign>(
      broadcast_id, broadcast->data_.clone());
  auto cert = CO_TRY(Certificate::create(broadcast->certificate_));
  auto check_result = CO_TRY(
      check_source(overlay, src_keyhash, cert.get(), static_cast<td::uint32>(broadcast->data_.size()), src_peer_id));
  CO_TRY(overlay->get_broadcasts_limiter(src_keyhash, cert.get()).precheck_new_broadcast(broadcast->data_.size()));
  co_await overlay->precheck_broadcast(src_keyhash, broadcast_id, broadcast->extra_.clone(), false)
      .trace("precheck broadcast");
  {
    TD_PERF_COUNTER(check_signature_overlay_broadcast_twostep_simple);
    CO_TRY(overlay->check_signature_from_peer(src_key, to_sign, broadcast->signature_, src_peer_id));
  }
  co_await overlay->precheck_broadcast(src_keyhash, broadcast_id, broadcast->extra_.clone(), true)
      .trace("precheck broadcast");
  // utime and is_delivered could change during precheck_broadcast
  CO_TRY(overlay->check_date(broadcast->date_));
  if (overlay->is_delivered(broadcast_id)) {
    VLOG(TWOSTEP_DEBUG) << "twostep DUPLICATE receiver broadcast_id=" << broadcast_id.to_hex();
    co_return td::Status::Error(ErrorCode::notready, "duplicate broadcast");
  }
  CO_TRY(overlay->get_broadcasts_limiter(src_keyhash, cert.get()).try_register_broadcast(broadcast->data_.size()));
  if (will_rebroadcast) {
    td::uint64 total_size = rebroadcast(overlay, bcast_src_adnl_id, serialize_tl_object(broadcast, true));
    overlay->get_broadcasts_limiter(src_keyhash, cert.get()).register_out_traffic(total_size);
  }
  VLOG(TWOSTEP_INFO) << "twostep FINISH receiver broadcast_id=" << broadcast_id.to_hex()
                     << " data_hash=" << data_hash.to_hex() << " data_size=" << broadcast->data_.size()
                     << " decoded=true"
                     << " local=" << overlay->local_id();
  overlay->register_delivered_broadcast(broadcast_id);
  co_await check_and_deliver(overlay, src_keyhash, check_result, std::move(broadcast->data_),
                             std::move(broadcast->extra_));
  co_return {};
}

td::actor::Task<> BroadcastsTwostep::process_broadcast(OverlayImpl *overlay, adnl::AdnlNodeIdShort src_peer_id,
                                                       tl_object_ptr<ton_api::overlay_broadcastTwostepFec> broadcast) {
  td::uint32 date = static_cast<td::uint32>(broadcast->date_);
  CO_TRY(overlay->check_date(date));
  PublicKey src_key(broadcast->src_);
  PublicKeyHash src_keyhash(src_key.compute_short_id());
  adnl::AdnlNodeIdShort bcast_src_adnl_id{broadcast->src_adnl_id_};
  size_t data_size = static_cast<td::uint32>(broadcast->data_size_);
  size_t part_size = broadcast->part_.size();
  td::uint32 seqno = static_cast<td::uint32>(broadcast->seqno_);
  // PoC: matches the bumped fec_k. fec_total_chunks(persistent_node_count() - 1)
  // is the max seqno the sender will produce.
  size_t max_other = overlay->persistent_node_count();
  if (max_other > 0) --max_other;
  if (seqno >= fec_total_chunks(max_other)) {
    co_return td::Status::Error(ErrorCode::protoviolation, "too big seqno");
  }

  td::Bits256 broadcast_id = get_tl_object_sha_bits256(create_tl_object<ton_api::overlay_broadcastTwostep_id>(
      broadcast->flags_, broadcast->date_, src_keyhash.bits256_value(), bcast_src_adnl_id.bits256_value(),
      broadcast->data_hash_, broadcast->data_size_, static_cast<td::int32>(part_size), broadcast->extra_.clone()));
  auto it = broadcasts_.find(broadcast_id);
  if (overlay->is_delivered(broadcast_id) || (it != broadcasts_.end() && it->second->seen_parts.contains(seqno))) {
    VLOG(TWOSTEP_DEBUG) << "twostep DUPLICATE receiver broadcast_id=" << broadcast_id.to_hex() << " seqno=" << seqno;
    co_return td::Status::Error(ErrorCode::notready, "duplicate broadcast");
  }

  td::BufferSlice to_sign = create_serialize_tl_object<ton_api::overlay_broadcastTwostepFec_toSign>(
      broadcast_id, seqno, broadcast->part_.clone());
  auto cert = CO_TRY(Certificate::create(broadcast->certificate_));
  auto check_result =
      CO_TRY(check_source(overlay, src_keyhash, cert.get(), static_cast<td::uint32>(data_size), src_peer_id));
  if (it == broadcasts_.end()) {
    CO_TRY(overlay->get_broadcasts_limiter(src_keyhash, cert.get()).precheck_new_broadcast(data_size));
    co_await overlay->precheck_broadcast(src_keyhash, broadcast_id, broadcast->extra_.clone(), false)
        .trace("precheck broadcast");
  }
  {
    TD_PERF_COUNTER(check_signature_overlay_broadcast_twostep_fec);
    CO_TRY(overlay->check_signature_from_peer(src_key, to_sign, broadcast->signature_, src_peer_id));
  }
  if (it == broadcasts_.end()) {
    co_await overlay->precheck_broadcast(src_keyhash, broadcast_id, broadcast->extra_.clone(), true)
        .trace("precheck broadcast");
    // utime, is_delivered and broadcasts_ could change during precheck_broadcast
    it = broadcasts_.find(broadcast_id);
    CO_TRY(overlay->check_date(date));
    if (overlay->is_delivered(broadcast_id) || (it != broadcasts_.end() && it->second->seen_parts.contains(seqno))) {
      VLOG(TWOSTEP_DEBUG) << "twostep DUPLICATE receiver broadcast_id=" << broadcast_id.to_hex() << " seqno=" << seqno;
      co_return td::Status::Error(ErrorCode::notready, "duplicate broadcast");
    }
  }
  if (it == broadcasts_.end()) {
    CO_TRY(overlay->get_broadcasts_limiter(src_keyhash, cert.get()).try_register_broadcast(data_size));
    td::Result<std::unique_ptr<td::raptorq::Decoder>> R;
    if (part_size == 0 ||
        (R = td::raptorq::Decoder::create({(data_size + part_size - 1) / part_size, part_size, data_size}))
            .is_error()) {
      co_return td::Status::Error(ErrorCode::protoviolation, "invalid FEC parameters");
    }
    td::uint32 symbols_needed = static_cast<td::uint32>((data_size + part_size - 1) / part_size);
    std::unique_ptr<BroadcastTwostep> bcast(
        new BroadcastTwostep{.broadcast_id = broadcast_id,
                             .date = date,
                             .decoder = R.move_as_ok(),
                             // Single-chunk Fec path doesn't use the fast-path
                             // / off-actor decode optimizations — it only ever
                             // adds one symbol per message, so the systematic
                             // bookkeeping below stays zero.
                             .systematic_parts = {},
                             .debug = {.src_adnl_id = bcast_src_adnl_id,
                                       .data_hash = broadcast->data_hash_,
                                       .data_size = static_cast<td::uint32>(data_size),
                                       .symbols_received = 0,
                                       .symbols_needed = symbols_needed,
                                       .timestamp = td::Timestamp::now(),
                                       .chunk_senders = {}}});
    lru_.put(bcast.get());
    it = broadcasts_.emplace(broadcast_id, std::move(bcast)).first;
    VLOG(TWOSTEP_INFO) << "twostep START receiver " << *it->second << " from=" << src_peer_id
                       << " local=" << overlay->local_id();
  }
  auto bcast = it->second.get();
  bcast->seen_parts.insert(seqno);
  // PoC: each peer carries multiple chunks now (K = fec_k(N) = (N-1)/2 * M).
  // Rebroadcast every direct chunk from the source instead of only the first.
  bool will_rebroadcast = src_peer_id == bcast_src_adnl_id;
  if (will_rebroadcast) {
    td::uint64 total_size = rebroadcast(overlay, bcast_src_adnl_id, serialize_tl_object(broadcast, true));
    bcast->rebroadcasted_part = true;
    overlay->get_broadcasts_limiter(src_keyhash, cert.get()).register_out_traffic(total_size);
  }
  if (bcast->delivered) {
    co_return {};
  }
  bcast->debug.chunk_senders.insert(src_peer_id);
  CO_TRY(bcast->decoder->add_symbol({seqno, std::move(broadcast->part_)}));
  bcast->debug.symbols_received++;
  VLOG(TWOSTEP_INFO) << "twostep RECV_CHUNK receiver " << *bcast << " seqno=" << seqno << " from=" << src_peer_id
                     << " will_rebroadcast=" << will_rebroadcast << " local=" << overlay->local_id();
  if (bcast->decoder->may_try_decode()) {
    VLOG(TWOSTEP_INFO) << "twostep DECODE_BEGIN receiver broadcast_id=" << broadcast_id.to_hex()
                       << " local=" << overlay->local_id();
    auto R = CO_TRY(bcast->decoder->try_decode(false));
    VLOG(TWOSTEP_INFO) << "twostep DECODE_END receiver broadcast_id=" << broadcast_id.to_hex()
                       << " local=" << overlay->local_id();
    VLOG(TWOSTEP_INFO) << "twostep FINISH receiver " << *bcast << " decoded=true elapsed=" << bcast->debug.elapsed()
                       << " local=" << overlay->local_id();
    bcast->delivered = true;
    bcast->decoder = {};
    // Match the sender's incremental BLAKE3 over (data || zero-padding-to-K*part_size).
    size_t k = (data_size + part_size - 1) / part_size;
    if (broadcast->data_hash_ != blake3_padded(R.data.as_slice(), k, part_size)) {
      co_return td::Status::Error(ErrorCode::protoviolation, "broadcast data hash mismatch");
    }
    co_await check_and_deliver(overlay, src_keyhash, check_result, std::move(R.data), std::move(broadcast->extra_));
  }
  co_return {};
}

td::actor::Task<> BroadcastsTwostep::process_broadcast(
    OverlayImpl *overlay, adnl::AdnlNodeIdShort src_peer_id,
    tl_object_ptr<ton_api::overlay_broadcastTwostepFecBundle> broadcast) {
  td::uint32 date = static_cast<td::uint32>(broadcast->date_);
  CO_TRY(overlay->check_date(date));
  PublicKey src_key(broadcast->src_);
  PublicKeyHash src_keyhash(src_key.compute_short_id());
  adnl::AdnlNodeIdShort bcast_src_adnl_id{broadcast->src_adnl_id_};
  size_t data_size = static_cast<td::uint32>(broadcast->data_size_);
  size_t part_size = static_cast<td::uint32>(broadcast->part_size_);
  td::uint32 seqno_from = static_cast<td::uint32>(broadcast->seqno_from_);
  if (part_size == 0 || broadcast->parts_.size() % part_size != 0) {
    co_return td::Status::Error(ErrorCode::protoviolation, "bundle parts length not multiple of part_size");
  }
  size_t seqno_count = broadcast->parts_.size() / part_size;
  size_t max_other = overlay->persistent_node_count();
  if (max_other > 0) --max_other;
  if (static_cast<size_t>(seqno_from) + seqno_count > fec_total_chunks(max_other)) {
    co_return td::Status::Error(ErrorCode::protoviolation, "bundle seqno range exceeds total_chunks");
  }

  td::Bits256 broadcast_id = get_tl_object_sha_bits256(create_tl_object<ton_api::overlay_broadcastTwostep_id>(
      broadcast->flags_, broadcast->date_, src_keyhash.bits256_value(), bcast_src_adnl_id.bits256_value(),
      broadcast->data_hash_, broadcast->data_size_, static_cast<td::int32>(part_size), broadcast->extra_.clone()));
  auto it = broadcasts_.find(broadcast_id);
  if (overlay->is_delivered(broadcast_id)) {
    VLOG(TWOSTEP_DEBUG) << "twostep DUPLICATE receiver broadcast_id=" << broadcast_id.to_hex();
    co_return td::Status::Error(ErrorCode::notready, "duplicate broadcast");
  }

  // Match the sender's "sign sha256(parts)" trick (see signed_fec_bundle).
  td::Bits256 parts_hash = blake3_bits256(broadcast->parts_.as_slice());
  td::BufferSlice to_sign = create_serialize_tl_object<ton_api::overlay_broadcastTwostepFecBundle_toSign>(
      broadcast_id, static_cast<std::int32_t>(seqno_from), td::BufferSlice(parts_hash.as_slice()));
  auto cert = CO_TRY(Certificate::create(broadcast->certificate_));
  auto check_result =
      CO_TRY(check_source(overlay, src_keyhash, cert.get(), static_cast<td::uint32>(data_size), src_peer_id));
  if (it == broadcasts_.end()) {
    CO_TRY(overlay->get_broadcasts_limiter(src_keyhash, cert.get()).precheck_new_broadcast(data_size));
    co_await overlay->precheck_broadcast(src_keyhash, broadcast_id, broadcast->extra_.clone(), false)
        .trace("precheck broadcast");
  }
  {
    TD_PERF_COUNTER(check_signature_overlay_broadcast_twostep_fec);
    CO_TRY(overlay->check_signature_from_peer(src_key, to_sign, broadcast->signature_, src_peer_id));
  }
  if (it == broadcasts_.end()) {
    co_await overlay->precheck_broadcast(src_keyhash, broadcast_id, broadcast->extra_.clone(), true)
        .trace("precheck broadcast");
    it = broadcasts_.find(broadcast_id);
    CO_TRY(overlay->check_date(date));
    if (overlay->is_delivered(broadcast_id)) {
      co_return td::Status::Error(ErrorCode::notready, "duplicate broadcast");
    }
  }
  if (it == broadcasts_.end()) {
    CO_TRY(overlay->get_broadcasts_limiter(src_keyhash, cert.get()).try_register_broadcast(data_size));
    td::Result<std::unique_ptr<td::raptorq::Decoder>> R;
    size_t k = (data_size + part_size - 1) / part_size;
    if ((R = td::raptorq::Decoder::create({k, part_size, data_size})).is_error()) {
      co_return td::Status::Error(ErrorCode::protoviolation, "invalid FEC parameters");
    }
    std::unique_ptr<BroadcastTwostep> bcast(
        new BroadcastTwostep{.broadcast_id = broadcast_id,
                             .date = date,
                             .decoder = R.move_as_ok(),
                             .systematic_parts = std::vector<td::BufferSlice>(k),
                             .k = k,
                             .part_size = part_size,
                             .data_size = data_size,
                             .debug = {.src_adnl_id = bcast_src_adnl_id,
                                       .data_hash = broadcast->data_hash_,
                                       .data_size = static_cast<td::uint32>(data_size),
                                       .symbols_received = 0,
                                       .symbols_needed = static_cast<td::uint32>(k),
                                       .timestamp = td::Timestamp::now(),
                                       .chunk_senders = {}}});
    lru_.put(bcast.get());
    it = broadcasts_.emplace(broadcast_id, std::move(bcast)).first;
    VLOG(TWOSTEP_INFO) << "twostep START receiver " << *it->second << " from=" << src_peer_id
                       << " local=" << overlay->local_id();
  }
  auto bcast = it->second.get();
  bool will_rebroadcast = src_peer_id == bcast_src_adnl_id && !bcast->rebroadcasted_part;
  if (will_rebroadcast) {
    td::uint64 total_size = rebroadcast(overlay, bcast_src_adnl_id, serialize_tl_object(broadcast, true));
    bcast->rebroadcasted_part = true;
    overlay->get_broadcasts_limiter(src_keyhash, cert.get()).register_out_traffic(total_size);
  }
  if (bcast->delivered) {
    co_return {};
  }
  bcast->debug.chunk_senders.insert(src_peer_id);
  for (size_t j = 0; j < seqno_count; ++j) {
    td::uint32 esi = seqno_from + static_cast<td::uint32>(j);
    if (bcast->seen_parts.contains(esi)) {
      continue;
    }
    bcast->seen_parts.insert(esi);
    td::BufferSlice part = broadcast->parts_.from_slice(broadcast->parts_.as_slice().substr(j * part_size, part_size));
    // Stash systematic parts (id < k) so we can short-circuit decoding if all
    // K arrive. Clone is refcounted — cheap.
    if (esi < bcast->k && bcast->systematic_parts[esi].empty()) {
      bcast->systematic_parts[esi] = part.clone();
      bcast->systematic_filled++;
    }
    // After dispatching to the decode worker we no longer own the decoder.
    // Remaining bundles still help the fast path; skip the decoder feed.
    if (bcast->decoder) {
      CO_TRY(bcast->decoder->add_symbol({esi, std::move(part)}));
    }
    bcast->debug.symbols_received++;
  }
  VLOG(TWOSTEP_INFO) << "twostep RECV_BUNDLE receiver " << *bcast << " seqno_from=" << seqno_from
                     << " seqno_count=" << seqno_count << " from=" << src_peer_id
                     << " will_rebroadcast=" << will_rebroadcast
                     << " systematic=" << bcast->systematic_filled << "/" << bcast->k
                     << " local=" << overlay->local_id();

  // Fast path: all K systematic parts in hand → reconstruct via memcpy and
  // deliver without the ~20 ms FEC solver. The decode worker, if already
  // running, will see bcast->delivered when it finishes and discard.
  if (bcast->systematic_filled == bcast->k && !bcast->delivered) {
    td::BufferSlice data(bcast->data_size);
    for (size_t i = 0; i < bcast->k; ++i) {
      size_t offset = i * bcast->part_size;
      size_t len = std::min(bcast->part_size, bcast->data_size - offset);
      std::memcpy(data.as_slice().begin() + offset, bcast->systematic_parts[i].as_slice().begin(), len);
    }
    if (broadcast->data_hash_ != blake3_padded(data.as_slice(), bcast->k, bcast->part_size)) {
      co_return td::Status::Error(ErrorCode::protoviolation, "broadcast data hash mismatch (fast-path)");
    }
    bcast->delivered = true;
    bcast->decoder = {};
    bcast->systematic_parts.clear();
    VLOG(TWOSTEP_INFO) << "twostep FINISH receiver " << *bcast << " decoded=fast elapsed=" << bcast->debug.elapsed()
                       << " local=" << overlay->local_id();
    co_await check_and_deliver(overlay, src_keyhash, check_result, std::move(data), std::move(broadcast->extra_));
    co_return {};
  }

  // Slow path: enough symbols for the solver but no full systematic set yet.
  // Move the decoder onto a worker actor; this coroutine suspends waiting on
  // its result while the receiver actor keeps processing more bundles. If a
  // later bundle completes the systematic set, the fast path above wins and
  // we drop the worker's result when this coroutine resumes.
  if (bcast->decoder && !bcast->decode_dispatched && bcast->decoder->may_try_decode()) {
    bcast->decode_dispatched = true;
    auto [task, promise] = td::actor::StartedTask<td::BufferSlice>::make_bridge();
    td::actor::create_actor<TwostepDecodeWorker>("twostep-decode", std::move(bcast->decoder), std::move(promise),
                                                 broadcast_id, overlay->local_id())
        .release();
    auto R = co_await std::move(task).wrap();

    // Re-check existence: gc only runs after 25 s and decode is ~20 ms, but
    // be defensive in case future code changes that.
    auto it2 = broadcasts_.find(broadcast_id);
    if (it2 == broadcasts_.end()) {
      co_return {};
    }
    auto bcast2 = it2->second.get();
    if (bcast2->delivered) {
      co_return {};
    }
    if (R.is_error()) {
      co_return R.move_as_error();
    }
    auto data = R.move_as_ok();
    if (broadcast->data_hash_ != blake3_padded(data.as_slice(), bcast2->k, bcast2->part_size)) {
      co_return td::Status::Error(ErrorCode::protoviolation, "broadcast data hash mismatch");
    }
    bcast2->delivered = true;
    bcast2->systematic_parts.clear();
    VLOG(TWOSTEP_INFO) << "twostep FINISH receiver " << *bcast2 << " decoded=true elapsed=" << bcast2->debug.elapsed()
                       << " local=" << overlay->local_id();
    co_await check_and_deliver(overlay, src_keyhash, check_result, std::move(data), std::move(broadcast->extra_));
  }
  co_return {};
}

void BroadcastsTwostep::gc(OverlayImpl *overlay) {
  while (!broadcasts_.empty()) {
    auto bcast = static_cast<BroadcastTwostep *>(lru_.prev);
    CHECK(bcast);
    if (bcast->date > td::Clocks::system() - 25) {  // see OverlayImpl::check_date
      break;
    }
    auto broadcast_id = bcast->broadcast_id;

    if (!bcast->delivered) {
      FLOG(INFO) {
        sb << "twostep GC_INCOMPLETE receiver " << *bcast << " decoded=false elapsed=" << bcast->debug.elapsed() << " ";
        bcast->debug.print_senders(sb);
      };
    }
    CHECK(broadcasts_.erase(broadcast_id));
    overlay->register_delivered_broadcast(broadcast_id);
  }
}

}  // namespace overlay

}  // namespace ton
