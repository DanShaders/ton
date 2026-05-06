/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <cstring>
#include <initializer_list>
#include <vector>

#include "keys/keys.hpp"
#include "td/utils/Span.h"
#include "td/utils/buffer.h"
#include "ton/ton-types.h"
#include "validator/consensus/bus.h"
#include "validator/consensus/chain-state.h"
#include "validator/consensus/simplex/bus.h"
#include "validator/consensus/simplex/certificate.h"
#include "validator/consensus/simplex/votes.h"
#include "validator/consensus/types.h"

namespace ton::validator::consensus::test {

// =============================================================================
// Bus setup
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

// =============================================================================
// Constexpr helpers — small enough to live inline.
// =============================================================================

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

// =============================================================================
// Signing
// =============================================================================

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

// =============================================================================
// Chain state
// =============================================================================

ChainStateRef make_zerostate();
ChainStateRef make_normal_state(ShardIdFull shard, BlockSeqno seqno, BlockIdExt mc_block_id);

// =============================================================================
// Candidate bundle: candidate + cert + serialized forms + resolver request/response.
// Replaces hand-built MockCandidate.
// =============================================================================

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
