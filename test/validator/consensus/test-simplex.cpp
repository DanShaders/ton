/*
    This file is part of TON Blockchain Library.

    TON Blockchain Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    TON Blockchain Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with TON Blockchain Library.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2017-2020 Telegram Systems LLP
*/

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <deque>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "common/refcnt.hpp"
#include "keyring/keyring.h"
#include "td/actor/BusRuntime.h"
#include "td/actor/TestScheduler.h"
#include "td/actor/common.h"
#include "td/utils/Span.h"
#include "td/utils/Status.h"
#include "td/utils/buffer.h"
#include "td/utils/tests.h"
#include "tl-utils/common-utils.hpp"
#include "ton/ton-types.h"
#include "validator-session/candidate-serializer.h"
#include "validator/consensus/simplex/bus.h"
#include "validator/consensus/simplex/stats.h"
#include "validator/consensus/stats.h"
#include "vm/boc.h"
#include "vm/cells.h"

template <typename, typename>
struct InHelper;

template <typename T>
struct InHelper<T, std::tuple<>> : std::false_type {};

template <typename T, typename... Ts>
struct InHelper<T, std::tuple<T, Ts...>> : std::true_type {};

template <typename T, typename U, typename... Ts>
struct InHelper<T, std::tuple<U, Ts...>> : InHelper<T, std::tuple<Ts...>> {};

template <typename T, typename L>
concept In = InHelper<T, L>::value;

template <typename L, In<L>>
struct IndexOfHelper;

template <typename T, typename... Ts>
struct IndexOfHelper<std::tuple<T, Ts...>, T> : std::integral_constant<std::size_t, 0> {};

template <typename T, typename... Ts, typename U>
struct IndexOfHelper<std::tuple<T, Ts...>, U>
    : std::integral_constant<std::size_t, IndexOfHelper<std::tuple<Ts...>, U>::value + 1> {};

template <typename L, In<L> T>
inline constexpr std::size_t IndexOf = IndexOfHelper<L, T>::value;

template <typename, typename>
struct ConsHelper;

template <typename T, typename... Ts>
struct ConsHelper<T, std::tuple<Ts...>> {
  using type = std::tuple<T, Ts...>;
};

template <typename T, typename Ts>
using Cons = ConsHelper<T, Ts>::type;

using td::actor::detail::ValidPublishTargetFor, td::actor::detail::ValidRequestFor;
template <typename T>
concept Returns = requires { typename T::ReturnType; };

template <typename, typename>
struct TargetsHelper;

template <typename B>
struct TargetsHelper<B, std::tuple<>> {
  using type = std::tuple<>;
};

template <typename B, ValidPublishTargetFor<B> T, typename... Ts>
struct TargetsHelper<B, std::tuple<T, Ts...>> {
  using type = Cons<T, typename TargetsHelper<B, std::tuple<Ts...>>::type>;
};

template <typename B, typename T, typename... Ts>
struct TargetsHelper<B, std::tuple<T, Ts...>> : TargetsHelper<B, std::tuple<Ts...>> {};

template <typename B, typename L>
using Targets = TargetsHelper<B, L>::type;

template <typename>
struct RequestsHelper;

template <>
struct RequestsHelper<std::tuple<>> {
  using requests = std::tuple<>;
  using nonrequests = std::tuple<>;
};

template <Returns T, typename... Ts>
struct RequestsHelper<std::tuple<T, Ts...>> {
  using tail = RequestsHelper<std::tuple<Ts...>>;
  using requests = Cons<T, typename tail::requests>;
  using nonrequests = tail::nonrequests;
};

template <typename T, typename... Ts>
struct RequestsHelper<std::tuple<T, Ts...>> {
  using tail = RequestsHelper<std::tuple<Ts...>>;
  using requests = tail::requests;
  using nonrequests = Cons<T, typename tail::nonrequests>;
};

template <typename... Ts>
using Requests = RequestsHelper<Ts...>::requests;

template <typename... Ts>
using NonRequests = RequestsHelper<Ts...>::nonrequests;

template <Returns T>
struct MockResult {
  td::Result<typename T::ReturnType> value;
  std::optional<td::Timestamp> time;
};

template <typename B, typename L, typename C>
class MockBus : public B {
 public:
  using Parent = B;
  using Events = td::TypeList<>;
  using Logs = L;
  using Calls = C;

 private:
  template <typename>
  struct MyEvents;
  template <typename... Ts>
  struct MyEvents<std::tuple<Ts...>> {
    using type = std::vector<std::variant<std::shared_ptr<const Ts>...>>;
  };

