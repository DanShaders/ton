/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "td/actor/TestScheduler.h"
#include "td/utils/tests.h"
#include "validator/consensus/simplex/bus.h"
#include "validator/consensus/stats.h"
#include "validator/consensus/test/fixtures.h"
#include "validator/consensus/test/harness.h"

namespace ton::validator::consensus::test {
namespace {

using namespace std::chrono_literals;
namespace s = consensus::simplex;

using ConsensusBus = TestBus<s::BroadcastVote, TraceEvent, s::ResolveState, OurLeaderWindowStarted, s::StoreCandidate,
                             s::WaitForParent, MisbehaviorReport, ValidationRequest>;

using ConsensusTest = SimplexTest<s::Consensus, ConsensusBus>;

class ConsensusBeforeStart : public ConsensusTest {
  td::actor::Task<> run_test() override {
    EXPECT(!ts_.next_timeout().has_value());
    expect_events();
    co_return {};
  }
};
REGISTER_TEST(Consensus, ConsensusBeforeStart);

class BootstrapVotes : public ConsensusTest {
  static constexpr CandidateId id0a{0, bits256(42)};
  static constexpr CandidateId id0b{0, bits256(128)};
  static constexpr CandidateId id1a{1, bits256(84)};
  static constexpr CandidateId id1b{1, bits256(48)};
  static constexpr CandidateId id2a{2, bits256(123)};

  TestBusOptions options() const override {
    auto opts = ConsensusTest::options();
    opts.local_idx = PeerValidatorId{1};
    return opts;
  }

  void configure(Bus& bus) override {
    // Set slots_per_leader_window AFTER the harness built collator_schedule so
    // the schedule keeps its default-4-slot windows (under which validator 0
    // leads window 0 covering slots 0-3 and validator 1 leads window 1 covering
    // slots 4-7 — neither matches the consensus actor's runtime view of "slot 3
    // starts window 1"). The actor reads slots_per_leader_window at runtime so
    // it sees 3; the leader assignment that tells us "are we the leader?" still
    // sees 4. This mismatch was implicit in the manual test and is preserved
    // here intentionally — the test exercises the actor's slot-logic, not the
    // schedule.
    bus.config.slots_per_leader_window = 3;
    bus.bootstrap_votes.push_back(s::NotarizeVote{id0a});
    bus.bootstrap_votes.push_back(s::FinalizeVote{id1a});
    bus.bootstrap_votes.push_back(s::SkipVote{2});
    bus.bootstrap_votes.push_back(s::NotarizeVote{{3, bits256(3)}});
    bus.bootstrap_votes.push_back(s::FinalizeVote{{4, bits256(4)}});
    bus.bootstrap_votes.push_back(s::SkipVote{5});
  }

