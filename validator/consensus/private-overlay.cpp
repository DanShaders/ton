/*
 * Copyright (c) 2025-2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <vector>

#include "adnl/adnl-node-id.hpp"
#include "auto/tl/ton_api.h"
#include "overlay/overlays.h"
#include "td/utils/Random.h"
#include "td/utils/Status.h"
#include "td/utils/logging.h"
#include "ton/ton-io.hpp"

#include "bus.h"

namespace ton::validator::consensus {

namespace tl {

using requestError = ton_api::consensus_requestError;
using RequestErrorRef = tl_object_ptr<requestError>;

}  // namespace tl

namespace {

class PrivateOverlayImpl : public td::actor::SpawnsWith<Bus>, public td::actor::ConnectsTo<Bus> {
 public:
  TON_RUNTIME_DEFINE_EVENT_HANDLER();

  static bool should_be_spawned(const Bus& bus) {
    return bus.is_validator() || bus.config.observers_in_private_overlay();
  }

  void start_up() override {
    auto& bus = *owning_bus();

    overlays_ = bus.overlays;
    local_adnl_id_ = bus.local_adnl_id;
    adnl_sender_ = bus.adnl_sender;

    std::vector<td::Bits256> overlay_nodes_tl;
    std::map<PublicKeyHash, td::uint32> authorized_keys;

    td::uint32 max_broadcast_size = bus.config.max_block_size + bus.config.max_collated_data_size + (1 << 20);
    for (const auto& peer : bus.validator_set) {
      adnl_id_to_peer_[peer.adnl_id] = peer;
      short_id_to_peer_[peer.short_id] = peer;
      overlay_nodes_.push_back(peer.adnl_id);
      overlay_nodes_tl.push_back(peer.short_id.bits256_value());
      authorized_keys.emplace(peer.short_id, max_broadcast_size);
    }

    td::actor::send_closure(adnl_sender_, &adnl::AdnlSenderEx::add_id, local_adnl_id_);

    auto overlay_seed = create_tl_object<tl::overlayId>(bus.session_id, std::move(overlay_nodes_tl));
    auto overlay_full_id = overlay::OverlayIdFull{serialize_tl_object(overlay_seed, true)};
    overlay_id_ = overlay_full_id.compute_short_id();

    overlay::OverlayOptions options;
    options.name_ = PSTRING() << "valgroup" << bus.shard << "." << bus.cc_seqno;
    options.private_ping_peers_ = true;
    options.twostep_broadcast_sender_ = adnl_sender_;
    options.send_twostep_broadcast_ = true;
    options.allow_old_broadcasts_ = false;

    if (bus.config.observers_in_private_overlay()) {
      overlay_nodes_ = bus.all_validators;
    }

    if (bus.config.collators_in_overlay()) {
      for (const auto& [validator_short_id, collators] : bus.collators_by_validator) {
        for (const auto& collator : collators) {
          collator_to_validators_[collator].insert(validator_short_id);
          collator_adnl_ids_.insert(collator);
        }
      }
      for (const auto& collator : collator_adnl_ids_) {
        if (std::find(overlay_nodes_.begin(), overlay_nodes_.end(), collator) == overlay_nodes_.end()) {
          overlay_nodes_.push_back(collator);
        }
        // For a single-key ed25519 collator the adnl short id equals its public key hash, which is what the
        // overlay privacy rules key broadcasts on.
        authorized_keys.emplace(PublicKeyHash{collator.bits256_value()}, max_broadcast_size);
      }
    }

    td::actor::send_closure(overlays_, &overlay::Overlays::create_private_overlay_ex, local_adnl_id_,
                            std::move(overlay_full_id), overlay_nodes_, make_callback(),
                            overlay::OverlayPrivacyRules{0, 0, std::move(authorized_keys)},
                            PSTRING() << R"({ "type": "consensus", "shard": ")" << bus.shard << R"(", "cc_seqno": )"
                                      << bus.cc_seqno << R"( })",
                            std::move(options));

    for (auto node : overlay_nodes_) {
      if (node != local_adnl_id_) {
        other_overlay_nodes_.push_back(node);
      }
    }
  }

  template <>
  void handle(BusHandle, std::shared_ptr<const StopRequested>) {
    td::actor::send_closure(overlays_, &overlay::Overlays::delete_overlay, local_adnl_id_, overlay_id_);
    stop();
  }

  template <>
  void handle(BusHandle, std::shared_ptr<const OutgoingProtocolMessage> message) {
    auto send_to_peer = [&](const adnl::AdnlNodeIdShort& adnl_id) {
      CHECK(adnl_id != local_adnl_id_);
      td::actor::send_closure(overlays_, &overlay::Overlays::send_message_via, adnl_id, local_adnl_id_, overlay_id_,
                              message->message.data.clone(), adnl_sender_);
    };

    auto broadcast_fn = [&](const OutgoingProtocolMessage::BroadcastToAll&) {
      for (const auto& adnl_id : other_overlay_nodes_) {
        send_to_peer(adnl_id);
      }
    };

    auto gossip_fn = [&](const OutgoingProtocolMessage::BroadcastToRandom& r) {
      std::vector<adnl::AdnlNodeIdShort> selected_peers;
      std::sample(other_overlay_nodes_.begin(), other_overlay_nodes_.end(), std::back_inserter(selected_peers),
                  std::min(r.count, other_overlay_nodes_.size()), gossip_rng_);

      for (auto peer : selected_peers) {
        send_to_peer(peer);
      }
    };

    auto direct_fn = [&](const OutgoingProtocolMessage::SendToPeer& s) {
      if (s.peer != local_adnl_id_) {
        send_to_peer(s.peer);
      }
    };

    std::visit(td::overloaded(broadcast_fn, gossip_fn, direct_fn), message->recipient);
  }

  template <>
  td::actor::Task<ProtocolMessage> process(BusHandle, std::shared_ptr<OutgoingOverlayRequest> message) {
    auto destination = message->destination;
    if (!destination) {
      CHECK(!other_overlay_nodes_.empty());
      size_t node_idx = td::Random::fast(0, static_cast<int>(other_overlay_nodes_.size()) - 1);
      destination = other_overlay_nodes_[node_idx];
    }

    auto [awaiter, promise] = td::actor::StartedTask<td::BufferSlice>::make_bridge();
    // FIXME: Pass max response size from the caller.
    td::actor::send_closure(
        overlays_, &overlay::Overlays::send_query_via, *destination, local_adnl_id_, overlay_id_, "",
        std::move(promise), message->timeout, std::move(message->request.data),
        owning_bus()->config.max_block_size + owning_bus()->config.max_collated_data_size + (1 << 20), adnl_sender_);
    auto response = co_await std::move(awaiter);
    if (fetch_tl_object<tl::requestError>(response, true).is_ok()) {
      co_return td::Status::Error("Peer returned an error");
    }
    co_return ProtocolMessage{std::move(response)};
  }

  template <>
  void handle(BusHandle, std::shared_ptr<const CandidateGenerated> event) {
    auto& bus = *owning_bus();
    if (bus.config.enable_block_sync()) {
      return;
    }

    tl_object_ptr<ton_api::consensus_CollatorDelegation> delegation;
    PublicKeyHash send_as;
    if (event->collator_pubkey.has_value()) {
      // We produced this candidate as a remote collator on the leader's behalf: broadcast under our own overlay
      // identity and attach the leader's window-delegation proof.
      CHECK(bus.is_dedicated_collator);
      delegation = create_tl_object<ton_api::consensus_collatorDelegation_present>(event->collator_pubkey.value(),
                                                                                   event->delegation_signature.clone());
      send_as = PublicKeyHash{bus.local_adnl_id.bits256_value()};
    } else {
      CHECK(bus.is_validator());
      delegation = create_tl_object<ton_api::consensus_collatorDelegation_none>();
      send_as = bus.local_id->short_id;
    }
    td::BufferSlice extra =
        create_serialize_tl_object<ton_api::consensus_broadcastExtra>(event->candidate->id.slot, std::move(delegation));
    td::actor::send_closure(overlays_, &overlay::Overlays::send_broadcast_fec_with_extra, local_adnl_id_, overlay_id_,
                            send_as, 0, event->candidate->serialize(), std::move(extra));
  }

 private:
  std::unique_ptr<overlay::Overlays::Callback> make_callback() {
    class Callback final : public overlay::Overlays::Callback {
     public:
      explicit Callback(td::actor::ActorId<PrivateOverlayImpl> owner) : owner_(owner) {
      }

      void receive_message(adnl::AdnlNodeIdShort src, overlay::OverlayIdShort, td::BufferSlice data) override {
        td::actor::send_closure(owner_, &PrivateOverlayImpl::on_overlay_message, src, std::move(data));
      }

      void receive_query(adnl::AdnlNodeIdShort src, overlay::OverlayIdShort, td::BufferSlice data,
                         td::Promise<td::BufferSlice> promise) override {
        td::actor::send_closure(owner_, &PrivateOverlayImpl::on_query, src, std::move(data), std::move(promise));
      }

      void receive_broadcast_with_extra(PublicKeyHash src, overlay::OverlayIdShort, td::BufferSlice data,
                                        td::BufferSlice extra) override {
        td::actor::send_closure(owner_, &PrivateOverlayImpl::on_overlay_broadcast, src, std::move(data),
                                std::move(extra));
      }

      void precheck_broadcast(PublicKeyHash src, overlay::OverlayIdShort overlay_id, td::Bits256 broadcast_id,
                              td::BufferSlice extra, bool signature_checked, td::Promise<> promise) override {
        td::actor::send_closure(owner_, &PrivateOverlayImpl::precheck_broadcast, src, broadcast_id, std::move(extra),
                                signature_checked, std::move(promise));
      }

     private:
      td::actor::ActorId<PrivateOverlayImpl> owner_;
    };

    return std::make_unique<Callback>(actor_id(this));
  }

  void on_overlay_message(adnl::AdnlNodeIdShort src_adnl_id, td::BufferSlice data) {
    auto peer = adnl_id_to_peer_.find(src_adnl_id);
    std::optional<PeerValidatorId> source_validator;
    if (peer != adnl_id_to_peer_.end()) {
      source_validator = peer->second.idx;
    }

    owning_bus().publish<IncomingProtocolMessage>(source_validator, src_adnl_id, std::move(data));
  }

  void on_overlay_broadcast(PublicKeyHash src, td::BufferSlice data, td::BufferSlice extra) {
    auto& bus = *owning_bus();

    if (bus.config.enable_block_sync()) {
      LOG(WARNING) << "Dropping candidate broadcast from " << src << " in private overlay: protocol violation";
      return;
    }
    if (bus.is_validator() && src == bus.local_id->short_id) {
      return;
    }

    auto parsed_extra = fetch_tl_object<ton_api::consensus_broadcastExtra>(extra, true).move_as_ok();
    auto slot = static_cast<td::uint32>(parsed_extra->slot_);

    td::Result<CandidateRef> maybe_candidate;
    auto peer_it = short_id_to_peer_.find(src);
    if (peer_it != short_id_to_peer_.end()) {
      maybe_candidate = Candidate::deserialize(std::move(data), bus, peer_it->second.idx, slot);
    } else {
      // Collator/validator split: a non-validator collator may broadcast on a leader's behalf.
      auto r_collator = verify_collator_delegation(src, slot, *parsed_extra->delegation_);
      if (r_collator.is_error()) {
        LOG(WARNING) << "MISBEHAVIOR: Rejecting collator candidate broadcast from " << src << ": "
                     << r_collator.move_as_error();
        return;
      }
      auto leader = bus.collator_schedule->expected_collator_for(slot);
      maybe_candidate = Candidate::deserialize(std::move(data), bus, leader, slot, r_collator.move_as_ok());
    }

    if (maybe_candidate.is_error()) {
      // FIXME: If we actually collected signed broadcast parts, we could have produced a
      //        MisbehaviorProof here.
      LOG(WARNING) << "MISBEHAVIOR: Failed to deserialize block candidate broadcast: "
                   << maybe_candidate.move_as_error();
      return;
    }
    auto candidate = maybe_candidate.move_as_ok();

    if (!candidate->is_empty()) {
      const BlockCandidate& block = std::get<BlockCandidate>(candidate->block);
      td::actor::send_closure(bus.manager, &ManagerFacade::cache_block_candidate, block.clone());
    }

    owning_bus().publish<CandidateReceived>(std::move(candidate));
  }

  td::actor::Task<> precheck_broadcast(PublicKeyHash src, td::Bits256 broadcast_id, td::BufferSlice extra,
                                       bool signature_checked) {
    if (owning_bus()->config.enable_block_sync()) {
      co_return td::Status::Error("Precheck failed: Candidate broadcasts in private overlay are disabled");
    }
    auto parsed_extra = fetch_tl_object<ton_api::consensus_broadcastExtra>(extra, true);
    if (parsed_extra.is_error()) {
      co_return parsed_extra.move_as_error_prefix("Precheck failed: Failed to parse broadcast extra: ");
    }

    auto& bus = *owning_bus();
    auto extra_obj = parsed_extra.move_as_ok();
    auto slot = static_cast<td::uint32>(extra_obj->slot_);
    auto peer_it = short_id_to_peer_.find(src);
    if (peer_it != short_id_to_peer_.end()) {
      if (peer_it->second.idx != bus.collator_schedule->expected_collator_for(slot)) {
        co_return td::Status::Error("Precheck failed: Broadcast is not from the expected collator");
      }
    } else {
      // Collator/validator split: accept a broadcast from a collator authorized for this window.
      auto r_collator = verify_collator_delegation(src, slot, *extra_obj->delegation_);
      if (r_collator.is_error()) {
        co_return r_collator.move_as_error_prefix("Precheck failed: ");
      }
    }

    co_return co_await owning_bus()
        .publish<PrecheckCandidateBroadcast>(slot, broadcast_id, signature_checked)
        .trace("Precheck failed");
  }

  // Collator/validator split: validate that `src` is a collator authorized (in the on-chain registry) by the
  // leader that owns window `slot`, and that `delegation` carries that leader's signature over the window.
  // Returns the collator's full ed25519 public key on success (used to verify the candidate co-signature).
  td::Result<PublicKey> verify_collator_delegation(PublicKeyHash src, td::uint32 slot,
                                                   const ton_api::consensus_CollatorDelegation& delegation) {
    auto& bus = *owning_bus();
    adnl::AdnlNodeIdShort src_adnl{src.bits256_value()};
    auto it = collator_to_validators_.find(src_adnl);
    if (it == collator_to_validators_.end()) {
      return td::Status::Error("broadcast source is neither a validator nor a known collator");
    }
    const auto* present = dynamic_cast<const ton_api::consensus_collatorDelegation_present*>(&delegation);
    if (present == nullptr) {
      return td::Status::Error("collator broadcast is missing a delegation");
    }
    PeerValidator leader = bus.collator_schedule->expected_collator_for(slot).get_using(bus);
    if (!it->second.contains(leader.short_id)) {
      return td::Status::Error("collator is not authorized by the window's leader");
    }
    PublicKey collator_key{pubkeys::Ed25519{present->collator_pubkey_}};
    if (collator_key.compute_short_id() != src) {
      return td::Status::Error("collator delegation key does not match the broadcast sender");
    }
    auto window_signed = create_serialize_tl_object<ton_api::consensus_collatorWindow>(static_cast<int>(slot));
    if (!leader.check_signature(bus.session_id, window_signed.as_slice(), present->signature_.as_slice())) {
      return td::Status::Error("collator window delegation signature is invalid");
    }
    return collator_key;
  }

  void on_query(adnl::AdnlNodeIdShort src, td::BufferSlice data, td::Promise<td::BufferSlice> promise) {
    auto peer = adnl_id_to_peer_.find(src);
    auto peer_idx = peer != adnl_id_to_peer_.end() ? std::optional{peer->second.idx} : std::nullopt;

    auto request = std::make_shared<IncomingOverlayRequest>(peer_idx, src, std::move(data));

    auto task = [](BusHandle bus, auto message, auto promise) -> td::actor::Task<> {
      auto response = co_await bus.publish(message).wrap();
      if (response.is_ok()) {
        promise.set_value(response.move_as_ok().data);
      } else {
        LOG(WARNING) << "Failed to process overlay request from " << message->source << ": "
                     << response.move_as_error();
        promise.set_value(create_serialize_tl_object<tl::requestError>());
      }
      co_return {};
    };
    task(owning_bus(), request, std::move(promise)).start().detach();
  }

  td::actor::ActorId<overlay::Overlays> overlays_;
  td::actor::ActorId<adnl::AdnlSenderEx> adnl_sender_;
  overlay::OverlayIdShort overlay_id_;
  adnl::AdnlNodeIdShort local_adnl_id_;
  std::vector<adnl::AdnlNodeIdShort> overlay_nodes_;
  std::vector<adnl::AdnlNodeIdShort> other_overlay_nodes_;
  std::map<adnl::AdnlNodeIdShort, PeerValidator> adnl_id_to_peer_;
  std::map<PublicKeyHash, PeerValidator> short_id_to_peer_;

  // Collator/validator split: collators added to the overlay (not part of validator_set), and the set of
  // validators (by public key hash) that authorized each collator in the on-chain registry.
  std::set<adnl::AdnlNodeIdShort> collator_adnl_ids_;
  std::map<adnl::AdnlNodeIdShort, std::set<PublicKeyHash>> collator_to_validators_;

  std::mt19937 gossip_rng_ = td::Random::fast_gen();
};

}  // namespace

void PrivateOverlay::register_in(td::actor::Runtime& runtime) {
  runtime.register_actor<PrivateOverlayImpl>("PrivateOverlay");
}

}  // namespace ton::validator::consensus
