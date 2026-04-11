/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <set>

#include "td/actor/TestScheduler.h"
#include "td/utils/tests.h"

#include "shard-ext-message-pool.h"
#include "workchain-ext-message-pool.h"

namespace ton::validator {
namespace {

using Hash = ExtMessage::Hash;

// Stub ExtMessage for testing. Carries a synthetic hash and shard prefix; other methods are unused.
class StubExtMessage : public ExtMessage {
 public:
  StubExtMessage(AccountIdPrefixFull prefix, Hash hash) : prefix_(prefix), hash_(hash) {
  }

  AccountIdPrefixFull shard() const override {
    return prefix_;
  }
  td::BufferSlice serialize() const override {
    return {};
  }
  td::Ref<vm::Cell> root_cell() const override {
    return {};
  }
  Hash hash() const override {
    return hash_;
  }
  Hash hash_norm() const override {
    return hash_;
  }
  ton::WorkchainId wc() const override {
    return prefix_.workchain;
  }
  ton::StdSmcAddress addr() const override {
    return {};
  }

 private:
  AccountIdPrefixFull prefix_;
  Hash hash_;
};

// Helper: create a StubExtMessage with a given prefix and a unique hash derived from `id`.
td::Ref<ExtMessage> make_msg(WorkchainId wc, td::uint64 prefix, td::uint8 id) {
  Hash h = Hash::zero();
  h.as_array()[0] = id;
  return td::make_ref<StubExtMessage>(AccountIdPrefixFull{wc, prefix}, h);
}

// Helper: extract the id byte we put in the hash.
td::uint8 msg_id(const PrioritizedExternal& m) {
  return m.hash().as_array()[0];
}

Hash hash_for(td::uint8 id) {
  Hash h = Hash::zero();
  h.as_array()[0] = id;
  return h;
}

// ============================================================================
// 1. ShardExternalsPool tests (through WorkchainExternalsPool + Reader)
// ============================================================================

TEST(ShardExternalsPool, ActivateDelivers) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto shard = ShardIdFull{0};

    // Add a message before activating.
    wpool.add_external(make_msg(0, 0x4000000000000000ULL, 1), 10);

    auto token = wpool.take_token(shard);
    auto queue = token.activate();
    co_await ts.wait_sync_work();

    // The message should arrive on the queue.
    auto got = co_await queue.pop();
    EXPECT_EQ(1, msg_id(got));

    token.deactivate({got.hash()});
    co_await ts.wait_sync_work();
    wpool.store_token(std::move(token));
    co_return {};
  });
}

TEST(ShardExternalsPool, AddAfterActivateWakes) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto shard = ShardIdFull{0};

    auto token = wpool.take_token(shard);
    auto queue = token.activate();
    co_await ts.wait_sync_work();

    // Pool is empty; start a pop that will block.
    auto pop_task = queue.pop();

    // Now add a message -- it should wake the coroutine and appear in the queue.
    wpool.add_external(make_msg(0, 0x4000000000000000ULL, 2), 5);
    co_await ts.wait_sync_work();

    auto got = co_await std::move(pop_task);
    EXPECT_EQ(2, msg_id(got));

    token.deactivate({got.hash()});
    co_await ts.wait_sync_work();
    wpool.store_token(std::move(token));
    co_return {};
  });
}

TEST(ShardExternalsPool, DeactivateReturnsUnapplied) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto shard = ShardIdFull{0};

    // Add two messages, activate, pop both, deactivate saying one was applied.
    wpool.add_external(make_msg(0, 0x4000000000000000ULL, 1), 10);
    wpool.add_external(make_msg(0, 0x4000000000000000ULL, 2), 10);

    auto token = wpool.take_token(shard);
    auto queue = token.activate();
    co_await ts.wait_sync_work();

    // Pop both messages from the queue.
    auto got1 = co_await queue.pop();
    co_await ts.wait_sync_work();
    auto got2 = co_await queue.pop();

    // Deactivate, saying got1 was applied. got2 should be returned to the pool.
    token.deactivate({got1.hash()});
    co_await ts.wait_sync_work();

    // Activate again and verify we get back the unapplied message.
    auto queue2 = token.activate();
    co_await ts.wait_sync_work();

    auto got3 = co_await queue2.pop();
    EXPECT_EQ(msg_id(got2), msg_id(got3));

    token.deactivate({got3.hash()});
    co_await ts.wait_sync_work();
    wpool.store_token(std::move(token));
    co_return {};
  });
}

