/*
    This file is part of TON Blockchain Library.

    TON Blockchain Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    TON Blockchain Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with TON Blockchain Library.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2017-2025 Telegram Systems LLP
*/
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <vector>
#include <utility>

#include "td/utils/Slice-decl.h"

namespace vm {

static thread_local bool safe_to_dealloc_arena_at_thread_exit = false;

struct ArenaAllocatorBase {
  ArenaAllocatorBase() {
    batches.emplace_back(alloc_batch());
  }

 protected:
  struct Batch {
    friend struct ArenaAllocatorBase;
    static constexpr size_t batch_align = 1 << 12;
    static constexpr size_t batch_size = 1 << 22;

    char* ptr;
    td::MutableSlice batch;
    int* counter;

    Batch() = delete;
    Batch(char* p, td::MutableSlice b, int* c) : ptr(p), batch(b), counter(c) {
    }

    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;

    Batch(Batch&& other) : ptr(other.ptr), batch(other.batch), counter(other.counter) {
      other.ptr = nullptr;
      other.counter = nullptr;
    }
    Batch& operator=(Batch&& other) {
      ptr = other.ptr;
      batch = other.batch;
      counter = other.counter;
      other.ptr = nullptr;
      other.counter = nullptr;
      return *this;
    }

    ~Batch() {
      // In some use scenarios, things might be allocated in threads which are then destroyed.
      // In this case, preserve . Note that in in this case allocated memory will be never freed.
      // For this contest, it is OK to set safe_to_dealloc_arena_at_thread_exit to true
      // (we do not have a situation when allocated stuff is accessed from different thread
      // after initial thread was killed).
      if (safe_to_dealloc_arena_at_thread_exit) {
        std::free(ptr);
        delete counter;
      }
    }
  };

  std::vector<Batch> batches;

  static Batch alloc_batch() {
    char* ptr = reinterpret_cast<char*>(std::aligned_alloc(Batch::batch_align, Batch::batch_size));
    if (!ptr)
      throw std::bad_alloc();
    return {ptr, td::MutableSlice(ptr, Batch::batch_size), new int(0)};
  }
  std::pair<char*, int*> fast_alloc(size_t size) {
    assert(!batches.empty());
    Batch* batch = &batches.back();
    auto aligned_size = (size + 7) / 8 * 8;
    if (TD_UNLIKELY(batch->batch.size() < size)) {
      if (batches.size() == 1) {
        batches.emplace_back(alloc_batch());
      } else if (batches.size() == 2) {
        if (batches.front().counter == 0) {
          std::swap(batches.front(), batches.back());
        } else {
          batches.emplace_back(alloc_batch());
        }
      } else {
        bool found = false;
        for (auto it = batches.begin(); it != batches.end(); ++it) {
          if (*it->counter != 0)
            continue;
          Batch b = std::move(*it);
          b.batch = td::MutableSlice(b.ptr, Batch::batch_size);
          batches.erase(it);
          batches.emplace_back(std::move(b));
          found = true;
          break;
        }
        if (!found)
          batches.emplace_back(alloc_batch());
      }
      batch = &batches.back();
    }
    ++(*batch->counter);
    auto res = batch->batch.begin();
    batch->batch.remove_prefix(aligned_size);
    return {res, batch->counter};
  }
};

}  // namespace vm
