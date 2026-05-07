/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "consensus/bus.h"
#include "consensus/test/fixtures.h"
#include "td/utils/tests-snapshot.h"
#include "td/utils/tests.h"

namespace ton::validator::consensus {
namespace {

using namespace test;

TEST(ConsensusEventStrings, Start) {
  auto event = Start{fixed_chain_state()};
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(ConsensusEventStrings, FinalizeBlock) {
  auto event = FinalizeBlock{fixed_candidate_full(), fixed_signature_set()};
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(ConsensusEventStrings, OurLeaderWindowStarted) {
  td::Time::FreezeGuard guard;

  auto event = OurLeaderWindowStarted{
      .base = fixed_parent_id(),
      .state = fixed_chain_state(),
      .start_slot = 10,
      .end_slot = 14,
      .start_time = td::Timestamp::at_unix(1700000000.0),
  };
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(ConsensusEventStrings, CandidateGenerated) {
  auto event = CandidateGenerated{fixed_candidate_full(), adnl::AdnlNodeIdShort{fixed_bits256(0x11)}};
  EXPECT_SNAPSHOT_NAMED("with_collator_id", event.contents_to_string());

  event = CandidateGenerated{fixed_candidate_full(), std::nullopt};
  EXPECT_SNAPSHOT_NAMED("without_collator_id", event.contents_to_string());
}

TEST(ConsensusEventStrings, CandidateReceived) {
  auto event = CandidateReceived{fixed_candidate_full()};
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(ConsensusEventStrings, ValidationRequest) {
  auto event = ValidationRequest{fixed_chain_state(), fixed_candidate_full()};
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(ConsensusEventStrings, IncomingProtocolMessage) {
  auto event = IncomingProtocolMessage{PeerValidatorId{2}, fixed_protocol_message()};
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(ConsensusEventStrings, OutgoingProtocolMessage) {
  auto event = OutgoingProtocolMessage{PeerValidatorId{5}, fixed_protocol_message()};
  EXPECT_SNAPSHOT_NAMED("with_recipient", event.contents_to_string());

  event = OutgoingProtocolMessage{std::nullopt, fixed_protocol_message()};
  EXPECT_SNAPSHOT_NAMED("broadcast", event.contents_to_string());
}

TEST(ConsensusEventStrings, IncomingOverlayRequest) {
  auto event = IncomingOverlayRequest{PeerValidatorId{1}, fixed_protocol_message()};
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(ConsensusEventStrings, OutgoingOverlayRequest) {
  td::Time::FreezeGuard guard;

  auto event = OutgoingOverlayRequest{PeerValidatorId{4}, td::Timestamp::in(2.5), fixed_protocol_message()};
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(ConsensusEventStrings, BlockFinalizedInMasterchain) {
  auto event = BlockFinalizedInMasterchain{fixed_block_id_ext(42)};
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(ConsensusEventStrings, MisbehaviorReport) {
  auto event = MisbehaviorReport{PeerValidatorId{6}, fixed_misbehavior()};
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(ConsensusEventStrings, TraceEvent) {
  auto event = TraceEvent{fixed_trace_event()};
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(ConsensusEventStrings, NoncriticalParamsUpdated) {
  auto event = NoncriticalParamsUpdated{NewConsensusConfig::NoncriticalParams{}};
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(ConsensusEventStrings, PrecheckCandidateBroadcast) {
  auto event = PrecheckCandidateBroadcast{
      .slot = 12,
      .broadcast_id = fixed_bits256(0x33),
      .signature_checked = true,
  };
  EXPECT_SNAPSHOT(event.contents_to_string());
}

}  // namespace
}  // namespace ton::validator::consensus
