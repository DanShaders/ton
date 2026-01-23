#include "auto/tl/ton_api.hpp"
#include "td/actor/coro_utils.h"
#include "td/utils/port/path.h"

#include "PublicOverlayClient.h"

namespace tonlib {

class OverlayCallback final : public ton::overlay::Overlays::Callback {
 public:
  void receive_message(ton::adnl::AdnlNodeIdShort src, ton::overlay::OverlayIdShort overlay_id,
                       td::BufferSlice data) override {
    LOG(INFO) << "Received overlay message from " << src << " on overlay " << overlay_id << ", size: " << data.size()
              << " bytes";
  }

  void receive_query(ton::adnl::AdnlNodeIdShort src, ton::overlay::OverlayIdShort overlay_id, td::BufferSlice data,
                     td::Promise<td::BufferSlice> promise) override {
    promise.set_value(td::BufferSlice());
    // LOG(INFO) << "Received overlay query from " << src << " on overlay " << overlay_id << ", size: " << data.size()
    //           << " bytes";
    // promise.set_error(td::Status::Error("Queries are not supported"));
  }

  void receive_broadcast(ton::PublicKeyHash src, ton::overlay::OverlayIdShort overlay_id,
                         td::BufferSlice data) override {
    auto hash = sha256(data);
    LOG(INFO) << "Received overlay broadcast from " << src << " on overlay " << overlay_id << ": "
              << td::format::as_hex_dump<0>(td::Slice(hash));
  }

  void check_broadcast(ton::PublicKeyHash src, ton::overlay::OverlayIdShort overlay_id, td::BufferSlice data,
                       td::Promise<td::Unit> promise) override {
    promise.set_value(td::Unit());
  }
};

PublicOverlayClient::PublicOverlayClient(PublicOverlayClientConfig config) : config_(std::move(config)) {
}

td::actor::Task<td::Unit> PublicOverlayClient::init() {
  co_await td::mkdir(config_.db_root);

  keyring_ = ton::keyring::Keyring::create(config_.db_root);
  co_await td::actor::ask(keyring_, &ton::keyring::Keyring::add_key, config_.client_private_key, false);

  network_manager_ = ton::adnl::AdnlNetworkManager::create(static_cast<td::uint16>(config_.bind_address.get_port()));

  ton::adnl::AdnlCategoryMask mask;
  mask[0] = true;
  td::actor::send_closure(network_manager_, &ton::adnl::AdnlNetworkManager::add_self_addr, config_.bind_address, mask,
                          0);

  adnl_ = ton::adnl::Adnl::create(config_.db_root, keyring_.get());
  td::actor::send_closure(adnl_, &ton::adnl::Adnl::register_network_manager, network_manager_.get());

  local_id_ = ton::adnl::AdnlNodeIdShort{config_.client_private_key.compute_short_id()};

  auto address = ton::create_tl_object<ton::ton_api::adnl_address_udp>(config_.bind_address.get_ipv4(),
                                                                       config_.bind_address.get_port());
  auto upcasted_address = std::unique_ptr<ton::ton_api::adnl_Address>(address.release());
  std::vector<ton::tl_object_ptr<ton::ton_api::adnl_Address>> addr_vec;
  addr_vec.push_back(std::move(upcasted_address));
  auto address_list = ton::create_tl_object<ton::ton_api::adnl_addressList>(std::move(addr_vec), 0, 0, 0, 0);

  co_await td::actor::ask(adnl_, &ton::adnl::Adnl::add_id,
                          ton::adnl::AdnlNodeIdFull{config_.client_private_key.compute_public_key()},
                          ton::adnl::AdnlAddressList::create(address_list).move_as_ok(), static_cast<td::uint8>(0));

  dht_ = co_await ton::dht::Dht::create_client(local_id_, config_.db_root, config_.dht_config, keyring_.get(),
                                               adnl_.get());

  overlays_ = ton::overlay::OverlayManager::create(config_.db_root, keyring_.get(), adnl_.get(), dht_.get());

  auto overlay_id_hash = ton::create_hash_tl_object<ton::ton_api::tonNode_shardPublicOverlayId>(
      config_.zerostate.workchain, config_.shard_id.shard, config_.zerostate.file_hash);
  td::BufferSlice overlay_id_buffer{32};
  overlay_id_buffer.as_slice().copy_from(as_slice(overlay_id_hash));
  overlay_id_ = ton::overlay::OverlayIdFull{std::move(overlay_id_buffer)};

  ton::overlay::OverlayOptions options{};
  ton::overlay::OverlayPrivacyRules privacy_rules{
      ton::overlay::Overlays::max_fec_broadcast_size(),
      ton::overlay::CertificateFlags::AllowFec,
      {},
  };

  co_await td::actor::ask(overlays_, &ton::overlay::Overlays::create_public_overlay_ex, local_id_, overlay_id_.clone(),
                          std::make_unique<OverlayCallback>(), privacy_rules, "{}", options);

  LOG(INFO) << "Public overlay client connecting to overlay " << overlay_id_.compute_short_id() << " for shard "
            << config_.shard_id.to_str();

  while (true) {
    auto peers = co_await td::actor::ask(overlays_, &ton::overlay::Overlays::get_overlay_random_peers, local_id_,
                                         overlay_id_.compute_short_id(), 30);
    if (!peers.empty()) {
      LOG(INFO) << "Connected to " << peers.size() << " peers in overlay";
      break;
    }
    co_await td::actor::coro_sleep(td::Timestamp::in(1.0));
  }

  co_return {};
}

}  // namespace tonlib
