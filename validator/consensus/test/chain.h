/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <memory>
#include <span>
#include <vector>

#include "consensus/bus.h"
#include "consensus/simplex/certificate.h"
#include "consensus/simplex/votes.h"
#include "keys/encryptor.h"
#include "td/utils/buffer.h"

#include "chain-state.h"

namespace ton::validator::consensus::test {

struct CandidateArtifact;
struct SkipArtifact;
struct Chain;

// A signed vote with its wire form (consensus_simplex_vote).
struct SignedVoteArtifact {
  simplex::Signed<simplex::Vote> signed_vote;
  td::BufferSlice wire;
};

// A multi-signer certificate, its wire form, and the per-signer votes that
// went into it. `votes` is in the order signers were given to the factory; the
// no-arg overloads use bus.validator_set order.
template <simplex::ValidVote V>
struct CertArtifact {
  simplex::CertificateRef<V> cert;
  td::BufferSlice wire;
  std::vector<SignedVoteArtifact> votes;
};

// CRTP base shared by Chain and CandidateArtifact. The only thing it adds is
// `propose(slot, opts)`, which produces a full candidate extending the
// derived's `base_state()` with the derived's `parent_for_child()` as parent.
template <typename Self>
struct ExtensionMixin {
  CandidateArtifact propose(td::uint32 slot, ExtendOptions opts = {}) const;
};

struct Chain : ExtensionMixin<Chain> {
  const consensus::Bus* bus = nullptr;
  std::span<const std::unique_ptr<Decryptor>> decryptors = {};
  ChainStateRef zerostate;

  // ExtensionMixin hooks.
  ParentId parent_for_child() const {
    return std::nullopt;
  }
  ChainStateRef base_state() const {
    return zerostate;
  }
  const Chain& signing_chain() const {
    return *this;
  }

  SkipArtifact skip(td::uint32 slot) const;

  // Lower-level building blocks; CandidateArtifact methods call these.
  CandidateArtifact build_full(td::uint32 slot, ParentId parent, BlockArtifact block, ChainStateRef post_state) const;
  CandidateArtifact build_empty(td::uint32 slot, CandidateId parent, BlockIdExt reference, ChainStateRef state_carry,
                                BlockIdExt last_full) const;

  SignedVoteArtifact signed_vote(simplex::Vote vote, PeerValidatorId signer) const;
  template <simplex::ValidVote V>
  CertArtifact<V> make_cert(V vote, std::span<const PeerValidatorId> signers) const;
};

struct CandidateArtifact : ExtensionMixin<CandidateArtifact> {
  CandidateRef ref;
  td::BufferSlice wire;
  ChainStateRef state;            // post-state for the next propose()
  BlockIdExt last_full_block_id;  // carried forward through propose_empty()
  Chain chain;

  CandidateId id() const {
    return ref->id;
  }
  ParentId parent_id() const {
    return ref->parent_id;
  }

  // ExtensionMixin hooks.
  ParentId parent_for_child() const {
    return id();
  }
  ChainStateRef base_state() const {
    return state;
  }
  const Chain& signing_chain() const {
    return chain;
  }

  // Empty candidates: honest form copies last_full_block_id forward;
  // misbehavior form takes an arbitrary reference.
  CandidateArtifact propose_empty(td::uint32 slot) const;
  CandidateArtifact propose_empty(td::uint32 slot, BlockIdExt reference) const;

  // Vote/cert factories. No-arg overloads sign with every validator.
  SignedVoteArtifact notar_vote(PeerValidatorId signer) const;
  SignedVoteArtifact final_vote(PeerValidatorId signer) const;
  CertArtifact<simplex::NotarizeVote> notar() const;
  CertArtifact<simplex::NotarizeVote> notar(std::span<const PeerValidatorId> signers) const;
  CertArtifact<simplex::FinalizeVote> final() const;
  CertArtifact<simplex::FinalizeVote> final(std::span<const PeerValidatorId> signers) const;

  // Wire-form packaging used by candidate-resolver tests.
  ProtocolMessage resolve_request() const;
  ProtocolMessage resolve_response() const;  // builds full-quorum notar internally
  ProtocolMessage resolve_response(const CertArtifact<simplex::NotarizeVote>&) const;

  td::BufferSlice db_key_candidate() const;
  td::BufferSlice db_key_candidate_info() const;
};

struct SkipArtifact {
  td::uint32 slot = 0;
  const Chain* chain_ = nullptr;

  SignedVoteArtifact vote(PeerValidatorId signer) const;
  CertArtifact<simplex::SkipVote> cert() const;
  CertArtifact<simplex::SkipVote> cert(std::span<const PeerValidatorId> signers) const;
};

template <typename Self>
inline CandidateArtifact ExtensionMixin<Self>::propose(td::uint32 slot, ExtendOptions opts) const {
  const Self& self = static_cast<const Self&>(*this);
  auto extended = extend(self.base_state(), std::move(opts));
  return self.signing_chain().build_full(slot, self.parent_for_child(), std::move(extended.block),
                                         std::move(extended.state));
}

}  // namespace ton::validator::consensus::test