  template <typename>
  struct MyResults;
  template <typename... Ts>
  struct MyResults<std::tuple<Ts...>> {
    using type = std::tuple<std::deque<MockResult<Ts>>...>;
  };

 public:
  mutable MyEvents<L>::type events_;
  mutable MyResults<C>::type results_;

  template <In<L> E>
  void log(std::shared_ptr<const E> event) const {
    events_.emplace_back(std::move(event));
  }

  template <typename R>
    requires In<R, L> && In<R, C>
  td::actor::Task<typename R::ReturnType> call(std::shared_ptr<const R> request) const {
    log<R>(std::move(request));
    std::deque<MockResult<R>>& results = std::get<IndexOf<C, R>>(results_);
    if (results.empty()) {
      LOG(ERROR) << "Unexpected call, returning failure";
      td::TestContext::get()->register_test_failure();
      co_return td::Status::Error("Unexpected call");
    } else {
      MockResult<R>& result = results.front();
      if (result.time) {
        co_await td::actor::coro_sleep(*result.time);
      }
      td::Result<typename R::ReturnType> res = std::move(result.value);
      results.pop_front();
      co_return res;
    }
  }
};

template <typename B, typename H, typename P>
class MockActor : public td::actor::SpawnsWith<B>, public td::actor::ConnectsTo<B> {
 private:
  using BusHandle = td::actor::BusHandle<B>;

 public:
  template <typename BB, typename EE>
  void handle(td::actor::BusHandle<BB>, std::shared_ptr<const EE>) = delete;

  template <typename BB, typename EE>
  td::actor::Task<typename EE::ReturnType> process(td::actor::BusHandle<BB>, std::shared_ptr<const EE>) = delete;

  template <std::same_as<B> = B, In<H> E>
  void handle(BusHandle bh, std::shared_ptr<const E> event) {
    bh->template log<E>(std::move(event));
  }

  template <std::same_as<B> = B, In<P> R>
  td::actor::Task<typename R::ReturnType> process(BusHandle bh, std::shared_ptr<R> request) {
    return bh->template call<R>(std::move(request));
  }

 private:
  using A = MockActor<B, H, P>;
  template <typename>
  struct CanHelper;
  template <typename... Ts>
  struct CanHelper<std::tuple<Ts...>> {
    static constexpr bool handle()
      requires(td::actor::detail::CanActorHandleEvent<A, B, Ts> && ...)
    {
      return true;
    }
    static constexpr bool process()
      requires(td::actor::detail::CanActorProcessEvent<A, B, Ts> && ...)
    {
      return true;
    }
  };
  static_assert(CanHelper<H>::handle() && CanHelper<P>::process());
};

namespace c = ton::validator::consensus;
namespace s = c::simplex;

template <typename... Es>
struct TestBus : MockBus<s::Bus, std::tuple<Es...>, Requests<std::tuple<Es...>>> {
  std::map<ton::PublicKeyHash, ton::PrivateKey> keys;

  td::BufferSlice sign(const ton::PublicKeyHash& key_hash, td::Slice data) const {
    auto it = keys.find(key_hash);
    CHECK(it != keys.end());
    auto R = it->second.create_decryptor();
    R.ensure();
    auto R2 = R.ok()->sign(data);
    R2.ensure();
    return R2.move_as_ok();
  }
};

constexpr td::Bits256 bits256(unsigned char x) {
  unsigned char arr[32];
  std::fill(arr, arr + 32, x);
  return td::Bits256(arr);
}

template <typename A, typename B>
class SimplexTest : public td::Test {
 protected:
  using Bus = B;
  using BusHandle = td::actor::BusHandle<B>;
  using BusTargets = Targets<s::Bus, typename B::Logs>;
  using Actor = MockActor<Bus, NonRequests<BusTargets>, Requests<BusTargets>>;
  td::actor::TestScheduler ts_;
  BusHandle bh_;

  template <ValidPublishTargetFor<s::Bus> E>
  void send_event(E event) {
    bh_.broadcast(std::make_shared<E>(std::move(event)));
  }

  template <ValidRequestFor<s::Bus> R>
  td::actor::Task<typename R::ReturnType> send_request(R request) {
    return bh_.publish(std::make_shared<R>(std::move(request)));
  }

  template <In<typename B::Calls> R>
  void returns(R::ReturnType result) {
    std::get<IndexOf<typename B::Calls, R>>(bh_->results_).push_back({std::move(result), std::nullopt});
  }