TEST(ShardExternalsPool, DeactivateClosesQueue) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto shard = ShardIdFull{0};

    auto token = wpool.take_token(shard);
    auto queue = token.activate();
    co_await ts.wait_sync_work();

    // Start a pop that will block (pool is empty).
    auto pop_task = queue.pop();

    // Deactivate should close the queue.
    token.deactivate({});
    co_await ts.wait_sync_work();

    auto result = co_await std::move(pop_task).wrap();
    EXPECT(result.is_error());

    wpool.store_token(std::move(token));
    co_return {};
  });
}

TEST(ShardExternalsPool, RemoveMessages) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto shard = ShardIdFull{0};

    wpool.add_external(make_msg(0, 0x4000000000000000ULL, 1), 10);
    wpool.add_external(make_msg(0, 0x4000000000000000ULL, 2), 10);

    auto token = wpool.take_token(shard);

    // Remove m1 through the reader.
    token.remove_messages({hash_for(1)});
    co_await ts.wait_sync_work();

    // Activate: only m2 should come out.
    auto queue = token.activate();
    co_await ts.wait_sync_work();

    auto got = co_await queue.pop();
    EXPECT_EQ(2, msg_id(got));

    token.deactivate({got.hash()});
    co_await ts.wait_sync_work();
    wpool.store_token(std::move(token));
    co_return {};
  });
}

TEST(ShardExternalsPool, AddMessages) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto shard = ShardIdFull{0};

    auto token = wpool.take_token(shard);

    // Use add_messages (the bulk variant used by ShardExternalsPoolReader).
    std::vector<PrioritizedExternal> batch;
    batch.push_back({make_msg(0, 0x4000000000000000ULL, 10), 5});
    batch.push_back({make_msg(0, 0x4000000000000000ULL, 11), 5});
    token.add_messages(std::move(batch));
    co_await ts.wait_sync_work();

    auto queue = token.activate();
    co_await ts.wait_sync_work();

    std::set<td::uint8> ids;
    auto got1 = co_await queue.pop();
    ids.insert(msg_id(got1));
    co_await ts.wait_sync_work();
    auto got2 = co_await queue.pop();
    ids.insert(msg_id(got2));

    EXPECT(ids.count(10));
    EXPECT(ids.count(11));

    token.deactivate({got1.hash(), got2.hash()});
    co_await ts.wait_sync_work();
    wpool.store_token(std::move(token));
    co_return {};
  });
}

TEST(ShardExternalsPool, DuplicateHashIgnored) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto shard = ShardIdFull{0};

    wpool.add_external(make_msg(0, 0x4000000000000000ULL, 1), 10);
    // Add again with same hash -- should be ignored.
    wpool.add_external(make_msg(0, 0x4000000000000000ULL, 1), 10);

    auto token = wpool.take_token(shard);
    auto queue = token.activate();
    co_await ts.wait_sync_work();

    auto got = co_await queue.pop();
    EXPECT_EQ(1, msg_id(got));

    // Nothing else should be in the pool -- deactivate with got applied, reactivate -- pool empty.
    token.deactivate({got.hash()});
    co_await ts.wait_sync_work();

    auto queue2 = token.activate();
    co_await ts.wait_sync_work();

    // Try pop should fail (empty).
    auto result = co_await queue2.try_pop().wrap();
    EXPECT(result.is_error());

    token.deactivate({});
    co_await ts.wait_sync_work();
    wpool.store_token(std::move(token));
    co_return {};
  });
}

