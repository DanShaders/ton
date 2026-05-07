/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include "block/signature-set.h"
#include "consensus/chain-state.h"
#include "consensus/misbehavior.h"
#include "consensus/simplex/certificate.h"
#include "consensus/stats.h"
#include "consensus/types.h"
#include "vm/cells/CellBuilder.h"

namespace ton::validator::consensus::test {

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

}  // namespace ton::validator::consensus::test