  template <In<typename B::Logs>... E>
  void expect_events(const E... e) {
    auto& es = bh_->events_;
    EXPECT_EQ(es.size(), sizeof...(E));
    if (es.size() == sizeof...(E)) {
      auto it = es.begin();
      (
          [&]() {
            if (!(std::holds_alternative<std::shared_ptr<const E>>(*it) &&
                  *std::get<std::shared_ptr<const E>>(*it) == e)) {
              LOG(ERROR) << "Expectation failed: mismatched event ("
                         << std::visit([](const auto& v) { return v->contents_to_string(); }, *it)
                         << " != " << e.contents_to_string() << ")";
              td::TestContext::get()->register_test_failure();
            }
            ++it;
          }(),
          ...);
    }
    es.clear();
  }

  void run() override {
    ts_.run([this]() -> td::actor::Task<> {
      td::actor::Runtime runtime;
      A::register_in(runtime);
      runtime.register_actor<Actor>("Mock");
      auto bus = std::make_shared<Bus>();
      bus->session_id = bits256(239);
      for (std::size_t i = 0; i < 2; i++) {
        ton::PrivateKey priv(ton::privkeys::Ed25519::random());
        ton::PublicKey pub = priv.compute_public_key();
        ton::PublicKeyHash hash = pub.compute_short_id();
        bus->keys[hash] = std::move(priv);
        bus->validator_set.push_back(c::PeerValidator{c::PeerValidatorId(i), std::move(pub), hash, {}, 1});
      }
      bus->local_id = bus->validator_set[1];
      bus->total_weight = 2;
      bus->populate_collator_schedule();
      configure(*bus);
      bh_ = runtime.start(std::move(bus));
      co_await run_test();
      send_event<c::StopRequested>({});
      co_return {};
    });
  }

  virtual void configure(Bus& bus) {
  }

  virtual td::actor::Task<> run_test() = 0;
};

constexpr c::CandidateId make_candidate_id() {
  return {0, bits256(123)};
}

c::CandidateRef make_candidate(c::CandidateId id = make_candidate_id()) {
  return td::make_ref<c::Candidate>(make_candidate_id(), std::nullopt, c::PeerValidatorId(),
                                    ton::BlockCandidate({}, {}, {}, {}, {}), td::BufferSlice());
}

c::ChainStateRef make_state() {
  return td::make_ref<c::ChainState>(c::ChainState::ZerostateTip{{}, {}}, ton::BlockIdExt());
}

ton::tl_object_ptr<ton::ton_api::consensus_simplex_notarizeVote> serialize_vote(s::NotarizeVote vote) {
  return ton::create_tl_object<ton::ton_api::consensus_simplex_notarizeVote>(
      ton::create_tl_object<ton::ton_api::consensus_candidateId>(vote.id.slot, vote.id.hash));
}

ton::tl_object_ptr<ton::ton_api::consensus_simplex_finalizeVote> serialize_vote(s::FinalizeVote vote) {
  return ton::create_tl_object<ton::ton_api::consensus_simplex_finalizeVote>(
      ton::create_tl_object<ton::ton_api::consensus_candidateId>(vote.id.slot, vote.id.hash));
}

ton::tl_object_ptr<ton::ton_api::consensus_simplex_skipVote> serialize_vote(s::SkipVote vote) {
  return ton::create_tl_object<ton::ton_api::consensus_simplex_skipVote>(vote.slot);
}

template <td::OneOf<s::NotarizeVote, s::FinalizeVote, s::SkipVote> T>
td::BufferSlice vote_to_sign(const s::Bus& bus, T vote) {
  return ton::create_serialize_tl_object<ton::ton_api::consensus_dataToSign>(
      bus.session_id, ton::serialize_tl_object(serialize_vote(vote), true));
}

template <td::OneOf<s::NotarizeVote, s::FinalizeVote, s::SkipVote> T, typename... Es>
td::BufferSlice serialize_signed_vote(const TestBus<Es...>& bus, const ton::PublicKeyHash& key_hash, T vote) {
  return ton::create_serialize_tl_object<ton::ton_api::consensus_simplex_vote>(
      serialize_vote(vote), bus.sign(key_hash, vote_to_sign(bus, vote).as_slice()));
}

template <td::OneOf<s::NotarizeVote, s::FinalizeVote, s::SkipVote> T, typename... Es>
td::BufferSlice serialize_certificate(const TestBus<Es...>& bus, T vote) {
  td::BufferSlice to_sign = vote_to_sign(bus, vote);
  std::vector<ton::tl_object_ptr<ton::ton_api::consensus_simplex_voteSignature>> vote_signatures;
  for (auto& validator : bus.validator_set) {
    td::BufferSlice signature = bus.sign(validator.short_id, to_sign.as_slice());
    vote_signatures.push_back(ton::create_tl_object<ton::ton_api::consensus_simplex_voteSignature>(
        static_cast<td::int32>(validator.idx.value()), signature.clone()));
  }
  return ton::create_serialize_tl_object<ton::ton_api::consensus_simplex_certificate>(
      serialize_vote(vote),
      ton::create_tl_object<ton::ton_api::consensus_simplex_voteSignatureSet>(std::move(vote_signatures)));
}

