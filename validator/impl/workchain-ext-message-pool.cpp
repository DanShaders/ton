/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "ton/ton-shard.h"

#include "workchain-ext-message-pool.h"

namespace ton::validator {

WorkchainExternalsPool::WorkchainExternalsPool(WorkchainId workchain) : workchain_(workchain) {
  auto shard = ShardIdFull{workchain};
  auto actor = td::actor::create_actor<ShardExternalsPool>("ExternalsPool" + shard.to_str(), shard);
  tree_ = std::make_unique<ShardTree>();
  tree_->shard = shard;
  tree_->pool = std::move(actor);
  stored_tokens_.emplace(shard, ShardExternalsPoolReader{tree_->pool.get(), shard});
}

void WorkchainExternalsPool::add_external(td::Ref<ExtMessage> message, int priority) {
  auto account_prefix = message->shard();
  CHECK(account_prefix.workchain == workchain_);
  auto node = find_leaf(account_prefix.account_id_prefix).node;
  CHECK(node && !node->pool.empty());
  td::actor::send_closure(node->pool, &ShardExternalsPool::add_external, td::Badge<WorkchainExternalsPool>{},
                          PrioritizedExternal{std::move(message), priority});
}

bool WorkchainExternalsPool::has_token(ShardIdFull shard) const {
  return stored_tokens_.contains(shard);
}

ShardExternalsPoolReader WorkchainExternalsPool::take_token(ShardIdFull shard) {
  CHECK(shard.workchain == workchain_);

  auto it = stored_tokens_.find(shard);
  CHECK(it != stored_tokens_.end());
  auto token = std::move(it->second);
  stored_tokens_.erase(it);
  return token;
}

void WorkchainExternalsPool::store_token(ShardExternalsPoolReader token) {
  CHECK(token.shard().workchain == workchain_);

  auto shard = token.shard();
  CHECK(!stored_tokens_.contains(shard));
  stored_tokens_.emplace(shard, std::move(token));
}

WorkchainExternalsPool::SplitResult WorkchainExternalsPool::split(ShardExternalsPoolReader parent) {
  CHECK(parent.shard().workchain == workchain_);
  CHECK(!stored_tokens_.contains(parent.shard()));

  auto node = find_leaf(parent.shard().shard).node;
  CHECK(node && !node->pool.empty());
  CHECK(node->children[0] == nullptr && node->children[1] == nullptr);

  auto left_shard = node->shard.left();
  auto right_shard = node->shard.right();

  node->children[0] = std::make_unique<ShardTree>();
  node->children[0]->shard = left_shard;
  node->children[0]->pool =
      td::actor::create_actor<ShardExternalsPool>("ExternalsPool" + left_shard.to_str(), left_shard);

  node->children[1] = std::make_unique<ShardTree>();
  node->children[1]->shard = right_shard;
  node->children[1]->pool =
      td::actor::create_actor<ShardExternalsPool>("ExternalsPool" + right_shard.to_str(), right_shard);

  td::actor::send_closure(node->pool, &ShardExternalsPool::drain_to_children, td::Badge<WorkchainExternalsPool>{},
                          node->children[0]->pool.get(), node->children[1]->pool.get());
  node->pool = {};

  return {
      .left{node->children[0]->pool.get(), left_shard},
      .right{node->children[1]->pool.get(), right_shard},
  };
}

ShardExternalsPoolReader WorkchainExternalsPool::merge(ShardExternalsPoolReader left, ShardExternalsPoolReader right) {
  CHECK(left.shard().workchain == workchain_);
  CHECK(right.shard().workchain == workchain_);
  CHECK(shard_is_sibling(left.shard().shard, right.shard().shard));

  ShardTree* parent = find_leaf(left.shard().shard).parent;
  CHECK(parent);
  CHECK(parent->children[0] && parent->children[1]);
  CHECK(parent->pool.empty());

  auto parent_shard = parent->shard;
  parent->pool = td::actor::create_actor<ShardExternalsPool>("ExternalsPool" + parent_shard.to_str(), parent_shard);

  td::actor::send_closure(parent->children[0]->pool, &ShardExternalsPool::drain_to_parent,
                          td::Badge<WorkchainExternalsPool>{}, parent->pool.get());
  td::actor::send_closure(parent->children[1]->pool, &ShardExternalsPool::drain_to_parent,
                          td::Badge<WorkchainExternalsPool>{}, parent->pool.get());
  parent->children[0] = {};
  parent->children[1] = {};

  return ShardExternalsPoolReader{parent->pool.get(), parent_shard};
}

auto WorkchainExternalsPool::find_leaf(td::uint64 prefix) -> FindLeafResult {
  ShardTree* node = tree_.get();
  ShardTree* parent = nullptr;
  for (int i = 64; i--;) {
    if (!node->pool.empty()) {
      return {node, parent};
    }
    auto bit = prefix >> i & 1;
    CHECK(node->children[bit]);
    parent = node;
    node = node->children[bit].get();
  }
  UNREACHABLE();
}

}  // namespace ton::validator