  td::actor::Task<> run_test() override {
    send_event(s::LeaderWindowObserved{0, std::nullopt});
    co_await ts_.wait_sync_work();
    expect_events();
    // Slot 0: not notarizing.
    // Note: starting id intentionally differs from any bootstrap_votes entry — the test
    // mutates the candidate's id below to drive the actor through its discipline checks.
    auto candidate = make_skeleton_candidate();
    send_event(CandidateReceived{candidate});
    co_await ts_.wait_sync_work();
    expect_events();
    const_cast<Candidate&>(*candidate).id = id0b;
    send_event(CandidateReceived{candidate});
    co_await ts_.wait_sync_work();
    expect_events();
    // Slot 1: notarizing
    const_cast<Candidate&>(*candidate).id = id1b;
    send_event(CandidateReceived{candidate});
    auto state = make_zerostate();
    queue_result_of<s::WaitForParent>(std::nullopt);
    queue_result_of<s::StoreCandidate>(td::Unit{});
    queue_result_of<s::ResolveState>({state, std::nullopt});
    queue_result_of<ValidationRequest>(ton::validator::CandidateAccept{0});
    co_await ts_.wait_sync_work();
    expect_events(TraceEvent{stats::CandidateReceived::create(candidate, false)}, s::WaitForParent{candidate},
                  s::StoreCandidate{candidate}, s::ResolveState{std::nullopt}, ValidationRequest{state, candidate},
                  s::BroadcastVote{s::NotarizeVote{candidate->id}});
    // Slot 0: finalizing
    send_event(s::NotarizationObserved{id0a, {}});
    co_await ts_.wait_sync_work();
    expect_events(s::BroadcastVote{s::FinalizeVote{id0a}});
    // Slot 1: not finalizing
    send_event(s::NotarizationObserved{id1b, {}});
    co_await ts_.wait_sync_work();
    expect_events();
    // Slot 2: notarizing
    const_cast<Candidate&>(*candidate).id = id2a;
    send_event(CandidateReceived{candidate});
    queue_result_of<s::WaitForParent>(std::nullopt);
    queue_result_of<s::StoreCandidate>(td::Unit{});
    queue_result_of<s::ResolveState>({state, std::nullopt});
    queue_result_of<ValidationRequest>(ton::validator::CandidateAccept{0});
    co_await ts_.wait_sync_work();
    expect_events(TraceEvent{stats::CandidateReceived::create(candidate, false)}, s::WaitForParent{candidate},
                  s::StoreCandidate{candidate}, s::ResolveState{std::nullopt}, ValidationRequest{state, candidate},
                  s::BroadcastVote{s::NotarizeVote{candidate->id}});
    // Slot 2: not finalizing
    send_event(s::NotarizationObserved{id2a, {}});
    co_await ts_.wait_sync_work();
    expect_events();
    // Timeout behavior
    send_event(s::LeaderWindowObserved{3, std::nullopt});
    co_await ts_.wait_sync_work();
    expect_events();
    EXPECT_EQ(ts_.next_timeout(), td::Timestamp::in(3400ms));
    ts_.advance_time_to(*ts_.next_timeout());
    co_await ts_.wait_sync_work();
    expect_events(s::BroadcastVote{s::SkipVote{3}}, s::BroadcastVote{s::SkipVote{5}});
    co_return {};
  }
};
REGISTER_TEST(Consensus, BootstrapVotes);

class OldSlotsSkipped : public ConsensusTest {
  TestBusOptions options() const override {
    auto opts = ConsensusTest::options();
    opts.local_idx = PeerValidatorId{1};
    return opts;
  }

  void configure(Bus& bus) override {
    bus.first_nonannounced_window = 1;
  }

  td::actor::Task<> run_test() override {
    co_await ts_.wait_sync_work();
    expect_events(s::BroadcastVote{s::SkipVote{0}}, s::BroadcastVote{s::SkipVote{1}}, s::BroadcastVote{s::SkipVote{2}},
                  s::BroadcastVote{s::SkipVote{3}});
    co_return {};
  }
};
REGISTER_TEST(Consensus, OldSlotsSkipped);

class Finalization : public ConsensusTest {
  TestBusOptions options() const override {
    auto opts = ConsensusTest::options();
    opts.local_idx = PeerValidatorId{1};
    return opts;
  }

  td::actor::Task<> run_test() override {
    auto candidate = make_skeleton_candidate();
    auto state = make_zerostate();
    send_event(s::LeaderWindowObserved{0, candidate->parent_id});
    send_event(CandidateReceived{candidate});
    queue_result_of<s::WaitForParent>(std::nullopt);
    queue_result_of<s::StoreCandidate>(td::Unit{});
    queue_result_of<s::ResolveState>({state, std::nullopt});
    queue_result_of<ValidationRequest>(ton::validator::CandidateAccept{0});
    co_await ts_.wait_sync_work();
    expect_events(TraceEvent{stats::CandidateReceived::create(candidate, false)}, s::WaitForParent{candidate},
                  s::StoreCandidate{candidate}, s::ResolveState{std::nullopt}, ValidationRequest{state, candidate},
                  s::BroadcastVote{s::NotarizeVote{candidate->id}});
    send_event(s::NotarizationObserved{candidate->id, {}});
    co_await ts_.wait_sync_work();
    expect_events(s::BroadcastVote{s::FinalizeVote{candidate->id}});
    co_return {};
  }
};
REGISTER_TEST(Consensus, Finalization);

class FinalizationOutOfOrder : public ConsensusTest {
  TestBusOptions options() const override {
    auto opts = ConsensusTest::options();
    opts.local_idx = PeerValidatorId{1};
    return opts;
  }

