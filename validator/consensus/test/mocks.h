/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "auto/tl/ton_api.h"
#include "keyring/keyring.h"
#include "td/actor/Mocks.h"
#include "td/utils/logging.h"
#include "td/utils/buffer.h"
#include "td/utils/check.h"
#include "td/utils/logging.h"
#include "tl-utils/common-utils.hpp"
#include "validator/consensus/bus.h"
#include "validator/consensus/test/fixtures.h"

namespace ton::validator::consensus::test {

// =============================================================================
// DB-call event types — published into the bus event log by MockDb so resolver
// tests can EXPECT(...) the precise sequence of disk operations.
// =============================================================================

struct DbGet {
  td::BufferSlice key;
  bool operator==(const DbGet& o) const {
    return key.as_slice() == o.key.as_slice();
  }
  std::string contents_to_string() const {
    return PSTRING() << "DbGet{" << key << "}";
  }
};

struct DbGetByPrefix {
  td::uint32 prefix;
  bool operator==(const DbGetByPrefix&) const = default;
  std::string contents_to_string() const {
    return PSTRING() << "DbGetByPrefix{" << td::BufferSlice(reinterpret_cast<const char*>(&prefix), 4) << "}";
  }
};

struct DbSet {
  td::BufferSlice key;
  td::BufferSlice value;
  bool operator==(const DbSet& o) const {
    return key.as_slice() == o.key.as_slice() && value.as_slice() == o.value.as_slice();
  }
  std::string contents_to_string() const {
    return PSTRING() << "DbSet{" << key << ", " << value << "}";
  }
};

// =============================================================================
// MockDb — in-memory map that publishes DbGet/DbGetByPrefix/DbSet events into
// the bus event log. Use when the test needs to assert *which* DB calls
// happened, in what order. For tests that don't care, use SilentDb instead.
// =============================================================================

template <typename BusType>
class MockDb : public consensus::Db {
 public:
  explicit MockDb(BusType* bus) : bus_(bus) {
  }

  void seed(td::BufferSlice key, td::BufferSlice value) {
    data_[std::move(key)] = std::move(value);
  }

  std::optional<td::BufferSlice> get(td::Slice key) const override {
    td::BufferSlice k(key);
    bus_->template log<DbGet>(std::make_shared<DbGet>(k.clone()));
    auto it = data_.find(k);
    return it == data_.end() ? std::nullopt : std::make_optional(it->second.clone());
  }

  std::vector<std::pair<td::BufferSlice, td::BufferSlice>> get_by_prefix(td::uint32 prefix) const override {
    bus_->template log<DbGetByPrefix>(std::make_shared<DbGetByPrefix>(prefix));
    std::vector<std::pair<td::BufferSlice, td::BufferSlice>> res;
    for (const auto& [key, value] : data_) {
      if (key.size() >= 4 && *reinterpret_cast<const td::uint32*>(key.data()) == prefix) {
        res.emplace_back(key.clone(), value.clone());
      }
    }
    return res;
  }

  td::actor::Task<> set(td::BufferSlice key, td::BufferSlice value) override {
    bus_->template log<DbSet>(std::make_shared<DbSet>(key.clone(), value.clone()));
    data_[std::move(key)] = std::move(value);
    co_return {};
  }

  td::actor::Task<> close() override {
    UNREACHABLE();
    co_return {};
  }

 private:
  BusType* bus_;
  std::map<td::BufferSlice, td::BufferSlice> data_;
};

// =============================================================================
// SilentDb — in-memory map, no event logging. Default backing store for tests
// that don't care about DB introspection.
// =============================================================================

class SilentDb : public consensus::Db {
 public:
  std::optional<td::BufferSlice> get(td::Slice key) const override {
    auto it = data_.find(td::BufferSlice{key});
    return it == data_.end() ? std::nullopt : std::make_optional(it->second.clone());
  }

  std::vector<std::pair<td::BufferSlice, td::BufferSlice>> get_by_prefix(td::uint32 prefix) const override {
    std::vector<std::pair<td::BufferSlice, td::BufferSlice>> res;
    for (const auto& [key, value] : data_) {
      if (key.size() >= 4 && *reinterpret_cast<const td::uint32*>(key.data()) == prefix) {
        res.emplace_back(key.clone(), value.clone());
      }
    }
    return res;
  }

  td::actor::Task<> set(td::BufferSlice key, td::BufferSlice value) override {
    data_[std::move(key)] = std::move(value);
    co_return {};
  }

  td::actor::Task<> close() override {
    co_return {};
  }

 private:
  std::map<td::BufferSlice, td::BufferSlice> data_;
};

// =============================================================================
// SignMessage — emitted by MockKeyring on sign_message(). Lets pool tests
// observe what bytes the actor under test asked to sign, in what order.
// =============================================================================

struct SignMessage {
  PublicKeyHash key_hash;
  td::BufferSlice data;

  bool operator==(const SignMessage& o) const {
    return key_hash == o.key_hash && data.as_slice() == o.data.as_slice();
  }
  std::string contents_to_string() const {
    return PSTRING() << "SignMessage{" << key_hash << ", " << data << "}";
  }
};

// =============================================================================
// MockKeyring — synthesizes signatures from a private-key vector held next to
// the bus. Logs each sign request as SignMessage on the bus.
//
// The unused interface methods abort if called; tests that need a richer
// keyring should subclass and override.
// =============================================================================

template <typename BusType>
class MockKeyring : public keyring::Keyring {
 public:
  MockKeyring(BusType* bus, std::vector<PrivateKey>* keys) : bus_(bus), keys_(keys) {
  }

  void sign_message(PublicKeyHash key_hash, td::BufferSlice data, td::Promise<td::BufferSlice> promise) override {
    bus_->template log<SignMessage>(std::make_shared<SignMessage>(key_hash, data.clone()));
    for (auto& priv : *keys_) {
      if (priv.compute_public_key().compute_short_id() == key_hash) {
        promise.set_value(sign_with(priv, data.as_slice()));
        return;
      }
    }
    promise.set_error(td::Status::Error("MockKeyring: unknown key"));
  }

  // Unused — fail loudly if a future test starts depending on them.
  void add_key(PrivateKey, bool, td::Promise<td::Unit>) override {
    UNREACHABLE();
  }
  void check_key(PublicKeyHash, td::Promise<td::Unit>) override {
    UNREACHABLE();
  }
  void add_key_short(PublicKeyHash, td::Promise<PublicKey>) override {
    UNREACHABLE();
  }
  void del_key(PublicKeyHash, td::Promise<td::Unit>) override {
    UNREACHABLE();
  }
  void export_private_key(PublicKeyHash, td::Promise<PrivateKey>) override {
    UNREACHABLE();
  }
  void get_public_key(PublicKeyHash, td::Promise<PublicKey>) override {
    UNREACHABLE();
  }
  void sign_add_get_public_key(PublicKeyHash, td::BufferSlice,
                               td::Promise<std::pair<td::BufferSlice, PublicKey>>) override {
    UNREACHABLE();
  }
  void sign_messages(PublicKeyHash, std::vector<td::BufferSlice>,
                     td::Promise<std::vector<td::Result<td::BufferSlice>>>) override {
    UNREACHABLE();
  }
  void decrypt_message(PublicKeyHash, td::BufferSlice, td::Promise<td::BufferSlice>) override {
    UNREACHABLE();
  }
  void export_all_private_keys(td::Promise<std::vector<PrivateKey>>) override {
    UNREACHABLE();
  }

 private:
  BusType* bus_;
  std::vector<PrivateKey>* keys_;
};

}  // namespace ton::validator::consensus::test
