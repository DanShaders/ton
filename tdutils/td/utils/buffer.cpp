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
#include <array>
#include <cstddef>
#include <new>
#include <vector>

#include "td/utils/SpinLock.h"
#include "td/utils/buffer.h"
#include "td/utils/port/thread_local.h"

// fixes https://bugs.llvm.org/show_bug.cgi?id=33723 for clang >= 3.6 + c++11 + libc++
#if TD_CLANG && _LIBCPP_VERSION
#define TD_OFFSETOF __builtin_offsetof
#else
#define TD_OFFSETOF offsetof
#endif

namespace td {

namespace {
// Prototype-only: pool large BufferRaws by size bucket to avoid the page-fault
// tax from constantly allocating fresh OS pages. Same-process worker threads
// hand BufferSlices between each other (alloc on QUIC worker, free on actor),
// so the pool has to be cross-thread safe — single SpinLock per bucket is
// cheap relative to the page faults we're avoiding (~3% of receiver thread).
//
// Round size up to nearest power of two starting at kBucketMinShift (= 16 KiB),
// so a 1.2 MiB receive buffer always reuses the 2 MiB bucket.
constexpr size_t kBucketMinShift = 14;  // 16 KiB
constexpr size_t kBucketCount = 16;     // up to 2^(14+15) = 512 MiB
constexpr size_t kPerBucketCap = 1024;  // large enough to keep ~all in-flight
                                        // 2 MiB buffers cached between messages

struct BufferRawBucketPool {
  SpinLock lock;
  std::vector<char *> free_list;
};

BufferRawBucketPool &bucket(size_t shift) {
  static std::array<BufferRawBucketPool, kBucketCount> pools;
  return pools[shift - kBucketMinShift];
}

// Returns SIZE_MAX if size is too small to pool. Otherwise returns the
// shift index (so pool size = 1 << shift).
size_t bucket_shift_for(size_t data_size) {
  if (data_size < (size_t(1) << kBucketMinShift)) {
    return SIZE_MAX;
  }
  size_t shift = kBucketMinShift;
  while ((size_t(1) << shift) < data_size) {
    ++shift;
    if (shift >= kBucketMinShift + kBucketCount) {
      return SIZE_MAX;  // too big — skip the pool
    }
  }
  return shift;
}
}  // namespace

TD_THREAD_LOCAL BufferAllocator::BufferRawTls *BufferAllocator::buffer_raw_tls;  // static zero-initialized

std::atomic<size_t> BufferAllocator::buffer_mem;

size_t BufferAllocator::get_buffer_mem() {
  return buffer_mem;
}

BufferAllocator::WriterPtr BufferAllocator::create_writer(size_t size) {
  if (size < 512) {
    size = 512;
  }
  return create_writer_exact(size);
}

BufferAllocator::WriterPtr BufferAllocator::create_writer_exact(size_t size) {
  return WriterPtr(create_buffer_raw(size));
}

BufferAllocator::WriterPtr BufferAllocator::create_writer(size_t size, size_t prepend, size_t append) {
  auto ptr = create_writer(size + prepend + append);
  ptr->begin_ += prepend;
  ptr->end_ += prepend + size;
  return ptr;
}

BufferAllocator::ReaderPtr BufferAllocator::create_reader(size_t size) {
  if (size < 512) {
    return create_reader_fast(size);
  }
  auto ptr = create_writer_exact(size);
  ptr->end_ += (size + 7) & -8;
  return create_reader(ptr);
}

BufferAllocator::ReaderPtr BufferAllocator::create_reader_fast(size_t size) {
  size = (size + 7) & -8;

  init_thread_local<BufferRawTls>(buffer_raw_tls);

  auto buffer_raw = buffer_raw_tls->buffer_raw.get();
  if (buffer_raw == nullptr || buffer_raw->data_size_ - buffer_raw->end_.load(std::memory_order_relaxed) < size) {
    buffer_raw = create_buffer_raw(4096 * 4);
    buffer_raw_tls->buffer_raw = std::unique_ptr<BufferRaw, BufferAllocator::BufferRawDeleter>(buffer_raw);
  }
  buffer_raw->end_.fetch_add(size, std::memory_order_relaxed);
  buffer_raw->ref_cnt_.fetch_add(1, std::memory_order_relaxed);
  return ReaderPtr(buffer_raw);
}

BufferAllocator::ReaderPtr BufferAllocator::create_reader(const WriterPtr &raw) {
  raw->was_reader_ = true;
  raw->ref_cnt_.fetch_add(1, std::memory_order_relaxed);
  return ReaderPtr(raw.get());
}

BufferAllocator::ReaderPtr BufferAllocator::create_reader(const ReaderPtr &raw) {
  raw->ref_cnt_.fetch_add(1, std::memory_order_relaxed);
  return ReaderPtr(raw.get());
}

void BufferAllocator::dec_ref_cnt(BufferRaw *ptr) {
  // Standard refcount pattern: release on dec, acquire fence before destroy.
  // Cheaper than acq_rel on every decrement.
  int left = ptr->ref_cnt_.fetch_sub(1, std::memory_order_release);
  if (left == 1) {
    std::atomic_thread_fence(std::memory_order_acquire);
    size_t data_size = ptr->data_size_;
    ptr->~BufferRaw();
    size_t shift = bucket_shift_for(data_size);
    if (shift != SIZE_MAX && data_size == (size_t(1) << shift)) {
      auto &pool = bucket(shift);
      auto lk = pool.lock.lock();
      if (pool.free_list.size() < kPerBucketCap) {
        pool.free_list.push_back(reinterpret_cast<char *>(ptr));
        return;
      }
    }
    auto buf_size = max(sizeof(BufferRaw), TD_OFFSETOF(BufferRaw, data_) + data_size);
    buffer_mem -= buf_size;
    delete[] reinterpret_cast<char *>(ptr);
  }
}

BufferRaw *BufferAllocator::create_buffer_raw(size_t size) {
  size = (size + 7) & -8;

  // Try to satisfy from the bucket pool (round up to power-of-two bucket).
  size_t shift = bucket_shift_for(size);
  if (shift != SIZE_MAX) {
    size_t bucket_size = size_t(1) << shift;
    auto &pool = bucket(shift);
    char *mem = nullptr;
    {
      auto lk = pool.lock.lock();
      if (!pool.free_list.empty()) {
        mem = pool.free_list.back();
        pool.free_list.pop_back();
      }
    }
    if (mem != nullptr) {
      auto *buffer_raw = reinterpret_cast<BufferRaw *>(mem);
      // Memory already counted in buffer_mem at original allocation; don't
      // double-count on reuse.
      return new (buffer_raw) BufferRaw(bucket_size);
    }
    // Pool empty — fall through and allocate at bucket size so the next free
    // returns the right shape.
    size = bucket_size;
  }

  auto buf_size = TD_OFFSETOF(BufferRaw, data_) + size;
  if (buf_size < sizeof(BufferRaw)) {
    buf_size = sizeof(BufferRaw);
  }
  buffer_mem += buf_size;
  auto *buffer_raw = reinterpret_cast<BufferRaw *>(new char[buf_size]);
  return new (buffer_raw) BufferRaw(size);
}

void BufferBuilder::append(BufferSlice slice) {
  if (append_inplace(slice.as_slice())) {
    return;
  }
  append_slow(std::move(slice));
}
void BufferBuilder::append(Slice slice) {
  if (append_inplace(slice)) {
    return;
  }
  append_slow(BufferSlice(slice));
}

void BufferBuilder::prepend(BufferSlice slice) {
  if (prepend_inplace(slice.as_slice())) {
    return;
  }
  prepend_slow(std::move(slice));
}
void BufferBuilder::prepend(Slice slice) {
  if (prepend_inplace(slice)) {
    return;
  }
  prepend_slow(BufferSlice(slice));
}

BufferSlice BufferBuilder::extract() {
  if (to_append_.empty() && to_prepend_.empty()) {
    return buffer_writer_.as_buffer_slice();
  }
  size_t total_size = size();
  BufferWriter writer(0, 0, total_size);
  std::move(*this).for_each([&](auto &&slice) {
    writer.prepare_append().truncate(slice.size()).copy_from(slice.as_slice());
    writer.confirm_append(slice.size());
  });
  *this = {};
  return writer.as_buffer_slice();
}

size_t BufferBuilder::size() const {
  size_t total_size = 0;
  for_each([&](auto &&slice) { total_size += slice.size(); });
  return total_size;
}

bool BufferBuilder::append_inplace(Slice slice) {
  if (!to_append_.empty()) {
    return false;
  }
  auto dest = buffer_writer_.prepare_append();
  if (dest.size() < slice.size()) {
    return false;
  }
  dest.remove_suffix(dest.size() - slice.size());
  dest.copy_from(slice);
  buffer_writer_.confirm_append(slice.size());
  return true;
}
void BufferBuilder::append_slow(BufferSlice slice) {
  to_append_.push_back(std::move(slice));
}
bool BufferBuilder::prepend_inplace(Slice slice) {
  if (!to_prepend_.empty()) {
    return false;
  }
  auto dest = buffer_writer_.prepare_prepend();
  if (dest.size() < slice.size()) {
    return false;
  }
  dest.remove_prefix(dest.size() - slice.size());
  dest.copy_from(slice);
  buffer_writer_.confirm_prepend(slice.size());
  return true;
}
void BufferBuilder::prepend_slow(BufferSlice slice) {
  to_prepend_.push_back(std::move(slice));
}
}  // namespace td
