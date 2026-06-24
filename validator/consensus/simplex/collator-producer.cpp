/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <algorithm>

#include "consensus/utils.h"
#include "keyring/keyring.h"
#include "td/actor/coro_task.h"
#include "td/actor/coro_utils.h"
#include "td/utils/CancellationToken.h"

#include "bus.h"

namespace ton::validator::consensus::simplex {

namespace {

// Runs on a node that serves as a collator for some validator(s) in this group (bus.is_dedicated_collator is set).
// On receiving a consensus.pleaseCollate from the window's leader, it collates the block on the leader's behalf,
// co-signs it with the collator's own key, and broadcasts it carrying the leader's delegation proof. Consumers
// (private-overlay.cpp) then treat the broadcast as the leader's own candidate.
class CollatorProducerImpl : public td::actor::SpawnsWith<Bus>, public td::actor::ConnectsTo<Bus> {
 public:
  TON_RUNTIME_DEFINE_EVENT_HANDLER();

  static bool should_be_spawned(const Bus& bus) {
    return bus.is_dedicated_collator;
  }

  template <>
  void handle(BusHandle, std::shared_ptr<const IncomingProtocolMessage> message) {
    auto r_please = fetch_tl_object<ton_api::consensus_pleaseCollate>(message->message.data, true);
    if (r_please.is_error()) {
      return;  // not a pleaseCollate; other actors handle their own messages
    }
    auto please = r_please.move_as_ok();
    auto& bus = *owning_bus();

    if (!bus.is_dedicated_collator) {
      return;
    }
    if (!message->source_validator.has_value()) {
      LOG(WARNING) << "Dropping pleaseCollate from non-validator " << message->source;
      return;
    }

    auto start_slot = static_cast<td::uint32>(please->start_slot_);
    auto end_slot = static_cast<td::uint32>(please->end_slot_);
    PeerValidator leader = bus.collator_schedule->expected_collator_for(start_slot).get_using(bus);
    if (leader.idx != *message->source_validator) {
      LOG(WARNING) << "Dropping pleaseCollate from " << message->source << ": not the leader of slot " << start_slot;
      return;
    }

    auto it = bus.collators_by_validator.find(leader.short_id);
    if (it == bus.collators_by_validator.end() ||
        std::find(it->second.begin(), it->second.end(), bus.local_adnl_id) == it->second.end()) {
      LOG(WARNING) << "Dropping pleaseCollate: we are not a registered collator of " << leader.short_id;
      return;
    }

    auto window = create_serialize_tl_object<ton_api::consensus_collatorWindow>(static_cast<td::int32>(start_slot));
    if (!leader.check_signature(bus.session_id, window.as_slice(), please->signature_.as_slice())) {
      LOG(WARNING) << "Dropping pleaseCollate from " << leader.short_id << ": invalid window signature";
      return;
    }

    auto base = CandidateId::tl_to_parent_id(please->base_);
    collate_for_leader(base, start_slot, end_slot, leader, std::move(please->signature_)).start().detach();
  }

 private:
  td::actor::Task<> collate_for_leader(ParentId base, td::uint32 start_slot, td::uint32 end_slot, PeerValidator leader,
                                       td::BufferSlice delegation_signature) {
    auto& bus = *owning_bus();

    auto resolved = co_await owning_bus().publish<ResolveState>(base);
    ChainStateRef state = resolved.state;

    td::Timestamp start_time = td::Timestamp::now();
    if (resolved.gen_utime_exact.has_value()) {
      start_time = std::max(start_time, td::Timestamp::at_unix(*resolved.gen_utime_exact));
    }

    std::chrono::milliseconds hard_timeout =
        std::max(bus.config.noncritical_params.target_rate * 3, std::chrono::milliseconds(60'000));
    CollateParams params{
        .shard = bus.shard,
        .min_masterchain_block_id = state->min_mc_block_id(),
        .prev = state->block_ids(),
        .creator = Ed25519_PublicKey{leader.key.ed25519_value().raw()},
        .utime = start_time.at_unix(),
        .hard_timeout = start_time + hard_timeout,
        .prev_block_data = state->block_data(),
        .prev_block_state_roots = state->state(),
    };
    if (bus.shard.is_masterchain()) {
      params.soft_timeout = start_time + bus.config.noncritical_params.target_rate;
    } else {
      params.soft_timeout = start_time;
      params.wait_externals_until = start_time;
    }

    td::CancellationTokenSource cancellation_source;
    auto r_generated = co_await td::actor::ask(bus.manager, &ManagerFacade::collate_block, std::move(params),
                                               cancellation_source.get_cancellation_token())
                           .wrap();
    if (r_generated.is_error()) {
      LOG(WARNING) << "Collator failed to collate window " << start_slot << " for " << leader.short_id << ": "
                   << r_generated.move_as_error();
      co_return {};
    }
    auto generated = r_generated.move_as_ok();

    td::actor::send_closure(bus.manager, &ManagerFacade::cache_block_candidate, generated.candidate.clone());

    auto id = CandidateHashData::create_full(generated.candidate, base).build_id_with(start_slot);
    auto id_to_sign = serialize_tl_object(id.to_tl(), true);
    auto data_to_sign = create_serialize_tl_object<ton_api::consensus_dataToSign>(bus.session_id, std::move(id_to_sign));

    PublicKeyHash collator_key_hash{bus.local_adnl_id.bits256_value()};
    auto signed_result = co_await td::actor::ask(bus.keyring, &keyring::Keyring::sign_add_get_public_key,
                                                 collator_key_hash, std::move(data_to_sign));
    auto signature = std::move(signed_result.first);
    PublicKey collator_pubkey = std::move(signed_result.second);

    std::variant<BlockIdExt, BlockCandidate> block = std::move(generated.candidate);
    auto candidate = td::make_ref<Candidate>(id, base, leader.idx, std::move(block), std::move(signature));

    LOG(INFO) << "Collator produced block for delegated window " << start_slot << " on behalf of leader "
              << leader.short_id;
    // Register our own candidate locally so this node's CandidateResolver/StateResolver can resolve it as the
    // parent of the next delegated window (otherwise the next ResolveState would hang — we never receive our
    // own broadcast back). The notar cert is then observed via the normal NotarizationObserved path.
    owning_bus().publish<StoreCandidate>(candidate).start().detach();
    owning_bus().publish<CandidateGenerated>(candidate, std::optional<adnl::AdnlNodeIdShort>{},
                                             std::optional<td::Bits256>{collator_pubkey.ed25519_value().raw()},
                                             std::move(delegation_signature));
    co_return {};
  }
};

}  // namespace

void CollatorProducer::register_in(td::actor::Runtime& runtime) {
  runtime.register_actor<CollatorProducerImpl>("CollatorProducer");
}

}  // namespace ton::validator::consensus::simplex
