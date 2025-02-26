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

    Copyright 2017-2020 Telegram Systems LLP
*/
#pragma once

#include "td/utils/port/thread.h"
#include <atomic>
#include <memory>

namespace td {

class RwSpinLock {
  struct ReadUnlock {
    void operator()(RwSpinLock *ptr) {
      ptr->unlock_read();
    }
  };

  struct WriteUnlock {
    void operator()(RwSpinLock *ptr) {
      ptr->unlock_write();
    }
  };

  class InfBackoff {
    int cnt = 0;

   public:
    bool next() {
      cnt++;
      if (cnt < 50) {
        //TODO pause
        return true;
      } else {
        // td::this_thread::yield();
        return true;
      }
    }
  };

 public:
  using ReadLock = std::unique_ptr<RwSpinLock, ReadUnlock>;
  using WriteLock = std::unique_ptr<RwSpinLock, WriteUnlock>;

  ReadLock lock_read() {
    InfBackoff backoff;
    while (!try_lock_read()) {
      backoff.next();
    }
    return ReadLock(this);
  }

  WriteLock lock_write() {
    InfBackoff backoff;
    while (!try_lock_write()) {
      backoff.next();
    }
    return WriteLock(this);
  }

  bool try_lock_read() {
    uint32_t v = state_.load(std::memory_order_relaxed);
    if (v & WRITE_LOCKED) {
      return false;
    }
    return state_.compare_exchange_weak(v, v + READER_INC, std::memory_order_acquire);
  }

  bool try_lock_write() {
    uint32_t v = state_.load(std::memory_order_relaxed);
    if (v != 0) {
      return false;
    }
    return state_.compare_exchange_weak(v, WRITE_LOCKED, std::memory_order_acquire);
  }

 private:
  static constexpr uint32_t WRITE_LOCKED = 1;
  static constexpr uint32_t READER_INC = 2;
  std::atomic<uint32_t> state_{0};

  void unlock_read() {
    state_.fetch_sub(READER_INC, std::memory_order_release);
  }

  void unlock_write() {
    state_.store(0, std::memory_order_release);
  }
};

}  // namespace td 