/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <compare>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/common.h"

namespace td {

// File format:
//
//     # comments start with #; blank lines between entries are ignored
//
//     Test_Foo_Bar :: default :: 38
//     {state=ChainState{...}}
//     two-line content with \n
//
//     Test_Foo_Baz :: with_x :: 12
//     short content
class SnapshotStorage {
 public:
  SnapshotStorage(std::string path, bool update_mode);

  // Reads existing snapshots from disk. A missing file is OK (no entries loaded).
  Status load();

  // Writes the canonicalised (sorted) set of entries. Entries that weren't
  // `mark_used`'d during the run are dropped.
  Status save();

  // Looks up a recorded snapshot. Returns std::nullopt if missing.
  std::optional<std::string> get(Slice test_name, Slice key) const;

  // Sets / overwrites an entry.
  void set(std::string test_name, std::string key, std::string content);

  // Marks an entry as touched by the current run.
  void mark_used(Slice test_name, Slice key);

  bool update_mode() const {
    return update_mode_;
  }

  const std::string& path() const {
    return path_;
  }

 private:
  struct Key {
    std::string test_name;
    std::string key;

    std::strong_ordering operator<=>(const Key&) const = default;
  };

  std::string path_;
  bool update_mode_;
  mutable std::mutex mutex_;
  std::map<Key, std::string> entries_;
  std::set<Key> used_;
};

namespace detail {

bool check_snapshot(Slice key, Slice actual);

}  // namespace detail

}  // namespace td

#define EXPECT_SNAPSHOT(...)                                       \
  do {                                                             \
    if (!::td::detail::check_snapshot("default", (__VA_ARGS__))) { \
      ::td::TestContext::get()->register_test_failure();           \
    }                                                              \
  } while (0)

#define EXPECT_SNAPSHOT_NAMED(key, ...)                        \
  do {                                                         \
    if (!::td::detail::check_snapshot((key), (__VA_ARGS__))) { \
      ::td::TestContext::get()->register_test_failure();       \
    }                                                          \
  } while (0)