TEST(ShardExternalsPool, HighPriorityFirst) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto shard = ShardIdFull{0};

    // Add messages with different priorities.
    wpool.add_external(make_msg(0, 0x4000000000000000ULL, 1), 1);
    wpool.add_external(make_msg(0, 0x4000000000000000ULL, 2), 100);

    auto token = wpool.take_token(shard);
    auto queue = token.activate();
    co_await ts.wait_sync_work();

    // Highest priority message comes first.
    auto got = co_await queue.pop();
    EXPECT_EQ(2, msg_id(got));

    token.deactivate({got.hash()});
    co_await ts.wait_sync_work();
    wpool.store_token(std::move(token));
    co_return {};
  });
}

TEST(ShardExternalsPool, Expiry) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto shard = ShardIdFull{0};

    wpool.add_external(make_msg(0, 0x4000000000000000ULL, 1), 10);
    co_await ts.wait_sync_work();

    // Advance time past TTL (600s) + the 1s buffer in reschedule_cleanup.
    ts.advance_time(std::chrono::duration<double>(602.0));
    co_await ts.wait_sync_work();

    // Activate; the message should have expired and been removed.
    auto token = wpool.take_token(shard);
    auto queue = token.activate();
    co_await ts.wait_sync_work();

    auto result = co_await queue.try_pop().wrap();
    EXPECT(result.is_error());

    token.deactivate({});
    co_await ts.wait_sync_work();
    wpool.store_token(std::move(token));
    co_return {};
  });
}

// ============================================================================
// 2. WorkchainExternalsPool: routing, tokens, split, merge
// ============================================================================

TEST(WorkchainExternalsPool, TakeAndStoreToken) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto shard = ShardIdFull{0};

    auto token = wpool.take_token(shard);
    EXPECT(static_cast<bool>(token));
    EXPECT(token.shard() == shard);

    wpool.store_token(std::move(token));
    co_return {};
  });
}

TEST(WorkchainExternalsPool, AddExternalRouted) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto shard = ShardIdFull{0};

    wpool.add_external(make_msg(0, 0x4000000000000000ULL, 1), 10);
    co_await ts.wait_sync_work();

    // Take token, activate, verify message arrives.
    auto token = wpool.take_token(shard);
    auto queue = token.activate();
    co_await ts.wait_sync_work();

    auto got = co_await queue.pop();
    EXPECT_EQ(1, msg_id(got));

    token.deactivate({got.hash()});
    co_await ts.wait_sync_work();
    wpool.store_token(std::move(token));
    co_return {};
  });
}

TEST(WorkchainExternalsPool, SplitRoutesMessages) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto root_shard = ShardIdFull{0};

    // Prefix 0x0... -> left half, prefix 0x8... -> right half after split at root.
    wpool.add_external(make_msg(0, 0x2000000000000000ULL, 1), 10);
    wpool.add_external(make_msg(0, 0xA000000000000000ULL, 2), 10);
    co_await ts.wait_sync_work();

    // Split the root shard.
    auto parent_token = wpool.take_token(root_shard);
    auto [left_token, right_token] = wpool.split(std::move(parent_token));
    co_await ts.wait_sync_work();

    auto left_shard = left_token.shard();
    auto right_shard = right_token.shard();

    // Verify left shard and right shard are correct halves.
    EXPECT(left_shard == root_shard.left());
    EXPECT(right_shard == root_shard.right());

    // Activate left: should get message 1.
    auto lq = left_token.activate();
    co_await ts.wait_sync_work();
    auto got_l = co_await lq.pop();
    EXPECT_EQ(1, msg_id(got_l));

    // Activate right: should get message 2.
    auto rq = right_token.activate();
    co_await ts.wait_sync_work();
    auto got_r = co_await rq.pop();
    EXPECT_EQ(2, msg_id(got_r));

    left_token.deactivate({got_l.hash()});
    right_token.deactivate({got_r.hash()});
    co_await ts.wait_sync_work();

    wpool.store_token(std::move(left_token));
    wpool.store_token(std::move(right_token));
    co_return {};
  });
}

