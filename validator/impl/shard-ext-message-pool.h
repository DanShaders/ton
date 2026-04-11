/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include "interfaces/external-message.h"
#include "td/utils/Badge.h"

namespace ton::validator {

class WorkchainExternalsPool;

struct PrioritizedExternal {
  td::Ref<ExtMessage> message;
  int priority;

  ExtMessage::Hash hash() const {
    return message->hash_norm();
  }
};

class ShardExternalsPoolReader;

class ShardExternalsPool : public td::actor::Actor {
 public:
  explicit ShardExternalsPool(ShardIdFull shard) : shard_(shard) {
  }

  void add_external(td::Badge<WorkchainExternalsPool>, PrioritizedExternal external);
  void activate(td::Badge<ShardExternalsPoolReader>, ExtMessageQueue queue);
  void deactivate(td::Badge<ShardExternalsPoolReader>, std::vector<ExtMessage::Hash> applied_messages);
  void remove_messages(td::Badge<ShardExternalsPoolReader>, std::vector<ExtMessage::Hash> hashes);
  void add_messages(td::Badge<ShardExternalsPoolReader>, std::vector<PrioritizedExternal> entries);

  void drain_to_children(td::Badge<WorkchainExternalsPool>, td::actor::ActorId<ShardExternalsPool> left,
                         td::actor::ActorId<ShardExternalsPool> right);
  void drain_to_parent(td::Badge<WorkchainExternalsPool>, td::actor::ActorId<ShardExternalsPool> parent);

 private:
  static constexpr std::chrono::seconds ttl{600};

  struct Index {
    int priority;
    size_t i;

    std::strong_ordering operator<=>(const Index&) const = default;
  };

  struct Entry {
    ExtMessage::Hash hash;
    td::Ref<ExtMessage> message;
    td::Timestamp ts;
  };

  struct TimestampAndIndex {
    td::Timestamp ts;
    Index index;

    std::strong_ordering operator<=>(const TimestampAndIndex&) const = default;
  };

  struct ControlBlock {
    ExtMessageQueue queue;
    td::Promise<> promise;
    bool is_closed = false;
  };

  Index index_for(int priority);
  void add(PrioritizedExternal external, ExtMessage::Hash hash);
  void add_messages_impl(std::vector<PrioritizedExternal> entries);
  void remove(Index index);
  void reschedule_cleanup();
  void alarm() override;

  ShardIdFull shard_;

  std::map<ExtMessage::Hash, Index> by_hash_;
  std::set<TimestampAndIndex> expiry_order_;
  std::map<int, std::vector<Entry>> entries_;

  std::map<ExtMessage::Hash, PrioritizedExternal> pushed_to_queue_;

  std::shared_ptr<ControlBlock> queue_;
};

class ShardExternalsPoolReader {
 public:
  ShardExternalsPoolReader() = default;
  ShardExternalsPoolReader(const ShardExternalsPoolReader&) = delete;
  ShardExternalsPoolReader(ShardExternalsPoolReader&&) = default;
  ShardExternalsPoolReader& operator=(const ShardExternalsPoolReader&) = delete;
  ShardExternalsPoolReader& operator=(ShardExternalsPoolReader&&) = default;

  explicit operator bool() const {
    return !pool_.empty();
  }

  ShardIdFull shard() const {
    return shard_;
  }

  ExtMessageQueue activate();
  void deactivate(std::vector<ExtMessage::Hash> applied_messages);
  void remove_messages(std::vector<ExtMessage::Hash> hashes);
  void add_messages(std::vector<PrioritizedExternal> entries);

 private:
  using Badge = td::Badge<ShardExternalsPoolReader>;

  friend class WorkchainExternalsPool;

  explicit ShardExternalsPoolReader(td::actor::ActorId<ShardExternalsPool> pool, ShardIdFull shard)
      : pool_(pool), shard_(shard) {
  }

  td::actor::ActorId<ShardExternalsPool> pool_;
  ShardIdFull shard_;
};

}  // namespace ton::validator
