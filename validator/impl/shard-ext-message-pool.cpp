/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "common/errorcode.h"
#include "td/utils/Random.h"

#include "shard-ext-message-pool.h"

namespace ton::validator {

void ShardExternalsPool::add_external(td::Badge<WorkchainExternalsPool>, PrioritizedExternal external) {
  auto hash = external.hash();
  add(std::move(external), hash);
  reschedule_cleanup();
  if (queue_ && queue_->promise) {
    queue_->promise.set_value({});
  }
}

void ShardExternalsPool::activate(td::Badge<ShardExternalsPoolReader>, ExtMessageQueue queue) {
  CHECK(!queue_);
  queue_ = std::make_shared<ControlBlock>(queue);

  auto task = [](ShardExternalsPool& self, std::shared_ptr<ControlBlock> queue) -> td::actor::Task<> {
    while (!queue->is_closed) {
      if (self.entries_.empty()) {
        auto [awaiter, promise] = td::actor::StartedTask<>::make_bridge();
        queue->promise = std::move(promise);
        co_await std::move(awaiter);
        continue;
      }

      auto& [priority, entries] = *self.entries_.rbegin();
      size_t i = td::Random::fast_uint64() % entries.size();
      auto& entry = entries[i];
      auto message = entry.message;
      self.pushed_to_queue_[entry.hash] = {message, priority};
      self.remove({priority, i});
      co_await queue->queue.push({message, priority});
    }
    co_return {};
  }(*this, queue_);
  std::move(task).start().detach();
}

void ShardExternalsPool::deactivate(td::Badge<ShardExternalsPoolReader>,
                                    std::vector<ExtMessage::Hash> applied_messages) {
  CHECK(queue_);
  queue_->is_closed = true;
  queue_->queue.close();
  if (queue_->promise) {
    queue_->promise.set_error(td::Status::Error(cancelled));
  }
  queue_ = {};

  for (auto hash : applied_messages) {
    pushed_to_queue_.erase(hash);
  }
  for (auto& [hash, entry] : pushed_to_queue_) {
    add(std::move(entry), hash);
  }
}

void ShardExternalsPool::remove_messages(td::Badge<ShardExternalsPoolReader>, std::vector<ExtMessage::Hash> hashes) {
  for (auto hash : hashes) {
    if (auto it = by_hash_.find(hash); it != by_hash_.end()) {
      remove(it->second);
    }
  }
  reschedule_cleanup();
}

void ShardExternalsPool::add_messages(td::Badge<ShardExternalsPoolReader>, std::vector<PrioritizedExternal> entries) {
  add_messages_impl(entries);
}

void ShardExternalsPool::drain_to_children(td::Badge<WorkchainExternalsPool>,
                                           td::actor::ActorId<ShardExternalsPool> left,
                                           td::actor::ActorId<ShardExternalsPool> right) {
  std::vector<PrioritizedExternal> left_batch, right_batch;
  auto left_shard = shard_.left();
  for (auto& [priority, entries] : entries_) {
    for (auto& entry : entries) {
      auto& batch = entry.message->shard().inside(left_shard) ? left_batch : right_batch;
      batch.push_back({std::move(entry.message), priority});
    }
  }
  entries_.clear();
  by_hash_.clear();
  expiry_order_.clear();
  td::actor::send_closure(left, &ShardExternalsPool::add_messages_impl, std::move(left_batch));
  td::actor::send_closure(right, &ShardExternalsPool::add_messages_impl, std::move(right_batch));
}

void ShardExternalsPool::drain_to_parent(td::Badge<WorkchainExternalsPool>,
                                         td::actor::ActorId<ShardExternalsPool> parent) {
  std::vector<PrioritizedExternal> batch;
  for (auto& [priority, entries] : entries_) {
    for (auto& entry : entries) {
      batch.push_back({std::move(entry.message), priority});
    }
  }
  entries_.clear();
  by_hash_.clear();
  expiry_order_.clear();
  td::actor::send_closure(parent, &ShardExternalsPool::add_messages_impl, std::move(batch));
}

ShardExternalsPool::Index ShardExternalsPool::index_for(int priority) {
  if (auto it = entries_.find(priority); it != entries_.end()) {
    return {priority, it->second.size()};
  }
  return {priority, 0};
}

void ShardExternalsPool::add(PrioritizedExternal external, ExtMessage::Hash hash) {
  CHECK(external.message->shard().inside(shard_));

  auto index = index_for(external.priority);
  auto [_, inserted] = by_hash_.emplace(hash, index);
  if (!inserted) {
    return;
  }
  auto ts = td::Timestamp::now().in(ttl);
  expiry_order_.insert({ts, index});
  entries_[external.priority].push_back({hash, std::move(external.message), ts});
}

void ShardExternalsPool::add_messages_impl(std::vector<PrioritizedExternal> entries) {
  for (auto& entry : entries) {
    auto hash = entry.hash();
    add(std::move(entry), hash);
  }
  reschedule_cleanup();
  if (queue_ && queue_->promise) {
    queue_->promise.set_value({});
  }
}

void ShardExternalsPool::remove(Index index) {
  auto& entries = entries_[index.priority];
  auto& entry = entries[index.i];
  by_hash_.erase(entry.hash);
  expiry_order_.erase({entry.ts, index});

  if (entries.size() == index.i + 1) {
    entries.pop_back();
    if (entries.size() == 0) {
      entries_.erase(index.priority);
    }
    return;
  }

  Index old_index{index.priority, entries.size() - 1};
  std::swap(entries[index.i], entries.back());
  entries.pop_back();
  auto& old_entry = entries[index.i];
  expiry_order_.erase({old_entry.ts, old_index});
  expiry_order_.insert({old_entry.ts, index});
  by_hash_[old_entry.hash] = index;
}

void ShardExternalsPool::reschedule_cleanup() {
  using namespace std::literals;
  auto ts = expiry_order_.empty() ? td::Timestamp{} : expiry_order_.begin()->ts + 1s;

  if (get_alarm_timestamp() != ts) {
    alarm_timestamp() = ts;
  }
}

void ShardExternalsPool::alarm() {
  while (!expiry_order_.empty()) {
    auto [ts, index] = *expiry_order_.begin();
    if (!ts.is_in_past()) {
      break;
    }
    remove(index);
  }
  reschedule_cleanup();
}

ExtMessageQueue ShardExternalsPoolReader::activate() {
  ExtMessageQueue queue("shard_ext_pool_queue", 100);
  td::actor::send_closure(pool_, &ShardExternalsPool::activate, Badge{}, queue);
  return queue;
}

void ShardExternalsPoolReader::deactivate(std::vector<ExtMessage::Hash> applied_messages) {
  td::actor::send_closure(pool_, &ShardExternalsPool::deactivate, Badge{}, std::move(applied_messages));
}

void ShardExternalsPoolReader::remove_messages(std::vector<ExtMessage::Hash> hashes) {
  td::actor::send_closure(pool_, &ShardExternalsPool::remove_messages, Badge{}, std::move(hashes));
}

void ShardExternalsPoolReader::add_messages(std::vector<PrioritizedExternal> entries) {
  td::actor::send_closure(pool_, &ShardExternalsPool::add_messages, Badge{}, std::move(entries));
}

}  // namespace ton::validator