TEST(WorkchainExternalsPool, MergeCollectsMessages) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto root_shard = ShardIdFull{0};

    // Split first.
    auto parent_token = wpool.take_token(root_shard);
    auto [left_token, right_token] = wpool.split(std::move(parent_token));
    co_await ts.wait_sync_work();

    // Add messages to each child shard via the workchain pool.
    wpool.add_external(make_msg(0, 0x2000000000000000ULL, 1), 10);
    wpool.add_external(make_msg(0, 0xA000000000000000ULL, 2), 10);
    co_await ts.wait_sync_work();

    // Merge back.
    auto merged_token = wpool.merge(std::move(left_token), std::move(right_token));
    co_await ts.wait_sync_work();

    EXPECT(merged_token.shard() == root_shard);

    // Activate merged shard: should get both messages.
    auto queue = merged_token.activate();
    co_await ts.wait_sync_work();

    std::set<td::uint8> ids;
    auto got1 = co_await queue.pop();
    ids.insert(msg_id(got1));
    co_await ts.wait_sync_work();
    auto got2 = co_await queue.pop();
    ids.insert(msg_id(got2));

    EXPECT(ids.count(1));
    EXPECT(ids.count(2));

    merged_token.deactivate({got1.hash(), got2.hash()});
    co_await ts.wait_sync_work();
    wpool.store_token(std::move(merged_token));
    co_return {};
  });
}

TEST(WorkchainExternalsPool, AddExternalAfterSplit) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto root_shard = ShardIdFull{0};

    auto parent_token = wpool.take_token(root_shard);
    auto [left_token, right_token] = wpool.split(std::move(parent_token));
    co_await ts.wait_sync_work();

    // After split, adding an external should route to the correct child shard.
    wpool.add_external(make_msg(0, 0xC000000000000000ULL, 3), 5);  // right half
    co_await ts.wait_sync_work();

    auto rq = right_token.activate();
    co_await ts.wait_sync_work();

    auto got = co_await rq.pop();
    EXPECT_EQ(3, msg_id(got));

    right_token.deactivate({got.hash()});
    co_await ts.wait_sync_work();

    wpool.store_token(std::move(left_token));
    wpool.store_token(std::move(right_token));
    co_return {};
  });
}

// ============================================================================
// 3. ShardExternalsPoolReader: move semantics, activate/deactivate
// ============================================================================

TEST(ShardExternalsPoolReader, MoveSemantics) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto shard = ShardIdFull{0};

    auto token = wpool.take_token(shard);
    EXPECT(static_cast<bool>(token));

    // Move construct.
    auto token2 = std::move(token);
    EXPECT(!static_cast<bool>(token));
    EXPECT(static_cast<bool>(token2));
    EXPECT(token2.shard() == shard);

    // Move assign.
    ShardExternalsPoolReader token3;
    EXPECT(!static_cast<bool>(token3));
    token3 = std::move(token2);
    EXPECT(!static_cast<bool>(token2));
    EXPECT(static_cast<bool>(token3));

    wpool.store_token(std::move(token3));
    co_return {};
  });
}

TEST(ShardExternalsPoolReader, ActivateDeactivateCycle) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto shard = ShardIdFull{0};

    wpool.add_external(make_msg(0, 0x4000000000000000ULL, 7), 10);
    co_await ts.wait_sync_work();

    auto token = wpool.take_token(shard);

    // Activate.
    auto queue = token.activate();
    co_await ts.wait_sync_work();

    auto got = co_await queue.pop();
    EXPECT_EQ(7, msg_id(got));

    // Deactivate with applied messages.
    token.deactivate({got.hash()});
    co_await ts.wait_sync_work();

    wpool.store_token(std::move(token));
    co_return {};
  });
}

