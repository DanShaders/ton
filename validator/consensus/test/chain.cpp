/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <utility>

#include "auto/tl/ton_api.h"
#include "common/checksum.h"
#include "tl-utils/common-utils.hpp"
#include "validator/consensus/test/chain.h"

namespace ton::validator::consensus::test {

namespace {

namespace tl = ton_api;

// All signed payloads in the consensus protocol are wrapped in `dataToSign`:
//   dataToSign{session_id, inner_serialization} → signed bytes.
td::BufferSlice wrap_to_sign(const consensus::Bus& bus, td::Slice inner) {
  return create_serialize_tl_object<tl::consensus_dataToSign>(bus.session_id, td::BufferSlice(inner));
}

td::BufferSlice sign_id(const Chain& chain, PeerValidatorId signer, CandidateId id) {
  auto inner = serialize_tl_object(id.to_tl(), true);
  auto to_sign = wrap_to_sign(*chain.bus, inner.as_slice());
  return chain.decryptors[signer.value()]->sign(to_sign.as_slice()).move_as_ok();
}

td::BufferSlice sign_vote(const Chain& chain, PeerValidatorId signer, const simplex::Vote& vote) {
  auto inner = serialize_tl_object(vote.to_tl(), true);
  auto to_sign = wrap_to_sign(*chain.bus, inner.as_slice());
  return chain.decryptors[signer.value()]->sign(to_sign.as_slice()).move_as_ok();
}

std::vector<PeerValidatorId> all_validator_ids(const consensus::Bus& bus) {
  std::vector<PeerValidatorId> ids;
  ids.reserve(bus.validator_set.size());
  for (const auto& v : bus.validator_set) {
    ids.push_back(v.idx);
  }
  return ids;
}

}  // namespace

// ---------------------------------------------------------------------------
// Chain
// ---------------------------------------------------------------------------

SkipArtifact Chain::skip(td::uint32 slot) const {
  return SkipArtifact{slot, this};
}

CandidateArtifact Chain::build_full(td::uint32 slot, ParentId parent, BlockArtifact block,
                                    ChainStateRef post_state) const {
  PeerValidator leader = bus->collator_schedule->expected_collator_for(slot).get_using(*bus);
  Ed25519_PublicKey leader_pubkey{leader.key.ed25519_value().raw()};

  td::Bits256 collated_file_hash = td::sha256_bits256(td::Slice());  // we don't model collated data here
  BlockCandidate block_candidate{
      leader_pubkey, block.id, collated_file_hash, block.raw_data.clone(), td::BufferSlice{},
  };

  CandidateId id = CandidateHashData::create_full(block_candidate, parent).build_id_with(slot);
  td::BufferSlice signature = sign_id(*this, leader.idx, id);

  auto candidate = td::make_ref<Candidate>(id, parent, leader.idx, std::move(block_candidate), signature.clone());
  auto wire = candidate->serialize();

  return CandidateArtifact{
      .ref = std::move(candidate),
      .wire = std::move(wire),
      .state = std::move(post_state),
      .last_full_block_id = block.id,
      .chain = *this,
  };
}

CandidateArtifact Chain::build_empty(td::uint32 slot, CandidateId parent, BlockIdExt reference,
                                     ChainStateRef state_carry, BlockIdExt last_full) const {
  PeerValidator leader = bus->collator_schedule->expected_collator_for(slot).get_using(*bus);

  CandidateId id = CandidateHashData::create_empty(reference, parent).build_id_with(slot);
  td::BufferSlice signature = sign_id(*this, leader.idx, id);

  auto candidate = td::make_ref<Candidate>(id, ParentId{parent}, leader.idx, reference, signature.clone());
  auto wire = candidate->serialize();

  return CandidateArtifact{
      .ref = std::move(candidate),
      .wire = std::move(wire),
      .state = std::move(state_carry),
      .last_full_block_id = last_full,
      .chain = *this,
  };
}

SignedVoteArtifact Chain::signed_vote(simplex::Vote vote, PeerValidatorId signer) const {
  td::BufferSlice signature = sign_vote(*this, signer, vote);
  simplex::Signed<simplex::Vote> signed_vote{signer, vote, signature.clone()};
  auto wire = create_serialize_tl_object<tl::consensus_simplex_vote>(vote.to_tl(), std::move(signature));
  return SignedVoteArtifact{std::move(signed_vote), std::move(wire)};
}

template <simplex::ValidVote V>
CertArtifact<V> Chain::make_cert(V vote, std::span<const PeerValidatorId> signers) const {
  using Sig = typename simplex::Certificate<V>::VoteSignature;
  std::vector<Sig> sigs;
  std::vector<SignedVoteArtifact> votes;
  sigs.reserve(signers.size());
  votes.reserve(signers.size());

  for (PeerValidatorId signer : signers) {
    td::BufferSlice signature = sign_vote(*this, signer, simplex::Vote{vote});
    // For the per-signer SignedVoteArtifact, re-wrap in the polymorphic Vote.
    simplex::Vote as_vote{vote};
    auto wire = create_serialize_tl_object<tl::consensus_simplex_vote>(as_vote.to_tl(), signature.clone());
    sigs.push_back(Sig{signer, signature.clone()});
    votes.push_back(SignedVoteArtifact{
        simplex::Signed<simplex::Vote>{signer, as_vote, signature.clone()},
        std::move(wire),
    });
  }

  auto cert = td::make_ref<simplex::Certificate<V>>(std::move(vote), std::move(sigs));
  auto wire = cert->serialize();
  return CertArtifact<V>{std::move(cert), std::move(wire), std::move(votes)};
}

template CertArtifact<simplex::NotarizeVote> Chain::make_cert<simplex::NotarizeVote>(
    simplex::NotarizeVote, std::span<const PeerValidatorId>) const;
template CertArtifact<simplex::FinalizeVote> Chain::make_cert<simplex::FinalizeVote>(
    simplex::FinalizeVote, std::span<const PeerValidatorId>) const;
template CertArtifact<simplex::SkipVote> Chain::make_cert<simplex::SkipVote>(simplex::SkipVote,
                                                                             std::span<const PeerValidatorId>) const;

// ---------------------------------------------------------------------------
// CandidateArtifact
// ---------------------------------------------------------------------------

CandidateArtifact CandidateArtifact::propose_empty(td::uint32 slot) const {
  return propose_empty(slot, last_full_block_id);
}

CandidateArtifact CandidateArtifact::propose_empty(td::uint32 slot, BlockIdExt reference) const {
  return chain.build_empty(slot, id(), reference, state, last_full_block_id);
}

SignedVoteArtifact CandidateArtifact::notar_vote(PeerValidatorId signer) const {
  return chain.signed_vote(simplex::Vote{simplex::NotarizeVote{id()}}, signer);
}

SignedVoteArtifact CandidateArtifact::final_vote(PeerValidatorId signer) const {
  return chain.signed_vote(simplex::Vote{simplex::FinalizeVote{id()}}, signer);
}

CertArtifact<simplex::NotarizeVote> CandidateArtifact::notar() const {
  auto ids = all_validator_ids(*chain.bus);
  return chain.make_cert(simplex::NotarizeVote{id()}, std::span<const PeerValidatorId>(ids));
}

CertArtifact<simplex::NotarizeVote> CandidateArtifact::notar(std::span<const PeerValidatorId> signers) const {
  return chain.make_cert(simplex::NotarizeVote{id()}, signers);
}

CertArtifact<simplex::FinalizeVote> CandidateArtifact::final() const {
  auto ids = all_validator_ids(*chain.bus);
  return chain.make_cert(simplex::FinalizeVote{id()}, std::span<const PeerValidatorId>(ids));
}

CertArtifact<simplex::FinalizeVote> CandidateArtifact::final(std::span<const PeerValidatorId> signers) const {
  return chain.make_cert(simplex::FinalizeVote{id()}, signers);
}

ProtocolMessage CandidateArtifact::resolve_request() const {
  return ProtocolMessage{create_serialize_tl_object<tl::consensus_simplex_requestCandidate>(
      id().to_tl(), /*want_candidate=*/true, /*want_notar=*/true)};
}

ProtocolMessage CandidateArtifact::resolve_response() const {
  return resolve_response(notar());
}

ProtocolMessage CandidateArtifact::resolve_response(const CertArtifact<simplex::NotarizeVote>& notar_cert) const {
  return ProtocolMessage{create_serialize_tl_object<tl::consensus_simplex_candidateAndCert>(
      wire.clone(), serialize_tl_object(notar_cert.cert->to_tl_vote_signature_set(), true))};
}

td::BufferSlice CandidateArtifact::db_key_candidate() const {
  return create_serialize_tl_object<tl::consensus_simplex_db_key_candidate>(id().to_tl());
}

td::BufferSlice CandidateArtifact::db_key_candidate_info() const {
  return create_serialize_tl_object<tl::consensus_simplex_db_key_candidateResolver_candidateInfo>(id().to_tl());
}

// ---------------------------------------------------------------------------
// SkipArtifact
// ---------------------------------------------------------------------------

SignedVoteArtifact SkipArtifact::vote(PeerValidatorId signer) const {
  return chain_->signed_vote(simplex::Vote{simplex::SkipVote{slot}}, signer);
}

CertArtifact<simplex::SkipVote> SkipArtifact::cert() const {
  auto ids = all_validator_ids(*chain_->bus);
  return chain_->make_cert(simplex::SkipVote{slot}, std::span<const PeerValidatorId>(ids));
}

CertArtifact<simplex::SkipVote> SkipArtifact::cert(std::span<const PeerValidatorId> signers) const {
  return chain_->make_cert(simplex::SkipVote{slot}, signers);
}

}  // namespace ton::validator::consensus::test