// Candidate resolver tests

struct DbGet {
  td::BufferSlice key;

  bool operator==(const DbGet&) const = default;
  std::string contents_to_string() const {
    return PSTRING() << "{" << key << "}";
  }
};

struct DbGetByPrefix {
  td::uint32 prefix;

  bool operator==(const DbGetByPrefix&) const = default;
  std::string contents_to_string() const {
    return PSTRING() << "{" << td::BufferSlice(reinterpret_cast<const char*>(&prefix), 4) << "}";
  }
};

struct DbSet {
  td::BufferSlice key;
  td::BufferSlice value;

  bool operator==(const DbSet&) const = default;
  std::string contents_to_string() const {
    return PSTRING() << "{" << key << ", " << value << "}";
  }
};

using CandidateResolverBus = TestBus<c::OutgoingOverlayRequest, DbGet, DbGetByPrefix, DbSet>;

struct MockDb : c::Db {
  const CandidateResolverBus* bus_;
  std::map<td::BufferSlice, td::BufferSlice> data;

  MockDb(CandidateResolverBus* bus) : bus_(bus) {
  }

  std::optional<td::BufferSlice> get(td::Slice key) const override {
    td::BufferSlice k(key);
    bus_->log<DbGet>(std::make_shared<DbGet>(k.clone()));
    auto it = data.find(k);
    return it == data.end() ? std::nullopt : std::make_optional(it->second.clone());
  }

  std::vector<std::pair<td::BufferSlice, td::BufferSlice>> get_by_prefix(td::uint32 prefix) const override {
    bus_->log<DbGetByPrefix>(std::make_shared<DbGetByPrefix>(prefix));
    std::vector<std::pair<td::BufferSlice, td::BufferSlice>> res;
    for (auto& [key, value] : data) {
      if (key.size() >= 4 && *reinterpret_cast<const td::uint32*>(key.data()) == prefix) {
        res.emplace_back(key.clone(), value.clone());
      }
    }
    return res;
  }

  td::actor::Task<> set(td::BufferSlice key, td::BufferSlice value) override {
    bus_->log<DbSet>(std::make_shared<DbSet>(key.clone(), value.clone()));
    data[std::move(key)] = std::move(value);
    co_return {};
  }

  td::actor::Task<> close() override {
    UNREACHABLE();
    co_return {};
  }
};

struct MockCandidate {
  c::CandidateId id;
  c::CandidateRef candidate;
  s::NotarCertRef notar;
  td::BufferSlice serialized_candidate;
  c::ProtocolMessage request;
  c::ProtocolMessage response;

  td::BufferSlice db_key_candidate() const {
    return ton::create_serialize_tl_object<ton::ton_api::consensus_simplex_db_key_candidate>(
        ton::create_tl_object<ton::ton_api::consensus_candidateId>(id.slot, id.hash));
  }

  td::BufferSlice db_key_candidate_info() const {
    return ton::create_serialize_tl_object<ton::ton_api::consensus_simplex_db_key_candidateResolver_candidateInfo>(
        ton::create_tl_object<ton::ton_api::consensus_candidateId>(id.slot, id.hash));
  }
};

class CandidateResolverTest : public SimplexTest<s::CandidateResolver, CandidateResolverBus> {
 protected:
  void configure(Bus& bus) override {
    bus.db = std::make_unique<MockDb>(&bus);
  }

  static constexpr td::uint32 CANDIDATE_INFO_PREFIX =
      static_cast<td::uint32>(ton::ton_api::consensus_simplex_db_key_candidateResolver_candidateInfo::ID);

  td::actor::Task<> skip_startup() {
    co_await ts_.wait_sync_work();
    expect_events(DbGetByPrefix{CANDIDATE_INFO_PREFIX});
    co_return {};
  }

