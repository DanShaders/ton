/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <utility>
#include <vector>

#include "td/actor/TestScheduler.h"
#include "td/actor/common.h"
#include "td/utils/buffer.h"
#include "td/utils/tests.h"
#include "validator/consensus/simplex/bus.h"
#include "validator/consensus/simplex/stats.h"
#include "validator/consensus/stats.h"
#include "validator/consensus/test/fixtures.h"
#include "validator/consensus/test/harness.h"
#include "validator/consensus/test/mocks.h"

namespace ton::validator::consensus::test {
namespace {

using namespace std::chrono_literals;
namespace s = consensus::simplex;

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
    EXPECT(!ts_.next_timeout().has_value());
    co_return {};
  }
};
REGISTER_TEST(Pool, PoolBeforeStart);

class PoolAfterStart : public PoolTest {
  td::actor::Task<> run_test() override {
    send_event(Start{{}});
    co_await ts_.wait_sync_work();
    expect_events(
        TraceEvent{
            stats::Id::create(bh_->shard, bh_->cc_seqno, bh_->local_id.idx.value(), bh_->validator_set.size(),
                              bh_->local_id.weight, bh_->total_weight, bh_->config.slots_per_leader_window),
        },
        s::LeaderWindowObserved{0, std::nullopt});
    EXPECT_EQ(ts_.next_timeout(), td::Timestamp::in(10s));
    ts_.advance_time_to(*ts_.next_timeout());
    co_await ts_.wait_sync_work();
    EXPECT_EQ(ts_.next_timeout(), td::Timestamp::in(10s));
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
