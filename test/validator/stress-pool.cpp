/*
 * Copyright (c) 2025-2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "keyring/keyring.hpp"
#include "td/utils/Random.h"
#include "td/utils/tests.h"
#include "validator/consensus/runtime.h"
#include "validator/consensus/simplex/bus.h"

namespace ton::validator::consensus::simplex {
namespace {

// Stress test for simplex::Pool
// This test:
// 1. Creates random WaitForParent requests in a separate coroutine
// 2. Creates random certificates via BroadcastVote (with 1 validator, votes become certificates)
// 3. Verifies pool invariants on each observed event

struct StressBus : Bus {
  using Parent = Bus;
  using Events = td::TypeList<>;

  StressBus() {
    // Stop the scheduler when the bus is destroyed
    destructor_ = td::create_shared_destructor([] { td::actor::SchedulerContext::get().stop(); });
  }

  td::actor::ActorOwn<keyring::Keyring> keyring_own;
  std::shared_ptr<td::Destructor> destructor_;
};

// Test actor that generates random events and checks invariants
class StressTest : public runtime::SpawnsWith<StressBus>, public runtime::ConnectsTo<StressBus> {
 public:
  TON_RUNTIME_DEFINE_EVENT_HANDLER();

  void start_up() override {
    run().start().detach();
  }

  template <>
  void handle(runtime::BusHandle<StressBus> bus, std::shared_ptr<const NotarizationObserved> event) {
    auto& info = slot_states_[event->id.slot];

    // Invariant: slot must not be finalized when we notarize
    LOG_CHECK(event->id.slot >= first_finalized_slot_) << "Notarization issued for finalized slot " << event->id.slot;

    // Invariant: at most one block is notarized per slot
    if (info.notarized) {
      LOG_CHECK(info.notarized_id == event->id) << "Slot " << event->id.slot << " notarized with two different IDs!";
    }
    info.notarized = true;
    info.notarized_id = event->id;
  }

  template <>
  void handle(runtime::BusHandle<StressBus> bus, std::shared_ptr<const FinalizationObserved> event) {
    auto& info = slot_states_[event->id.slot];

    // Invariant: finalization slots must be increasing
    LOG_CHECK(event->id.slot >= first_finalized_slot_)
        << "Finalization slot " << event->id.slot << " is not increasing! Previous was " << (first_finalized_slot_ - 1);

    // Invariant: slot cannot be both skipped and finalized
    LOG_CHECK(!info.skipped) << "Slot " << event->id.slot << " both skipped and finalized!";

    // Invariant: cannot finalize twice
    LOG_CHECK(!info.finalized) << "Slot " << event->id.slot << " finalized twice!";

    info.finalized = true;
    // Note: We might not have observed NotarizationObserved, but finalization implies notarization
    info.notarized = true;
    info.notarized_id = event->id;
    first_finalized_slot_ = event->id.slot + 1;
  }

  template <>
  void handle(runtime::BusHandle<StressBus> bus, std::shared_ptr<const LeaderWindowObserved> event) {
    td::uint32 s = event->start_slot;

    // Invariant: s must not be finalized
    LOG_CHECK(s >= first_finalized_slot_) << "LeaderWindow starting at finalized slot " << s;

    if (event->base.has_value()) {
      td::uint32 t = event->base->slot;

      // Invariant: base slot must be notarized (or finalized, which implies notarization)
      // Pool might have observed finalization before notarization
      LOG_CHECK(slot_states_[t].notarized || slot_states_[t].finalized)
          << "LeaderWindow base slot " << t << " is not notarized or finalized";

      // Invariant: all slots (t; s) must be skipped
      for (td::uint32 slot = t + 1; slot < s; ++slot) {
        LOG_CHECK(slot_states_[slot].skipped)
            << "Slot " << slot << " between base " << t << " and window start " << s << " is not skipped";
      }
    }
  }

 private:
  // Main stress test loop - generates votes
  td::actor::Task<> run() {
    const int num_iterations = 100;

    for (int i = 0; i < num_iterations; ++i) {
      // Randomly choose between different actions
      int action = td::Random::fast(0, 3);

      switch (action) {
        case 0: {
          // Broadcast a random NotarizeVote
          send_random_notarize_vote();
          break;
        }
        case 1: {
          // Broadcast a random SkipVote
          send_random_skip_vote();
          break;
        }
        case 2: {
          // Broadcast a random FinalizeVote
          send_random_finalize_vote();
          break;
        }
        case 3: {
          send_random_wait_for_parent_request().start().detach();
          break;
        }
      }

      // Small delay to let the pool process events
      co_await td::actor::coro_sleep(td::Timestamp::in(0.001));
    }

    stopping_ = true;
    owning_bus().publish<StopRequested>();
    stop();
    co_return td::Unit{};
  }

  td::actor::Task<> send_random_wait_for_parent_request() {
    auto& bus = *owning_bus();

    // Generate random slot and candidate
    td::uint32 slot = td::Random::fast(1, 50);
    td::uint32 parent_slot = td::Random::fast(0, slot);

    // Create random hash for candidate
    Bits256 hash;
    td::Random::secure_bytes(hash.data(), 32);

    Bits256 parent_hash;
    td::Random::secure_bytes(parent_hash.data(), 32);

    RawCandidateId raw_id{slot, hash};
    RawParentId parent = parent_slot > 0 ? std::optional<RawCandidateId>{{parent_slot, parent_hash}} : std::nullopt;

    // Create CandidateId (not RawCandidateId) for the constructor
    CandidateId id{raw_id, BlockIdExt{}};

    // Create a dummy candidate
    auto candidate = td::make_ref<RawCandidate>(id, parent, bus.local_id.idx, BlockIdExt{}, td::BufferSlice());

    // Send WaitForParent request
    auto result = co_await owning_bus().publish<WaitForParent>(candidate).wrap();

    if (result.is_ok()) {
      auto misbehavior = result.move_as_ok();
      if (misbehavior.has_value()) {
        pending_misbehaviors_++;

        // Check invariant: misbehavior should be returned when there's a conflict with finalization
        // This happens when the slot or parent is finalized with a different block
      } else {
        resolved_requests_++;

        // Check WaitForParent resolution invariant:
        // Request resolves successfully when:
        // 1. parent_slot is notarized (or we're building on genesis)
        // 2. All slots between parent_slot+1 and slot are skipped
        if (parent.has_value()) {
          td::uint32 parent_s = parent->slot;
          // Parent should be notarized
          LOG_CHECK(slot_states_[parent_s].notarized || slot_states_[parent_s].finalized)
              << "WaitForParent resolved but parent slot " << parent_s << " is not notarized";

          // All intermediate slots should be skipped
          for (td::uint32 s = parent_s + 1; s < slot; ++s) {
            LOG_CHECK(slot_states_[s].skipped)
                << "WaitForParent resolved but intermediate slot " << s << " is not skipped";
          }
        }
      }
    } else if (!stopping_) {
      LOG(FATAL) << "WaitForParent request failed: " << result.error();
    }

    co_return td::Unit{};
  }

  void send_random_notarize_vote() {
    // Generate random slot and candidate
    td::uint32 slot = td::Random::fast(0, 30);

    // Create random hash for candidate
    Bits256 hash;
    td::Random::secure_bytes(hash.data(), 32);

    RawCandidateId id{slot, hash};
    NotarizeVote vote{id};

    owning_bus().publish<BroadcastVote>(Vote{vote});
    notarize_votes_sent_++;
  }

  void send_random_skip_vote() {
    // Generate random slot
    td::uint32 slot = td::Random::fast(0, 30);

    SkipVote vote{slot};

    owning_bus().publish<BroadcastVote>(Vote{vote});
    skip_votes_sent_++;

    // With single validator, vote immediately becomes a certificate
    slot_states_[slot].skipped = true;
  }

  void send_random_finalize_vote() {
    // Generate random slot and candidate
    td::uint32 slot = td::Random::fast(0, 30);

    // Create random hash for candidate
    Bits256 hash;
    td::Random::secure_bytes(hash.data(), 32);

    RawCandidateId id{slot, hash};
    NotarizeVote vote{id};

    owning_bus().publish<BroadcastVote>(Vote{vote});
    notarize_votes_sent_++;
  }

  bool stopping_ = false;

  struct SlotInfo {
    bool notarized = false;
    bool skipped = false;
    bool finalized = false;
    RawCandidateId notarized_id{0, td::Bits256::zero()};
  };

  std::map<td::uint32, SlotInfo> slot_states_;
  td::uint32 first_finalized_slot_ = 0;

  // Statistics
  int resolved_requests_ = 0;
  int pending_misbehaviors_ = 0;
  int notarize_votes_sent_ = 0;
  int skip_votes_sent_ = 0;
};

// Helper to create a mock database
class MockDb : public Db {
 public:
  std::optional<td::BufferSlice> get(td::Slice key) const override {
    auto it = storage_.find(key.str());
    if (it != storage_.end()) {
      return td::BufferSlice(it->second);
    }
    return std::nullopt;
  }

  std::vector<std::pair<td::BufferSlice, td::BufferSlice>> get_by_prefix(td::uint32 prefix) const override {
    return {};
  }

  td::actor::Task<> set(td::BufferSlice key, td::BufferSlice value) override {
    storage_[key.as_slice().str()] = value.as_slice().str();
    co_return td::Unit{};
  }

 private:
  mutable std::map<std::string, std::string> storage_;
};

TEST(SimplexPool, Stress) {
  td::actor::Scheduler scheduler({1});

  scheduler.run_in_context([&] {
    runtime::Runtime runtime;
    runtime.register_actor<StressTest>("StressTest");
    Pool::register_in(runtime);

    // Create a bus with minimal configuration
    auto bus = std::make_shared<StressBus>();

    // Set up simplex config
    bus->simplex_config.slots_per_leader_window = 10;

    // Create a single validator (ourselves)
    PrivateKey pk{privkeys::Ed25519::random()};
    PublicKey key = pk.compute_public_key();
    PublicKeyHash short_id = key.compute_short_id();

    bus->validator_set.push_back(PeerValidator{
        .idx = PeerValidatorId{0},
        .key = key,
        .short_id = short_id,
        .adnl_id = adnl::AdnlNodeIdShort{short_id.bits256_value()},
        .weight = 1,
    });

    bus->local_id = bus->validator_set[0];
    bus->total_weight = 1;

    // Set up session ID
    bus->session_id = ValidatorSessionId{td::Bits256::zero()};

    // Create an in-memory keyring
    bus->keyring_own = keyring::Keyring::create("");
    bus->keyring = bus->keyring_own.get();

    // Generate a key and add it to the keyring
    td::actor::send_closure(bus->keyring_own, &keyring::Keyring::add_key, std::move(pk), true, [](td::Unit) {});

    // Create mock database
    bus->db = std::make_unique<MockDb>();

    // Start the runtime
    auto bus_handle = runtime.start(bus, "StressPool");

    // Publish Start event to begin the test
    bus_handle.publish<Start>(std::vector<BlockIdExt>{}, BlockIdExt{});
  });

  scheduler.run();
}

}  // namespace
}  // namespace ton::validator::consensus::simplex
