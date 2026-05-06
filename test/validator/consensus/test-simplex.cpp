/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "auto/tl/ton_api.h"
#include "td/actor/TestScheduler.h"
#include "td/actor/common.h"
#include "td/utils/buffer.h"
#include "td/utils/tests.h"
#include "ton/ton-types.h"
#include "validator/consensus/simplex/bus.h"
#include "validator/consensus/simplex/stats.h"
#include "validator/consensus/stats.h"
#include "validator/consensus/test/fixtures.h"
#include "validator/consensus/test/harness.h"
#include "validator/consensus/test/mocks.h"

namespace ton::validator::consensus::test {
namespace {

namespace s = consensus::simplex;

// =============================================================================
// Candidate resolver tests
// =============================================================================

using CandidateResolverBus = TestBus<OutgoingOverlayRequest, DbGet, DbGetByPrefix, DbSet>;

class CandidateResolverTest : public SimplexTest<s::CandidateResolver, CandidateResolverBus> {
 protected:
  TestBusOptions options() const override {
    auto opts = SimplexTest<s::CandidateResolver, CandidateResolverBus>::options();
    opts.local_idx = PeerValidatorId{1};  // we are not the leader of the candidates we resolve
    return opts;
  }

  void configure(Bus& bus) override {
    bus.db = std::make_unique<MockDb<CandidateResolverBus>>(&bus);
  }

  static constexpr td::uint32 CANDIDATE_INFO_PREFIX =
      static_cast<td::uint32>(ton::ton_api::consensus_simplex_db_key_candidateResolver_candidateInfo::ID);

  td::actor::Task<> skip_startup() {
    co_await ts_.wait_sync_work();
    expect_events(DbGetByPrefix{CANDIDATE_INFO_PREFIX});
    co_return {};
  }
};

class OutgoingResolve : public CandidateResolverTest {
  td::actor::Task<> run_test() override {
    co_await skip_startup();
    auto bundle = make_candidate_bundle(*bh_, keys_);
    returns<OutgoingOverlayRequest>(std::move(bundle.resolve_response));
    s::ResolveCandidate::Result res = co_await send_request<s::ResolveCandidate>({bundle.id});
    expect_events(OutgoingOverlayRequest{PeerValidatorId(0), td::Timestamp::in(1), std::move(bundle.resolve_request)});
    EXPECT_EQ(*res.candidate, *bundle.candidate);
    EXPECT_EQ(*res.notar, *bundle.notar);
    co_return {};
  }
};
REGISTER_TEST(CandidateResolver, OutgoingResolve);

class IncomingResolve : public CandidateResolverTest {
  td::actor::Task<> run_test() override {
    co_await skip_startup();
    auto bundle = make_candidate_bundle(*bh_, keys_);
    co_await send_request<s::StoreCandidate>({bundle.candidate});
    expect_events(DbSet{bundle.db_key_candidate(), std::move(bundle.serialized_candidate)},
                  DbSet{bundle.db_key_candidate_info(), td::BufferSlice()});
    send_event<s::NotarizationObserved>({bundle.id, bundle.notar});
    ProtocolMessage res =
        co_await send_request<IncomingOverlayRequest>({PeerValidatorId(0), std::move(bundle.resolve_request)});
    EXPECT_EQ(res, bundle.resolve_response);
    expect_events();
    co_return {};
  }
};
REGISTER_TEST(CandidateResolver, IncomingResolve);

class InitialCandidates : public CandidateResolverTest {
  // Note: bundle is built lazily inside run_test() since `keys_` isn't populated
  // until the harness's run() begins, but configure() runs *during* that setup.
  // We instead pre-seed via configure() that captures a flag; the test rebuilds
  // the bundle in run_test() and asserts the resolver answers from disk.
  bool inject_done_ = false;
  CandidateBundle bundle_;

  void configure(Bus& bus) override {
    CandidateResolverTest::configure(bus);
    bundle_ = make_candidate_bundle(bus, keys_);
    auto& db = static_cast<MockDb<CandidateResolverBus>&>(*bus.db);
    db.seed(bundle_.db_key_candidate(), bundle_.serialized_candidate.clone());
    db.seed(bundle_.db_key_candidate_info(), td::BufferSlice());
    s::NotarCertRef notar = bundle_.notar;
    bus.bootstrap_certificates.push_back(std::move(notar.write()).consume_and_upcast());
    inject_done_ = true;
  }

