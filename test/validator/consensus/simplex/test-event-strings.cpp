/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "consensus/simplex/bus.h"
#include "consensus/test/fixtures.h"
#include "td/utils/tests-snapshot.h"
#include "td/utils/tests.h"

namespace ton::validator::consensus::simplex {
namespace {

using namespace consensus::test;

TEST(SimplexEventStrings, BroadcastVote) {
  auto event = BroadcastVote{Vote{NotarizeVote{fixed_candidate_id(8)}}};
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(SimplexEventStrings, NotarizationObserved) {
  auto event = NotarizationObserved{
      fixed_candidate_id(8),
      fixed_certificate(NotarizeVote{fixed_candidate_id(8)}),
  };
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(SimplexEventStrings, FinalizationObserved) {
  auto event = FinalizationObserved{
      fixed_candidate_id(8),
      fixed_certificate(FinalizeVote{fixed_candidate_id(8)}),
  };
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(SimplexEventStrings, LeaderWindowObserved) {
  auto event = LeaderWindowObserved{
      .start_slot = 16,
      .base = fixed_parent_id(15),
  };
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(SimplexEventStrings, WaitForParent) {
  auto event = WaitForParent{fixed_candidate_full()};
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(SimplexEventStrings, ResolveCandidate) {
  auto event = ResolveCandidate{fixed_candidate_id(9)};
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(SimplexEventStrings, StoreCandidate) {
  auto event = StoreCandidate{fixed_candidate_full()};
  EXPECT_SNAPSHOT(event.contents_to_string());
}

TEST(SimplexEventStrings, ResolveState) {
  auto event = ResolveState{fixed_parent_id()};
  EXPECT_SNAPSHOT_NAMED("with_parent", event.contents_to_string());

  event = ResolveState{ParentId{std::nullopt}};
  EXPECT_SNAPSHOT_NAMED("genesis", event.contents_to_string());
}

TEST(SimplexEventStrings, ResolveState_Response) {
  auto response = ResolveState::Result{
      .state = fixed_chain_state(),
      .gen_utime_exact = 1700000000.0,
  };
  EXPECT_SNAPSHOT_NAMED("with_gen_utime", ResolveState::response_to_string(response));

  response = ResolveState::Result{
      .state = fixed_chain_state(),
      .gen_utime_exact = std::nullopt,
  };
  EXPECT_SNAPSHOT_NAMED("without_gen_utime", ResolveState::response_to_string(response));
}

TEST(SimplexEventStrings, SaveCertificate) {
  auto event = SaveCertificate{fixed_certificate(Vote{NotarizeVote{fixed_candidate_id(8)}})};
  EXPECT_SNAPSHOT(event.contents_to_string());
}

}  // namespace
}  // namespace ton::validator::consensus::simplex