TEST(ShardExternalsPoolReader, RemoveAndAddMessages) {
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto shard = ShardIdFull{0};

    wpool.add_external(make_msg(0, 0x4000000000000000ULL, 1), 10);
    wpool.add_external(make_msg(0, 0x4000000000000000ULL, 2), 10);
    co_await ts.wait_sync_work();

    auto token = wpool.take_token(shard);

    // Use remove_messages through the reader interface.
    token.remove_messages({hash_for(1)});
    co_await ts.wait_sync_work();

    // Use add_messages through the reader interface.
    token.add_messages({{make_msg(0, 0x4000000000000000ULL, 3), 20}});
    co_await ts.wait_sync_work();

    auto queue = token.activate();
    co_await ts.wait_sync_work();

    // m3 has highest priority (20), should come first.
    auto got1 = co_await queue.pop();
    EXPECT_EQ(3, msg_id(got1));
    co_await ts.wait_sync_work();

    // m2 (priority 10) should come next.
    auto got2 = co_await queue.pop();
    EXPECT_EQ(2, msg_id(got2));

    token.deactivate({got1.hash(), got2.hash()});
    co_await ts.wait_sync_work();
    wpool.store_token(std::move(token));
    co_return {};
  });
}

// ============================================================================
// 4. BackpressureQueue integration
// ============================================================================

TEST(ShardExternalsPool, QueueStreaming) {
  // Verify that messages pushed into the pool during active collation
  // are delivered through the BackpressureQueue returned by activate().
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto shard = ShardIdFull{0};

    // Add 150 messages (more than the queue capacity of 100) to exercise
    // the flow control between the pool's push coroutine and the consumer.
    for (int i = 0; i < 150; i++) {
      // Each message needs a unique hash; use the low byte for ids 0..149.
      Hash h = Hash::zero();
      h.as_array()[0] = static_cast<td::uint8>(i & 0xFF);
      h.as_array()[1] = static_cast<td::uint8>((i >> 8) & 0xFF);
      auto msg = td::make_ref<StubExtMessage>(AccountIdPrefixFull{0, 0x4000000000000000ULL}, h);
      wpool.add_external(msg, 10);
    }
    co_await ts.wait_sync_work();

    auto token = wpool.take_token(shard);
    auto queue = token.activate();
    co_await ts.wait_sync_work();

    // Pop all 150 messages. The pool's coroutine must push them through
    // the BackpressureQueue, blocking when the queue is full.
    int count = 0;
    for (int i = 0; i < 150; i++) {
      auto got = co_await queue.pop();
      count++;
      co_await ts.wait_sync_work();
    }
    EXPECT_EQ(150, count);

    token.deactivate({});
    co_await ts.wait_sync_work();
    wpool.store_token(std::move(token));
    co_return {};
  });
}

TEST(ShardExternalsPool, AddDuringActiveCollation) {
  // Verify that messages added while the queue is active are
  // pushed through the queue to the consumer.
  td::actor::TestScheduler ts;
  ts.run([&]() -> td::actor::Task<td::Unit> {
    WorkchainExternalsPool wpool(0);
    auto shard = ShardIdFull{0};

    auto token = wpool.take_token(shard);
    auto queue = token.activate();
    co_await ts.wait_sync_work();

    // Add messages one at a time while the queue is active.
    for (td::uint8 i = 1; i <= 5; i++) {
      wpool.add_external(make_msg(0, 0x4000000000000000ULL, i), 10);
      co_await ts.wait_sync_work();

      auto got = co_await queue.pop();
      EXPECT_EQ(i, msg_id(got));
      co_await ts.wait_sync_work();
    }

    token.deactivate({});
    co_await ts.wait_sync_work();
    wpool.store_token(std::move(token));
    co_return {};
  });
}

}  // namespace
}  // namespace ton::validator