  td::actor::Task<> run_test() override {
    CHECK(inject_done_);
    co_await ts_.wait_sync_work();
    expect_events(DbGetByPrefix{CANDIDATE_INFO_PREFIX});
    ProtocolMessage res =
        co_await send_request<IncomingOverlayRequest>({PeerValidatorId(0), std::move(bundle_.resolve_request)});
    EXPECT_EQ(res, bundle_.resolve_response);
    expect_events(DbGet{bundle_.db_key_candidate()});
    co_return {};
  }
};
REGISTER_TEST(CandidateResolver, InitialCandidates);

// =============================================================================
// Consensus tests
// =============================================================================

using ConsensusBus = TestBus<s::BroadcastVote, TraceEvent, s::ResolveState, OurLeaderWindowStarted, s::StoreCandidate,
                             s::WaitForParent, MisbehaviorReport, ValidationRequest>;

using ConsensusTest = SimplexTest<s::Consensus, ConsensusBus>;

class ConsensusBeforeStart : public ConsensusTest {
  td::actor::Task<> run_test() override {
    EXPECT_EQ(ts_.next_timeout_in(), std::numeric_limits<double>::infinity());
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
    returns<s::WaitForParent>(std::nullopt);
    returns<s::StoreCandidate>(td::Unit{});
    returns<s::ResolveState>({state, std::nullopt});
    returns<ValidationRequest>(ton::validator::CandidateAccept{0});
    returns<s::BroadcastVote>(td::Unit{});
    co_await ts_.wait_sync_work();
    expect_events(TraceEvent{stats::CandidateReceived::create(candidate, false)}, s::WaitForParent{candidate},
                  s::StoreCandidate{candidate}, s::ResolveState{std::nullopt}, ValidationRequest{state, candidate},
                  s::BroadcastVote{s::NotarizeVote{candidate->id}});
    // Slot 0: finalizing
    send_event(s::NotarizationObserved{id0a, {}});
    returns<s::BroadcastVote>(td::Unit{});
    co_await ts_.wait_sync_work();
    expect_events(s::BroadcastVote{s::FinalizeVote{id0a}});
    // Slot 1: not finalizing
    send_event(s::NotarizationObserved{id1b, {}});
    co_await ts_.wait_sync_work();
    expect_events();
    // Slot 2: notarizing
    const_cast<Candidate&>(*candidate).id = id2a;
    send_event(CandidateReceived{candidate});
    returns<s::WaitForParent>(std::nullopt);
    returns<s::StoreCandidate>(td::Unit{});
    returns<s::ResolveState>({state, std::nullopt});
    returns<ValidationRequest>(ton::validator::CandidateAccept{0});
    returns<s::BroadcastVote>(td::Unit{});
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
    EXPECT_EQ(std::round(ts_.next_timeout_in() * 1000), 3400);
    ts_.advance_time(ts_.next_timeout_in());
    returns<s::BroadcastVote>(td::Unit{});
    returns<s::BroadcastVote>(td::Unit{});
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
    returns<s::BroadcastVote>(td::Unit{});
    returns<s::BroadcastVote>(td::Unit{});
    returns<s::BroadcastVote>(td::Unit{});
    returns<s::BroadcastVote>(td::Unit{});
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
    returns<s::WaitForParent>(std::nullopt);
    returns<s::StoreCandidate>(td::Unit{});
    returns<s::ResolveState>({state, std::nullopt});
    returns<ValidationRequest>(ton::validator::CandidateAccept{0});
    returns<s::BroadcastVote>(td::Unit{});
    co_await ts_.wait_sync_work();
    expect_events(TraceEvent{stats::CandidateReceived::create(candidate, false)}, s::WaitForParent{candidate},
                  s::StoreCandidate{candidate}, s::ResolveState{std::nullopt}, ValidationRequest{state, candidate},
                  s::BroadcastVote{s::NotarizeVote{candidate->id}});
    send_event(s::NotarizationObserved{candidate->id, {}});
    returns<s::BroadcastVote>(td::Unit{});
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
    returns<s::WaitForParent>(std::nullopt);
    returns<s::StoreCandidate>(td::Unit{});
    returns<s::ResolveState>({state, std::nullopt});
    returns<ValidationRequest>(ton::validator::CandidateAccept{0});
    returns<s::BroadcastVote>(td::Unit{});
    returns<s::BroadcastVote>(td::Unit{});
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
    returns<s::WaitForParent>(std::nullopt);
    returns<s::StoreCandidate>(td::Unit{});
    returns<s::ResolveState>({state, std::nullopt});
    returns<ValidationRequest>(ton::validator::CandidateReject{{}, {}});
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
    EXPECT_EQ(std::round(ts_.next_timeout_in() * 1000), 3400);
    ts_.advance_time(ts_.next_timeout_in());
    returns<s::BroadcastVote>(td::Unit{});
    returns<s::BroadcastVote>(td::Unit{});
    returns<s::BroadcastVote>(td::Unit{});
    returns<s::BroadcastVote>(td::Unit{});
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
    returns<s::ResolveState>({state, std::nullopt});
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
    returns<s::WaitForParent>(misbehavior);
    returns<s::StoreCandidate>(td::Unit{});
    co_await ts_.wait_sync_work();
    expect_events(TraceEvent{stats::CandidateReceived::create(candidate, false)}, s::WaitForParent{candidate},
                  s::StoreCandidate{candidate}, MisbehaviorReport{candidate->leader, misbehavior});
    co_return {};
  }
};
REGISTER_TEST(Consensus, MisbehaviorReported);

// =============================================================================
// Pool tests
// =============================================================================

using PoolBus = TestBus<TraceEvent, OutgoingProtocolMessage, MisbehaviorReport, s::LeaderWindowObserved,
                        s::SaveCertificate, s::NotarizationObserved, s::FinalizationObserved, SignMessage>;

class PoolTest : public SimplexTest<s::Pool, PoolBus> {
 protected:
  TestBusOptions options() const override {
    auto opts = SimplexTest<s::Pool, PoolBus>::options();
    opts.local_idx = PeerValidatorId{1};  // manual default: we are validator 1
    return opts;
  }