  MockCandidate make_candidate(const Bus& bus) const {
    auto R = vm::DataCell::create(td::Slice{}, 0, td::Span<td::Ref<vm::Cell>>{}, false);
    R.ensure();
    auto R2 = vm::std_boc_serialize(R.move_as_ok(), 31);
    R2.ensure();
    td::BufferSlice block_data = R2.move_as_ok();
    td::BufferSlice collated_data;
    td::Bits256 block_data_hash;
    td::sha256(block_data.as_slice(), block_data_hash.as_slice());
    td::Bits256 root_hash = bits256(0x1f);
    td::Bits256 collated_data_hash;
    td::sha256(collated_data.as_slice(), collated_data_hash.as_slice());
    td::Bits256 candidate_hash = ton::create_hash_tl_object<ton::ton_api::consensus_candidateHashDataOrdinary>(
        ton::create_tl_object<ton::ton_api::tonNode_blockIdExt>(ton::workchainInvalid, 0, 0, root_hash,
                                                                block_data_hash),
        collated_data_hash, ton::create_tl_object<ton::ton_api::consensus_candidateWithoutParents>());
    c::CandidateId candidate_id{0, candidate_hash};
    auto R3 = ton::validatorsession::serialize_candidate(
        ton::create_tl_object<ton::ton_api::validatorSession_candidate>(bits256(0), 0, root_hash, block_data.clone(),
                                                                        collated_data.clone()),
        true);
    R3.ensure();
    td::BufferSlice candidate_signature = bus.sign(
        bus.validator_set[0].short_id,
        ton::create_serialize_tl_object<ton::ton_api::consensus_dataToSign>(
            bus.session_id, ton::create_serialize_tl_object<ton::ton_api::consensus_candidateId>(0, candidate_hash))
            .as_slice());
    td::BufferSlice serialized_candidate = ton::create_serialize_tl_object<ton::ton_api::consensus_block>(
        0, ton::create_tl_object<ton::ton_api::consensus_candidateWithoutParents>(), R3.move_as_ok(),
        candidate_signature.clone());
    td::BufferSlice to_sign = vote_to_sign(bus, s::NotarizeVote{candidate_id});
    std::vector<s::NotarCert::VoteSignature> vote_signatures;
    std::vector<ton::tl_object_ptr<ton::ton_api::consensus_simplex_voteSignature>> vote_signatures_tl;
    for (auto& validator : bus.validator_set) {
      td::BufferSlice signature = bus.sign(validator.short_id, to_sign.as_slice());
      vote_signatures.push_back({validator.idx, signature.clone()});
      vote_signatures_tl.push_back(ton::create_tl_object<ton::ton_api::consensus_simplex_voteSignature>(
          static_cast<td::int32>(validator.idx.value()), signature.clone()));
    }
    return {.id = candidate_id,
            .candidate = td::make_ref<c::Candidate>(
                candidate_id, std::nullopt, c::PeerValidatorId(0),
                ton::BlockCandidate{ton::Ed25519_PublicKey(bus.validator_set[0].key.ed25519_value().raw()),
                                    {{ton::workchainInvalid, 0, 0}, root_hash, block_data_hash},
                                    collated_data_hash,
                                    block_data.clone(),
                                    collated_data.clone()},
                candidate_signature.clone()),
            .notar = td::make_ref<s::NotarCert>(s::NotarizeVote{candidate_id}, std::move(vote_signatures)),
            .serialized_candidate = serialized_candidate.clone(),
            .request = {ton::create_serialize_tl_object<ton::ton_api::consensus_simplex_requestCandidate>(
                ton::create_tl_object<ton::ton_api::consensus_candidateId>(0, candidate_hash), true, true)},
            .response = {ton::create_serialize_tl_object<ton::ton_api::consensus_simplex_candidateAndCert>(
                serialized_candidate.clone(),
                ton::create_serialize_tl_object<ton::ton_api::consensus_simplex_voteSignatureSet>(
                    std::move(vote_signatures_tl)))}};
  }
};

class OutgoingResolve : public CandidateResolverTest {
  td::actor::Task<> run_test() override {
    co_await skip_startup();
    MockCandidate candidate = make_candidate(*bh_);
    returns<c::OutgoingOverlayRequest>(std::move(candidate.response));
    s::ResolveCandidate::Result res = co_await send_request<s::ResolveCandidate>({candidate.id});
    expect_events(c::OutgoingOverlayRequest{c::PeerValidatorId(0), td::Timestamp::in(1), std::move(candidate.request)});
    EXPECT_EQ(*res.candidate, *candidate.candidate);
    EXPECT_EQ(*res.notar, *candidate.notar);
    co_return {};
  }
};

REGISTER_TEST(CandidateResolver, OutgoingResolve);

class IncomingResolve : public CandidateResolverTest {
  td::actor::Task<> run_test() override {
    co_await skip_startup();
    MockCandidate candidate = make_candidate(*bh_);
    co_await send_request<s::StoreCandidate>({candidate.candidate});
    expect_events(DbSet{candidate.db_key_candidate(), std::move(candidate.serialized_candidate)},
                  DbSet{candidate.db_key_candidate_info(), td::BufferSlice()});
    send_event<s::NotarizationObserved>({candidate.id, candidate.notar});
    c::ProtocolMessage res =
        co_await send_request<c::IncomingOverlayRequest>({c::PeerValidatorId(0), std::move(candidate.request)});
    EXPECT_EQ(res, candidate.response);
    expect_events();
    co_return {};
  }
};

