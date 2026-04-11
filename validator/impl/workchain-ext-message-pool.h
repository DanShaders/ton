/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include "shard-ext-message-pool.h"

namespace ton::validator {

struct ShardTree {
  ShardIdFull shard;
  td::actor::ActorOwn<ShardExternalsPool> pool;
  std::unique_ptr<ShardTree> children[2] = {};
};

class WorkchainExternalsPool {
 public:
  explicit WorkchainExternalsPool(WorkchainId workchain);

  void add_external(td::Ref<ExtMessage> message, int priority);

  ShardExternalsPoolReader take_token(ShardIdFull shard);
  void store_token(ShardExternalsPoolReader token);

  struct SplitResult {
    ShardExternalsPoolReader left;
    ShardExternalsPoolReader right;
  };
  SplitResult split(ShardExternalsPoolReader parent);
  ShardExternalsPoolReader merge(ShardExternalsPoolReader left, ShardExternalsPoolReader right);

 private:
  WorkchainId workchain_;
  std::unique_ptr<ShardTree> tree_;
  std::map<ShardIdFull, ShardExternalsPoolReader> stored_tokens_;

  struct FindLeafResult {
    ShardTree* node;
    ShardTree* parent;
  };
  FindLeafResult find_leaf(td::uint64 prefix);
};

}  // namespace ton::validator