  void configure(Bus& bus) override {
    bus.keyring = td::actor::create_actor<MockKeyring<PoolBus>>("MockKeyring", &bus, &keys_).release();
  }
};

class PoolBeforeStart : public PoolTest {
  td::actor::Task<> run_test() override {
    co_await ts_.wait_sync_work();
    expect_events();
    EXPECT_EQ(ts_.next_timeout_in(), std::numeric_limits<double>::infinity());
    co_return {};
  }
};
REGISTER_TEST(Pool, PoolBeforeStart);

class PoolAfterStart : public PoolTest {
  td::actor::Task<> run_test() override {
    send_event(Start{{}});
    returns<s::LeaderWindowObserved>(td::Unit{});
    co_await ts_.wait_sync_work();
    expect_events(
        TraceEvent{
            stats::Id::create(bh_->shard, bh_->cc_seqno, bh_->local_id.idx.value(), bh_->validator_set.size(),
                              bh_->local_id.weight, bh_->total_weight, bh_->config.slots_per_leader_window),
        },
        s::LeaderWindowObserved{0, std::nullopt});
    EXPECT_EQ(ts_.next_timeout_in(), 10);
    ts_.advance_time(ts_.next_timeout_in());
    co_await ts_.wait_sync_work();
    EXPECT_EQ(ts_.next_timeout_in(), 10);
    co_return {};
  }
};
REGISTER_TEST(Pool, PoolAfterStart);

class InitialState : public PoolTest {
  static constexpr s::NotarizeVote vote5{{5, bits256(78)}};
  static constexpr s::SkipVote vote4{4};
  static constexpr s::FinalizeVote vote3{{3, bits256(56)}};
  static constexpr s::NotarizeVote vote2{{2, bits256(34)}};
  static constexpr s::SkipVote vote1{1};
  static constexpr s::FinalizeVote vote0{{0, bits256(12)}};

  void configure(Bus& bus) override {
    PoolTest::configure(bus);
    bus.bootstrap_votes.push_back(vote5);
    bus.bootstrap_votes.push_back(vote4);
    bus.bootstrap_votes.push_back(vote3);
    bus.bootstrap_certificates.push_back(
        td::make_ref<s::Certificate<s::Vote>>(vote2, std::vector<s::Certificate<s::Vote>::VoteSignature>()));
    bus.bootstrap_certificates.push_back(
        td::make_ref<s::Certificate<s::Vote>>(vote1, std::vector<s::Certificate<s::Vote>::VoteSignature>()));
    bus.bootstrap_certificates.push_back(
        td::make_ref<s::Certificate<s::Vote>>(vote0, std::vector<s::Certificate<s::Vote>::VoteSignature>()));
  }

  td::actor::Task<> run_test() override {
    co_await ts_.wait_sync_work();
    expect_events(
        s::NotarizationObserved{
            vote2.id,
            td::make_ref<s::NotarCert>(vote2, std::vector<s::NotarCert::VoteSignature>()),
        },
        s::FinalizationObserved{
            vote0.id,
            td::make_ref<s::FinalCert>(vote0, std::vector<s::FinalCert::VoteSignature>()),
        },
        TraceEvent{s::stats::Voted::create(vote5)}, TraceEvent{s::stats::Voted::create(vote4)},
        TraceEvent{s::stats::Voted::create(vote3)},
        SignMessage{bh_->validator_set[1].short_id, vote_to_sign_payload(*bh_, vote5)},
        SignMessage{bh_->validator_set[1].short_id, vote_to_sign_payload(*bh_, vote4)},
        SignMessage{bh_->validator_set[1].short_id, vote_to_sign_payload(*bh_, vote3)});
    co_return {};
  }
};
REGISTER_TEST(Pool, InitialState);

class OurVote : public PoolTest {
  td::actor::Task<> run_test() override {
    s::Vote vote = s::NotarizeVote{make_candidate_id()};
    send_event(s::BroadcastVote{vote});
    co_await ts_.wait_sync_work();
    auto to_sign = vote_to_sign_payload(*bh_, vote);
    auto wire = serialize_signed_vote(*bh_, keys_, bh_->local_id.idx, vote);
    expect_events(TraceEvent{s::stats::Voted::create(vote)}, SignMessage{bh_->local_id.short_id, to_sign.clone()},
                  OutgoingProtocolMessage{std::nullopt, {std::move(wire)}});
    co_return {};
  }
};
REGISTER_TEST(Pool, OurVote);

}  // namespace
}  // namespace ton::validator::consensus::test