  td::actor::Task<> run_test() override {
    auto candidate = make_skeleton_candidate();
    auto state = make_zerostate();
    send_event(s::LeaderWindowObserved{0, candidate->parent_id});
    send_event(s::NotarizationObserved{candidate->id, {}});
    co_await ts_.wait_sync_work();
    expect_events();
    send_event(CandidateReceived{candidate});
    queue_result_of<s::WaitForParent>(std::nullopt);
    queue_result_of<s::StoreCandidate>(td::Unit{});
    queue_result_of<s::ResolveState>({state, std::nullopt});
    queue_result_of<ValidationRequest>(ton::validator::CandidateAccept{0});
    co_await ts_.wait_sync_work();
    expect_events(TraceEvent{stats::CandidateReceived::create(candidate, false)}, s::WaitForParent{candidate},
                  s::StoreCandidate{candidate}, s::ResolveState{std::nullopt}, ValidationRequest{state, candidate},
                  s::BroadcastVote{s::NotarizeVote{candidate->id}}, s::BroadcastVote{s::FinalizeVote{candidate->id}});
    co_return {};
  }
};
REGISTER_TEST(Consensus, FinalizationOutOfOrder);

class ValidationRejected : public ConsensusTest {
  TestBusOptions options() const override {
    auto opts = ConsensusTest::options();
    opts.local_idx = PeerValidatorId{1};
    return opts;
  }

  td::actor::Task<> run_test() override {
    auto candidate = make_skeleton_candidate();
    auto state = make_zerostate();
    send_event(s::LeaderWindowObserved{0, candidate->parent_id});
    send_event(CandidateReceived{candidate});
    queue_result_of<s::WaitForParent>(std::nullopt);
    queue_result_of<s::StoreCandidate>(td::Unit{});
    queue_result_of<s::ResolveState>({state, std::nullopt});
    queue_result_of<ValidationRequest>(ton::validator::CandidateReject{{}, {}});
    co_await ts_.wait_sync_work();
    expect_events(TraceEvent{stats::CandidateReceived::create(candidate, false)}, s::WaitForParent{candidate},
                  s::StoreCandidate{candidate}, s::ResolveState{std::nullopt}, ValidationRequest{state, candidate});
    co_return {};
  }
};
REGISTER_TEST(Consensus, ValidationRejected);

class SkipTimeout : public ConsensusTest {
  TestBusOptions options() const override {
    auto opts = ConsensusTest::options();
    opts.local_idx = PeerValidatorId{1};
    return opts;
  }

  td::actor::Task<> run_test() override {
    send_event(s::LeaderWindowObserved{0, std::nullopt});
    co_await ts_.wait_sync_work();
    EXPECT_EQ(ts_.next_timeout(), td::Timestamp::in(3400ms));
    ts_.advance_time_to(*ts_.next_timeout());
    co_await ts_.wait_sync_work();
    expect_events(s::BroadcastVote{s::SkipVote{0}}, s::BroadcastVote{s::SkipVote{1}}, s::BroadcastVote{s::SkipVote{2}},
                  s::BroadcastVote{s::SkipVote{3}});
    co_return {};
  }
};
REGISTER_TEST(Consensus, SkipTimeout);

class GenerationStarted : public ConsensusTest {
  td::actor::Task<> run_test() override {
    send_event(s::LeaderWindowObserved{0, std::nullopt});
    auto state = make_zerostate();
    queue_result_of<s::ResolveState>({state, std::nullopt});
    co_await ts_.wait_sync_work();
    expect_events(s::ResolveState{std::nullopt},
                  OurLeaderWindowStarted{std::nullopt, state, 0, 4, td::Timestamp::now()});
    co_return {};
  }
};
REGISTER_TEST(Consensus, GenerationStarted);

class MisbehaviorReported : public ConsensusTest {
  TestBusOptions options() const override {
    auto opts = ConsensusTest::options();
    opts.local_idx = PeerValidatorId{1};
    return opts;
  }

  td::actor::Task<> run_test() override {
    auto candidate = make_skeleton_candidate();
    auto misbehavior = td::make_ref<Misbehavior>();
    send_event(s::LeaderWindowObserved{0, candidate->parent_id});
    send_event(CandidateReceived{candidate});
    queue_result_of<s::WaitForParent>(misbehavior);
    queue_result_of<s::StoreCandidate>(td::Unit{});
    co_await ts_.wait_sync_work();
    expect_events(TraceEvent{stats::CandidateReceived::create(candidate, false)}, s::WaitForParent{candidate},
                  s::StoreCandidate{candidate}, MisbehaviorReport{candidate->leader, misbehavior});
    co_return {};
  }
};
REGISTER_TEST(Consensus, MisbehaviorReported);

}  // namespace
}  // namespace ton::validator::consensus::test
