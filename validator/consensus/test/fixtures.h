/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <cstring>
#include <initializer_list>
#include <vector>

#include "block/signature-set.h"
#include "consensus/chain-state.h"
#include "consensus/misbehavior.h"
#include "consensus/simplex/certificate.h"
#include "consensus/stats.h"
#include "consensus/types.h"
#include "keys/keys.hpp"
#include "td/utils/Span.h"
#include "td/utils/buffer.h"
#include "ton/ton-types.h"
#include "validator/consensus/bus.h"
#include "validator/consensus/simplex/bus.h"
#include "validator/consensus/simplex/votes.h"
#include "vm/cells/CellBuilder.h"

namespace ton::validator::consensus::test {

// =============================================================================
// Snapshot-test fixtures: deterministic content for `*::contents_to_string`.
// =============================================================================

inline td::Bits256 fixed_bits256(td::uint8 byte) {
  td::Bits256 x;
  std::fill(x.data(), x.data() + 32, byte);
  return x;
}

inline BlockIdExt fixed_block_id_ext(BlockSeqno seqno = 1) {
  return BlockIdExt{BlockId{basechainId, shardIdAll, seqno}, fixed_bits256(0xAA), fixed_bits256(0xBB)};
}

inline ChainStateRef fixed_chain_state(BlockSeqno seqno = 1) {
  td::Ref<vm::Cell> state = vm::CellBuilder().store_long(0xfeedface, 32).store_long(seqno, 32).finalize_novm();
  return td::make_ref<ChainState>(ChainState::ZerostateTip{fixed_block_id_ext(seqno), state}, fixed_block_id_ext(0));
}

inline CandidateId fixed_candidate_id(td::uint32 slot = 7) {
  return CandidateId{slot, fixed_bits256(0xCC)};
}

inline ParentId fixed_parent_id(td::uint32 slot = 6) {
  return CandidateId{slot, fixed_bits256(0xDD)};
}

inline BlockCandidate fixed_block_candidate(BlockSeqno seqno = 1) {
  return BlockCandidate{Ed25519_PublicKey{fixed_bits256(0xEE)}, fixed_block_id_ext(seqno), fixed_bits256(0xFF),
                        td::BufferSlice{"data"}, td::BufferSlice{"collated"}};
}

inline CandidateRef fixed_candidate_full(BlockSeqno seqno = 1, td::uint32 slot = 7, td::uint32 leader_idx = 3) {
  return td::make_ref<Candidate>(fixed_candidate_id(slot), fixed_parent_id(slot - 1), PeerValidatorId{leader_idx},
                                 std::variant<BlockIdExt, BlockCandidate>{fixed_block_candidate(seqno)},
                                 td::BufferSlice{"sig"});
}

inline CandidateRef fixed_candidate_empty(td::uint32 slot = 7, td::uint32 leader_idx = 3) {
  return td::make_ref<Candidate>(fixed_candidate_id(slot), fixed_parent_id(slot - 1), PeerValidatorId{leader_idx},
                                 std::variant<BlockIdExt, BlockCandidate>{fixed_block_id_ext(2)},
                                 td::BufferSlice{"sig"});
}

// Two deterministic 64-byte ed25519-shaped signatures distinguished by their
// signer NodeIdShort, so contents_to_string output stays stable across runs.
inline std::vector<ton::BlockSignature> fixed_block_signatures() {
  std::vector<ton::BlockSignature> result;
  result.emplace_back(fixed_bits256(0x10), td::BufferSlice{std::string(64, '\x10')});
  result.emplace_back(fixed_bits256(0x20), td::BufferSlice{std::string(64, '\x20')});
  return result;
}

inline td::Ref<block::BlockSignatureSet> fixed_signature_set() {
  return block::BlockSignatureSet::create_ordinary(fixed_block_signatures(), /*cc_seqno=*/0,
                                                   /*validator_set_hash=*/0);
}

template <simplex::ValidVote V>
inline std::vector<typename simplex::Certificate<V>::VoteSignature> fixed_vote_signatures() {
  std::vector<typename simplex::Certificate<V>::VoteSignature> result;
  result.push_back({PeerValidatorId{1}, td::BufferSlice{std::string(64, '\x11')}});
  result.push_back({PeerValidatorId{2}, td::BufferSlice{std::string(64, '\x22')}});
  return result;
}

template <simplex::ValidVote V>
inline simplex::CertificateRef<V> fixed_certificate(V vote) {
  return td::make_ref<simplex::Certificate<V>>(std::move(vote), fixed_vote_signatures<V>());
}

inline ProtocolMessage fixed_protocol_message() {
  // 8 bytes -> falls into hex-dump branch of message_to_string.
  return ProtocolMessage{td::BufferSlice{td::Slice("\x01\x02\x03\x04\x05\x06\x07\x08", 8)}};
}

class StubMisbehavior : public Misbehavior {};

inline MisbehaviorRef fixed_misbehavior() {
  return td::make_ref<StubMisbehavior>();
}

inline std::unique_ptr<const stats::Event> fixed_trace_event() {
  return stats::Id::create(/*shard=*/ShardIdFull{basechainId, shardIdAll}, /*cc_seqno=*/0, /*idx=*/0,
                           /*total_validators=*/4, /*weight=*/1, /*total_weight=*/4,
                           /*slots_per_leader_window=*/4);
}

// =============================================================================
// Simplex unit-test harness setup
// =============================================================================

struct TestBusOptions {
  // Default-constructed ShardIdFull → workchain=workchainInvalid, shard=0.
  // Some integration-style tests prefer a real workchain shard; consensus unit
  // tests rarely care since the consensus actor doesn't introspect the shard.
  ShardIdFull shard{};
  std::vector<ValidatorWeight> weights{1, 1};
  PeerValidatorId local_idx{0};
  td::uint32 slots_per_leader_window = 4;
  td::uint32 cc_seqno = 42;
};

// Generate Ed25519 keys, fill validator_set / total_weight / local_id /
// session_id / config / shard / cc_seqno on `bus`. The `keys` vector is
// resized to one entry per validator; positions correspond to PeerValidatorId.
//
// Caller still invokes bus.populate_collator_schedule() if applicable
// (simplex::Bus needs it; consensus::Bus base does not).
void install_validators(consensus::Bus& bus, std::vector<PrivateKey>& keys, const TestBusOptions& opts = {});

constexpr td::Bits256 bits256(unsigned char x) {
  unsigned char arr[32] = {};
  for (auto& b : arr) {
    b = x;
  }
  return td::Bits256(arr);
}

inline td::Bits256 bits256_pattern(td::uint64 x) {
  td::uint64 data[4] = {x, x, x, x};
  unsigned char raw[32];
  std::memcpy(raw, data, 32);
  return td::Bits256(raw);
}

constexpr CandidateId make_candidate_id(td::uint32 slot = 0, unsigned char hash_byte = 0x7B) {
  return CandidateId{slot, bits256(hash_byte)};
}

inline CandidateId make_candidate_id_with_pattern(td::uint32 slot, td::uint64 pattern) {
  return CandidateId{slot, bits256_pattern(pattern)};
}

td::BufferSlice sign_with(const PrivateKey& key, td::Slice data);

// Wraps `payload` in a consensus_dataToSign with the bus session_id, then signs
// with validator at `signer_idx`.
td::BufferSlice sign_consensus_payload(const consensus::Bus& bus, td::Span<PrivateKey> keys, PeerValidatorId signer_idx,
                                       td::Slice payload);

td::BufferSlice vote_to_sign_payload(const consensus::Bus& bus, const simplex::Vote& vote);

// Returns the wire-form `consensus_simplex_vote` for `vote` signed by `signer_idx`.
td::BufferSlice serialize_signed_vote(const consensus::Bus& bus, td::Span<PrivateKey> keys, PeerValidatorId signer_idx,
                                      simplex::Vote vote);

// Multi-signer certificate. Default signers = all validators.
template <typename VoteT>
td::Ref<simplex::Certificate<VoteT>> make_certificate(const consensus::Bus& bus, td::Span<PrivateKey> keys, VoteT vote,
                                                      std::initializer_list<PeerValidatorId> signers = {});

// Same, ready for the wire (consensus_simplex_certificate).
template <typename VoteT>
td::BufferSlice serialize_certificate(const consensus::Bus& bus, td::Span<PrivateKey> keys, VoteT vote,
                                      std::initializer_list<PeerValidatorId> signers = {});

extern template td::Ref<simplex::Certificate<simplex::NotarizeVote>> make_certificate<simplex::NotarizeVote>(
    const consensus::Bus&, td::Span<PrivateKey>, simplex::NotarizeVote, std::initializer_list<PeerValidatorId>);
extern template td::Ref<simplex::Certificate<simplex::FinalizeVote>> make_certificate<simplex::FinalizeVote>(
    const consensus::Bus&, td::Span<PrivateKey>, simplex::FinalizeVote, std::initializer_list<PeerValidatorId>);
extern template td::Ref<simplex::Certificate<simplex::SkipVote>> make_certificate<simplex::SkipVote>(
    const consensus::Bus&, td::Span<PrivateKey>, simplex::SkipVote, std::initializer_list<PeerValidatorId>);

// Minimal Candidate ref with empty block contents — for Consensus tests that
// only care about the candidate's id/parent/leader plumbing, not its contents.
CandidateRef make_skeleton_candidate(CandidateId id = make_candidate_id());

ChainStateRef make_zerostate();
ChainStateRef make_normal_state(ShardIdFull shard, BlockSeqno seqno, BlockIdExt mc_block_id);

// Candidate bundle: candidate + cert + serialized forms + resolver request/response.
struct CandidateBundle {
  CandidateId id;
  CandidateRef candidate;
  simplex::NotarCertRef notar;
  td::BufferSlice serialized_candidate;
  ProtocolMessage resolve_request;
  ProtocolMessage resolve_response;

  td::BufferSlice db_key_candidate() const;
  td::BufferSlice db_key_candidate_info() const;
};

// Build a bundle for `slot` whose candidate is signed by validator 0 and whose
// notar cert is signed by all validators in the set.
CandidateBundle make_candidate_bundle(const consensus::Bus& bus, td::Span<PrivateKey> keys, td::uint32 slot = 0);

}  // namespace ton::validator::consensus::test
