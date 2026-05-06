/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "validator/consensus/test/fixtures.h"

#include <utility>

#include "auto/tl/ton_api.h"
#include "crypto/block/fixtures.h"
#include "crypto/vm/boc.h"
#include "crypto/vm/cells/CellBuilder.h"
#include "td/utils/Status.h"
#include "td/utils/check.h"
#include "td/utils/crypto.h"
#include "tl-utils/common-utils.hpp"
#include "validator-session/candidate-serializer.h"
#include "validator/fabric.h"

namespace ton::validator::consensus::test {

void install_validators(consensus::Bus& bus, std::vector<PrivateKey>& keys, const TestBusOptions& opts) {
  CHECK(!opts.weights.empty());

  bus.session_id = bits256_pattern(0xDEADBEEFCAFEBABEull);
  bus.shard = opts.shard;
  bus.cc_seqno = opts.cc_seqno;
  bus.validator_set_hash = 0xDEADBEEF;
  bus.config.slots_per_leader_window = opts.slots_per_leader_window;
  bus.stop_promise = [](td::Result<>) {};

  keys.clear();
  keys.reserve(opts.weights.size());
  bus.validator_set.clear();
  bus.validator_set.reserve(opts.weights.size());
  bus.total_weight = 0;

  for (size_t i = 0; i < opts.weights.size(); ++i) {
    PrivateKey priv{privkeys::Ed25519::random()};
    PublicKey pub = priv.compute_public_key();
    PublicKeyHash short_id = pub.compute_short_id();
    bus.validator_set.push_back(PeerValidator{
        .idx = PeerValidatorId{i},
        .key = std::move(pub),
        .short_id = short_id,
        .adnl_id = {},
        .weight = opts.weights[i],
    });
    bus.total_weight += opts.weights[i];
    keys.push_back(std::move(priv));
  }

  CHECK(opts.local_idx.value() < opts.weights.size());
  bus.local_id = bus.validator_set[opts.local_idx.value()];
}

td::BufferSlice sign_with(const PrivateKey& key, td::Slice data) {
  auto decryptor = key.create_decryptor().move_as_ok();
  return decryptor->sign(data).move_as_ok();
}

td::BufferSlice sign_consensus_payload(const consensus::Bus& bus, td::Span<PrivateKey> keys,
                                       PeerValidatorId signer_idx, td::Slice payload) {
  auto signed_data = create_serialize_tl_object<ton_api::consensus_dataToSign>(bus.session_id, td::BufferSlice(payload));
  CHECK(signer_idx.value() < keys.size());
  return sign_with(keys[signer_idx.value()], signed_data.as_slice());
}

td::BufferSlice vote_to_sign_payload(const consensus::Bus& bus, const simplex::Vote& vote) {
  return create_serialize_tl_object<ton_api::consensus_dataToSign>(bus.session_id,
                                                                   serialize_tl_object(vote.to_tl(), true));
}

td::BufferSlice serialize_signed_vote(const consensus::Bus& bus, td::Span<PrivateKey> keys,
                                      PeerValidatorId signer_idx, simplex::Vote vote) {
  auto unsigned_tl = vote.to_tl();
  auto to_sign = create_serialize_tl_object<ton_api::consensus_dataToSign>(bus.session_id,
                                                                           serialize_tl_object(unsigned_tl, true));
  auto signature = sign_with(keys[signer_idx.value()], to_sign.as_slice());
  return create_serialize_tl_object<ton_api::consensus_simplex_vote>(std::move(unsigned_tl), std::move(signature));
}

namespace {

template <typename VoteT>
std::vector<typename simplex::Certificate<VoteT>::VoteSignature> build_vote_signatures(
    const consensus::Bus& bus, td::Span<PrivateKey> keys, const VoteT& vote,
    std::initializer_list<PeerValidatorId> signers) {
  auto unsigned_payload = serialize_tl_object(vote.to_tl(), true);
  auto to_sign = create_serialize_tl_object<ton_api::consensus_dataToSign>(bus.session_id, std::move(unsigned_payload));

  std::vector<typename simplex::Certificate<VoteT>::VoteSignature> sigs;
  auto add = [&](PeerValidatorId idx) {
    sigs.push_back({idx, sign_with(keys[idx.value()], to_sign.as_slice())});
  };
  if (signers.size() == 0) {
    for (const auto& v : bus.validator_set) {
      add(v.idx);
    }
  } else {
    for (auto idx : signers) {
      add(idx);
    }
  }
  return sigs;
}

}  // namespace

template <typename VoteT>
td::Ref<simplex::Certificate<VoteT>> make_certificate(const consensus::Bus& bus, td::Span<PrivateKey> keys,
                                                      VoteT vote, std::initializer_list<PeerValidatorId> signers) {
  auto sigs = build_vote_signatures(bus, keys, vote, signers);
  return td::make_ref<simplex::Certificate<VoteT>>(std::move(vote), std::move(sigs));
}

template <typename VoteT>
td::BufferSlice serialize_certificate(const consensus::Bus& bus, td::Span<PrivateKey> keys, VoteT vote,
                                      std::initializer_list<PeerValidatorId> signers) {
  return make_certificate(bus, keys, std::move(vote), signers)->serialize();
}

template td::Ref<simplex::Certificate<simplex::NotarizeVote>> make_certificate<simplex::NotarizeVote>(
    const consensus::Bus&, td::Span<PrivateKey>, simplex::NotarizeVote, std::initializer_list<PeerValidatorId>);
template td::Ref<simplex::Certificate<simplex::FinalizeVote>> make_certificate<simplex::FinalizeVote>(
    const consensus::Bus&, td::Span<PrivateKey>, simplex::FinalizeVote, std::initializer_list<PeerValidatorId>);
template td::Ref<simplex::Certificate<simplex::SkipVote>> make_certificate<simplex::SkipVote>(
    const consensus::Bus&, td::Span<PrivateKey>, simplex::SkipVote, std::initializer_list<PeerValidatorId>);

template td::BufferSlice serialize_certificate<simplex::NotarizeVote>(const consensus::Bus&,
                                                                      td::Span<PrivateKey>, simplex::NotarizeVote,
                                                                      std::initializer_list<PeerValidatorId>);
template td::BufferSlice serialize_certificate<simplex::FinalizeVote>(const consensus::Bus&,
                                                                      td::Span<PrivateKey>, simplex::FinalizeVote,
                                                                      std::initializer_list<PeerValidatorId>);
template td::BufferSlice serialize_certificate<simplex::SkipVote>(const consensus::Bus&, td::Span<PrivateKey>,
                                                                  simplex::SkipVote,
                                                                  std::initializer_list<PeerValidatorId>);

CandidateRef make_skeleton_candidate(CandidateId id) {
  return td::make_ref<Candidate>(id, std::nullopt, PeerValidatorId(),
                                 BlockCandidate({}, {}, {}, {}, {}), td::BufferSlice());
}

ChainStateRef make_zerostate() {
  return td::make_ref<ChainState>(ChainState::ZerostateTip{{}, {}}, BlockIdExt{});
}

ChainStateRef make_normal_state(ShardIdFull shard, BlockSeqno seqno, BlockIdExt mc_block_id) {
  auto state_root = vm::CellBuilder().store_long(seqno, 32).finalize_novm();
  auto state_update = block::test::make_merkle_update(state_root, state_root);
  auto cell = block::test::make_block_cell(shard, seqno, std::move(state_update));
  auto data = vm::std_boc_serialize(cell, 31).move_as_ok();
  BlockIdExt id(BlockId(shard, seqno), td::Bits256(cell->get_hash().bits()), td::sha256_bits256(data));
  auto block = create_block(id, data.clone()).move_as_ok();
  return td::make_ref<ChainState>(ChainState::NormalTip{std::move(block), std::move(state_root)}, mc_block_id);
}

td::BufferSlice CandidateBundle::db_key_candidate() const {
  return create_serialize_tl_object<ton_api::consensus_simplex_db_key_candidate>(
      create_tl_object<ton_api::consensus_candidateId>(id.slot, id.hash));
}

td::BufferSlice CandidateBundle::db_key_candidate_info() const {
  return create_serialize_tl_object<ton_api::consensus_simplex_db_key_candidateResolver_candidateInfo>(
      create_tl_object<ton_api::consensus_candidateId>(id.slot, id.hash));
}

CandidateBundle make_candidate_bundle(const consensus::Bus& bus, td::Span<PrivateKey> keys, td::uint32 slot) {
  // Empty cell-as-block + empty collated data — the simplest thing that round-trips through
  // candidate serialization. Tests that need richer block contents should build their own
  // BlockCandidate via crypto/block/fixtures.h and feed it in.
  auto root = vm::CellBuilder().store_long(0, 0).finalize_novm();
  auto block_data = vm::std_boc_serialize(root, 31).move_as_ok();
  td::BufferSlice collated_data;

  td::Bits256 block_data_hash = td::sha256_bits256(block_data.as_slice());
  td::Bits256 root_hash = bits256(0x1F);
  td::Bits256 collated_data_hash = td::sha256_bits256(collated_data.as_slice());
  // Match the deserialize path in validator/consensus/types.cpp: the resolver
  // reconstructs BlockIdExt from `bus.shard` and the inner candidate's `round_`.
  // Use the same shard here so the round-tripped candidate hash matches.
  constexpr BlockSeqno seqno = 0;
  td::Bits256 candidate_hash = create_hash_tl_object<ton_api::consensus_candidateHashDataOrdinary>(
      create_tl_object<ton_api::tonNode_blockIdExt>(bus.shard.workchain, bus.shard.shard, seqno, root_hash,
                                                    block_data_hash),
      collated_data_hash, create_tl_object<ton_api::consensus_candidateWithoutParents>());

  CandidateId candidate_id{slot, candidate_hash};
  auto serialized_inner = validatorsession::serialize_candidate(
      create_tl_object<ton_api::validatorSession_candidate>(bits256(0), seqno, root_hash, block_data.clone(),
                                                            collated_data.clone()),
      true).move_as_ok();

  auto candidate_signature = sign_consensus_payload(
      bus, keys, PeerValidatorId{0},
      create_serialize_tl_object<ton_api::consensus_candidateId>(slot, candidate_hash).as_slice());

  auto serialized_candidate = create_serialize_tl_object<ton_api::consensus_block>(
      slot, create_tl_object<ton_api::consensus_candidateWithoutParents>(), std::move(serialized_inner),
      candidate_signature.clone());

  auto notar_sigs = build_vote_signatures(bus, keys, simplex::NotarizeVote{candidate_id}, {});

  std::vector<tl_object_ptr<ton_api::consensus_simplex_voteSignature>> notar_sigs_tl;
  notar_sigs_tl.reserve(notar_sigs.size());
  for (auto& s : notar_sigs) {
    notar_sigs_tl.push_back(create_tl_object<ton_api::consensus_simplex_voteSignature>(
        static_cast<td::int32>(s.validator.value()), s.signature.clone()));
  }

  auto notar = td::make_ref<simplex::NotarCert>(simplex::NotarizeVote{candidate_id}, std::move(notar_sigs));

  CandidateRef candidate = td::make_ref<Candidate>(
      candidate_id, std::nullopt, PeerValidatorId{0},
      BlockCandidate{Ed25519_PublicKey(bus.validator_set[0].key.ed25519_value().raw()),
                     {{bus.shard, seqno}, root_hash, block_data_hash},
                     collated_data_hash, block_data.clone(), collated_data.clone()},
      candidate_signature.clone());

  ProtocolMessage request{create_serialize_tl_object<ton_api::consensus_simplex_requestCandidate>(
      create_tl_object<ton_api::consensus_candidateId>(slot, candidate_hash), true, true)};
  ProtocolMessage response{create_serialize_tl_object<ton_api::consensus_simplex_candidateAndCert>(
      serialized_candidate.clone(),
      create_serialize_tl_object<ton_api::consensus_simplex_voteSignatureSet>(std::move(notar_sigs_tl)))};

  return CandidateBundle{
      .id = candidate_id,
      .candidate = std::move(candidate),
      .notar = std::move(notar),
      .serialized_candidate = std::move(serialized_candidate),
      .resolve_request = std::move(request),
      .resolve_response = std::move(response),
  };
}

}  // namespace ton::validator::consensus::test