REGISTER_TEST(CandidateResolver, IncomingResolve);

class InitialCandidates : public CandidateResolverTest {
  MockCandidate candidate;

  void configure(Bus& bus) override {
    CandidateResolverTest::configure(bus);
    candidate = make_candidate(bus);
    auto& data = static_cast<MockDb&>(*bus.db).data;
    data.emplace(candidate.db_key_candidate(), std::move(candidate.serialized_candidate));
    data.emplace(candidate.db_key_candidate_info(), td::BufferSlice());
    s::NotarCertRef notar = candidate.notar;
    bus.bootstrap_certificates.push_back(std::move(notar.write()).consume_and_upcast());
  }

  td::actor::Task<> run_test() override {
    co_await ts_.wait_sync_work();
    expect_events(DbGetByPrefix{CANDIDATE_INFO_PREFIX});
    c::ProtocolMessage res =
        co_await send_request<c::IncomingOverlayRequest>({c::PeerValidatorId(0), std::move(candidate.request)});
    EXPECT_EQ(res, candidate.response);
    expect_events(DbGet{candidate.db_key_candidate()});
    co_return {};
  }
};

REGISTER_TEST(CandidateResolver, InitialCandidates);

// Consensus tests

using ConsensusBus = TestBus<s::BroadcastVote, c::TraceEvent, s::ResolveState, c::OurLeaderWindowStarted,
                             s::StoreCandidate, s::WaitForParent, c::MisbehaviorReport, c::ValidationRequest>;

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
  static constexpr c::CandidateId id0a{0, bits256(42)};
  static constexpr c::CandidateId id0b{0, bits256(128)};
  static constexpr c::CandidateId id1a{1, bits256(84)};
  static constexpr c::CandidateId id1b{1, bits256(48)};
  static constexpr c::CandidateId id2a{2, bits256(123)};

  void configure(Bus& bus) override {
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
    // Slot 0: not notarizing
    auto candidate = make_candidate(id0a);
    send_event(c::CandidateReceived{candidate});
    co_await ts_.wait_sync_work();
    expect_events();
    const_cast<c::Candidate&>(*candidate).id = id0b;
    send_event(c::CandidateReceived{candidate});
    co_await ts_.wait_sync_work();
    expect_events();
    // Slot 1: notarizing
    const_cast<c::Candidate&>(*candidate).id = id1b;
    send_event(c::CandidateReceived{candidate});
    auto state = make_state();
    returns<s::WaitForParent>(std::nullopt);
    returns<s::StoreCandidate>(td::Unit{});
    returns<s::ResolveState>({state, std::nullopt});
    returns<c::ValidationRequest>(ton::validator::CandidateAccept{0});
    returns<s::BroadcastVote>(td::Unit{});
    co_await ts_.wait_sync_work();
    expect_events(c::TraceEvent{c::stats::CandidateReceived::create(candidate, false)}, s::WaitForParent{candidate},
                  s::StoreCandidate{candidate}, s::ResolveState{std::nullopt}, c::ValidationRequest{state, candidate},
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
    const_cast<c::Candidate&>(*candidate).id = id2a;
    send_event(c::CandidateReceived{candidate});
    returns<s::WaitForParent>(std::nullopt);
    returns<s::StoreCandidate>(td::Unit{});
    returns<s::ResolveState>({state, std::nullopt});
    returns<c::ValidationRequest>(ton::validator::CandidateAccept{0});
    returns<s::BroadcastVote>(td::Unit{});
    co_await ts_.wait_sync_work();
    expect_events(c::TraceEvent{c::stats::CandidateReceived::create(candidate, false)}, s::WaitForParent{candidate},
                  s::StoreCandidate{candidate}, s::ResolveState{std::nullopt}, c::ValidationRequest{state, candidate},
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
  td::actor::Task<> run_test() override {
    auto candidate = make_candidate();
    auto state = make_state();
    send_event(s::LeaderWindowObserved{0, candidate->parent_id});
    send_event(c::CandidateReceived{candidate});
    returns<s::WaitForParent>(std::nullopt);
    returns<s::StoreCandidate>(td::Unit{});
    returns<s::ResolveState>({state, std::nullopt});
    returns<c::ValidationRequest>(ton::validator::CandidateAccept{0});
    returns<s::BroadcastVote>(td::Unit{});
    co_await ts_.wait_sync_work();
    expect_events(c::TraceEvent{c::stats::CandidateReceived::create(candidate, false)}, s::WaitForParent{candidate},
                  s::StoreCandidate{candidate}, s::ResolveState{std::nullopt}, c::ValidationRequest{state, candidate},
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
  td::actor::Task<> run_test() override {
    auto candidate = make_candidate();
    auto state = make_state();
    send_event(s::LeaderWindowObserved{0, candidate->parent_id});
    send_event(s::NotarizationObserved{candidate->id, {}});
    co_await ts_.wait_sync_work();
    expect_events();
    send_event(c::CandidateReceived{candidate});
    returns<s::WaitForParent>(std::nullopt);
    returns<s::StoreCandidate>(td::Unit{});
    returns<s::ResolveState>({state, std::nullopt});
    returns<c::ValidationRequest>(ton::validator::CandidateAccept{0});
    returns<s::BroadcastVote>(td::Unit{});
    returns<s::BroadcastVote>(td::Unit{});
    co_await ts_.wait_sync_work();
    expect_events(c::TraceEvent{c::stats::CandidateReceived::create(candidate, false)}, s::WaitForParent{candidate},
                  s::StoreCandidate{candidate}, s::ResolveState{std::nullopt}, c::ValidationRequest{state, candidate},
                  s::BroadcastVote{s::NotarizeVote{candidate->id}}, s::BroadcastVote{s::FinalizeVote{candidate->id}});
    co_return {};
  }
};

REGISTER_TEST(Consensus, FinalizationOutOfOrder);

class ValidationRejected : public ConsensusTest {
  td::actor::Task<> run_test() override {
    auto candidate = make_candidate();
    auto state = make_state();
    send_event(s::LeaderWindowObserved{0, candidate->parent_id});
    send_event(c::CandidateReceived{candidate});
    returns<s::WaitForParent>(std::nullopt);
    returns<s::StoreCandidate>(td::Unit{});
    returns<s::ResolveState>({state, std::nullopt});
    returns<c::ValidationRequest>(ton::validator::CandidateReject{{}, {}});
    co_await ts_.wait_sync_work();
    expect_events(c::TraceEvent{c::stats::CandidateReceived::create(candidate, false)}, s::WaitForParent{candidate},
                  s::StoreCandidate{candidate}, s::ResolveState{std::nullopt}, c::ValidationRequest{state, candidate});
    co_return {};
  }
};

REGISTER_TEST(Consensus, ValidationRejected);

class SkipTimeout : public ConsensusTest {
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
  void configure(Bus& bus) override {
    bus.local_id.idx = c::PeerValidatorId(0);
  }

  td::actor::Task<> run_test() override {
    send_event(s::LeaderWindowObserved{0, std::nullopt});
    auto state = make_state();
    returns<s::ResolveState>({state, std::nullopt});
    co_await ts_.wait_sync_work();
    expect_events(s::ResolveState{std::nullopt},
                  c::OurLeaderWindowStarted{std::nullopt, state, 0, 4, td::Timestamp::now()});
    co_return {};
  }
};

REGISTER_TEST(Consensus, GenerationStarted);

class MisbehaviorReported : public ConsensusTest {
  td::actor::Task<> run_test() override {
    auto candidate = make_candidate();
    auto misbehavior = td::make_ref<c::Misbehavior>();
    send_event(s::LeaderWindowObserved{0, candidate->parent_id});
    send_event(c::CandidateReceived{candidate});
    returns<s::WaitForParent>(misbehavior);
    returns<s::StoreCandidate>(td::Unit{});
    co_await ts_.wait_sync_work();
    expect_events(c::TraceEvent{c::stats::CandidateReceived::create(candidate, false)}, s::WaitForParent{candidate},
                  s::StoreCandidate{candidate}, c::MisbehaviorReport{candidate->leader, misbehavior});
    co_return {};
  }
};

REGISTER_TEST(Consensus, MisbehaviorReported);

// Pool tests

struct SignMessage {
  ton::PublicKeyHash key_hash;
  td::BufferSlice data;

  bool operator==(const SignMessage&) const = default;
  std::string contents_to_string() const {
    return PSTRING() << "{" << key_hash << ", " << data << "}";
  }
};

using PoolBus = TestBus<c::TraceEvent, c::OutgoingProtocolMessage, c::MisbehaviorReport, s::LeaderWindowObserved,
                        s::SaveCertificate, s::NotarizationObserved, s::FinalizationObserved, SignMessage>;

struct MockKeyring : ton::keyring::Keyring {
  const PoolBus* bus_;
  MockKeyring(const PoolBus* bus) : bus_(bus) {
  }

  void add_key(ton::PrivateKey key, bool temp, td::Promise<td::Unit> promise) override {
    UNREACHABLE();
  }

  void check_key(ton::PublicKeyHash key_hash, td::Promise<td::Unit> promise) override {
    UNREACHABLE();
  }

  void add_key_short(ton::PublicKeyHash key_hash, td::Promise<ton::PublicKey> promise) override {
    UNREACHABLE();
  }

  void del_key(ton::PublicKeyHash key_hash, td::Promise<td::Unit> promise) override {
    UNREACHABLE();
  }

  void export_private_key(ton::PublicKeyHash key_hash, td::Promise<ton::PrivateKey> promise) override {
    UNREACHABLE();
  }

  void get_public_key(ton::PublicKeyHash key_hash, td::Promise<ton::PublicKey> promise) override {
    UNREACHABLE();
  }

  void sign_message(ton::PublicKeyHash key_hash, td::BufferSlice data, td::Promise<td::BufferSlice> promise) override {
    bus_->log<SignMessage>(std::make_shared<SignMessage>(key_hash, data.clone()));
    promise.set_value(bus_->sign(key_hash, data.as_slice()));
  }

  void sign_add_get_public_key(ton::PublicKeyHash key_hash, td::BufferSlice data,
                               td::Promise<std::pair<td::BufferSlice, ton::PublicKey>> promise) override {
    UNREACHABLE();
  }

  void sign_messages(ton::PublicKeyHash key_hash, std::vector<td::BufferSlice> data,
                     td::Promise<std::vector<td::Result<td::BufferSlice>>> promise) override {
    UNREACHABLE();
  }

  void decrypt_message(ton::PublicKeyHash key_hash, td::BufferSlice data,
                       td::Promise<td::BufferSlice> promise) override {
    UNREACHABLE();
  }

  void export_all_private_keys(td::Promise<std::vector<ton::PrivateKey>> promise) override {
    UNREACHABLE();
  }
};

class PoolTest : public SimplexTest<s::Pool, PoolBus> {
 protected:
  void configure(Bus& bus) override {
    bus.keyring = td::actor::create_actor<MockKeyring>("MockKeyring", &bus).release();
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
    send_event(c::Start{{}});
    returns<s::LeaderWindowObserved>(td::Unit{});
    co_await ts_.wait_sync_work();
    expect_events(c::TraceEvent{c::stats::Id::create(bh_->shard, bh_->cc_seqno, bh_->local_id.idx.value(),
                                                     bh_->validator_set.size(), bh_->local_id.weight, bh_->total_weight,
                                                     bh_->config.slots_per_leader_window)},
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
    expect_events(s::NotarizationObserved{vote2.id, td::make_ref<s::NotarCert>(
                                                        vote2, std::vector<s::NotarCert::VoteSignature>())},
                  s::FinalizationObserved{
                      vote0.id, td::make_ref<s::FinalCert>(vote0, std::vector<s::FinalCert::VoteSignature>())},
                  c::TraceEvent{s::stats::Voted::create(vote5)}, c::TraceEvent{s::stats::Voted::create(vote4)},
                  c::TraceEvent{s::stats::Voted::create(vote3)},
                  SignMessage{bh_->validator_set[1].short_id, vote_to_sign(*bh_, vote5)},
                  SignMessage{bh_->validator_set[1].short_id, vote_to_sign(*bh_, vote4)},
                  SignMessage{bh_->validator_set[1].short_id, vote_to_sign(*bh_, vote3)});
    co_return {};
  }
};

REGISTER_TEST(Pool, InitialState);

class OurVote : public PoolTest {
  td::actor::Task<> run_test() override {
    s::Vote vote = s::NotarizeVote{make_candidate_id()};
    send_event(s::BroadcastVote{vote});
    co_await ts_.wait_sync_work();
    auto vote_tl = [&] {
      return ton::create_tl_object<ton::ton_api::consensus_simplex_notarizeVote>(
          ton::create_tl_object<ton::ton_api::consensus_candidateId>(0, bits256(123)));
    };
    td::BufferSlice to_sign = ton::create_serialize_tl_object<ton::ton_api::consensus_dataToSign>(
        bh_->session_id, ton::serialize_tl_object(vote_tl(), true));
    expect_events(c::TraceEvent{s::stats::Voted::create(vote)}, SignMessage{bh_->local_id.short_id, to_sign.clone()},
                  c::OutgoingProtocolMessage{std::nullopt,
                                             {ton::create_serialize_tl_object<ton::ton_api::consensus_simplex_vote>(
                                                 vote_tl(), bh_->sign(bh_->local_id.short_id, to_sign.as_slice()))}});
    co_return {};
  }
};

REGISTER_TEST(Pool, OurVote);
