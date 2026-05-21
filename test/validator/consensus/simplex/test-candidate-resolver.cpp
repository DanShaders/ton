/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <optional>
#include <utility>

#include "auto/tl/ton_api.h"
#include "td/actor/TestScheduler.h"
#include "td/utils/buffer.h"
#include "td/utils/tests.h"
#include "validator/consensus/simplex/bus.h"
#include "validator/consensus/test/harness.h"
#include "validator/consensus/test/mocks.h"

namespace ton::validator::consensus::test {
namespace {

namespace s = consensus::simplex;

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
    auto cand = chain().propose(/*slot=*/0);
    auto notar = cand.notar();
    queue_result_of<OutgoingOverlayRequest>(cand.resolve_response(notar));
    s::ResolveCandidate::Result res = co_await send_request<s::ResolveCandidate>({cand.id()});
    expect_events(OutgoingOverlayRequest{PeerValidatorId(0), td::Timestamp::in(1), cand.resolve_request()});
    EXPECT(td::actor::events_equal(*res.candidate, *cand.ref));
    EXPECT(td::actor::events_equal(*res.notar, *notar.cert));
    co_return {};
  }
};
REGISTER_TEST(CandidateResolver, OutgoingResolve);

class IncomingResolve : public CandidateResolverTest {
  td::actor::Task<> run_test() override {
    co_await skip_startup();
    auto cand = chain().propose(/*slot=*/0);
    auto notar = cand.notar();
    co_await send_request<s::StoreCandidate>({cand.ref});
    expect_events({
        DbSet{cand.db_key_candidate(), cand.wire.clone()},
        DbSet{cand.db_key_candidate_info(), td::BufferSlice()},
    });
    send_event<s::NotarizationObserved>({cand.id(), notar.cert});
    ProtocolMessage res = co_await send_request<IncomingOverlayRequest>({PeerValidatorId(0), cand.resolve_request()});
    EXPECT(td::actor::events_equal(res, cand.resolve_response(notar)));
    expect_events();
    co_return {};
  }
};
REGISTER_TEST(CandidateResolver, IncomingResolve);

class InitialCandidates : public CandidateResolverTest {
  std::optional<CandidateArtifact> cand_;
  std::optional<CertArtifact<simplex::NotarizeVote>> notar_;

  void configure(Bus& bus) override {
    CandidateResolverTest::configure(bus);
    cand_ = chain().propose(/*slot=*/0);
    notar_ = cand_->notar();
    auto& db = static_cast<MockDb<CandidateResolverBus>&>(*bus.db);
    db.seed(cand_->db_key_candidate(), cand_->wire.clone());
    db.seed(cand_->db_key_candidate_info(), td::BufferSlice());
    s::NotarCertRef notar_ref = notar_->cert;
    bus.bootstrap_certificates.push_back(std::move(notar_ref.write()).consume_and_upcast());
  }

  td::actor::Task<> run_test() override {
    CHECK(cand_.has_value());
    co_await ts_.wait_sync_work();
    expect_events(DbGetByPrefix{CANDIDATE_INFO_PREFIX});
    ProtocolMessage res = co_await send_request<IncomingOverlayRequest>({PeerValidatorId(0), cand_->resolve_request()});
    EXPECT(td::actor::events_equal(res, cand_->resolve_response(*notar_)));
    expect_events(DbGet{cand_->db_key_candidate()});
    co_return {};
  }
};
REGISTER_TEST(CandidateResolver, InitialCandidates);

}  // namespace
}  // namespace ton::validator::consensus::test
