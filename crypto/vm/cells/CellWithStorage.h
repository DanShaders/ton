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
#include <atomic>

namespace vm {
namespace detail {

template <class CellT>
struct DefaultAllocator {
  template <class T, class... ArgsT>
  std::unique_ptr<CellT> make_unique(ArgsT&&... args) {
    return std::make_unique<T>(std::forward<ArgsT>(args)...);
  }
};

template <class CellT, size_t Size = 0>
class CellWithArrayStorage : public CellT {
 public:
  template <class... ArgsT>
  CellWithArrayStorage(ArgsT&&... args) : CellT(std::forward<ArgsT>(args)...) {
  }
  ~CellWithArrayStorage() {
    CellT::destroy_storage(get_storage());
  }
  template <class Allocator, class... ArgsT>
  static auto create(Allocator allocator, size_t storage_size, ArgsT&&... args) {
    static_assert(CellT::max_storage_size <= 40 * 8, "");
    //size = 128 + 32 + 8;
    auto size = (storage_size + 7) / 8;
#define CASE(size) \
  case (size):     \
    return allocator.template make_unique<CellWithArrayStorage<CellT, (size) * 8>>(std::forward<ArgsT>(args)...);
#define CASE2(offset) CASE(offset) CASE(offset + 1)
#define CASE8(offset) CASE2(offset) CASE2(offset + 2) CASE2(offset + 4) CASE2(offset + 6)
#define CASE32(offset) CASE8(offset) CASE8(offset + 8) CASE8(offset + 16) CASE8(offset + 24)
    switch (size) { CASE32(0) CASE8(32) }
#undef CASE
#undef CASE2
#undef CASE8
#undef CASE32
    LOG(FATAL) << "TOO BIG " << storage_size;
    UNREACHABLE();
  }
  template <class... ArgsT>
  static std::unique_ptr<CellT> create(size_t storage_size, ArgsT&&... args) {
    return create(DefaultAllocator<CellT>{}, storage_size, std::forward<ArgsT>(args)...);
  }

 private:
  alignas(alignof(void*)) char storage_[Size];

  const char* get_storage() const final {
    return storage_;
  }
  char* get_storage() final {
    return storage_;
  }
};

template <class CellT>
class CellWithUniquePtrStorage : public CellT {
 public:
  template <class... ArgsT>
  CellWithUniquePtrStorage(size_t storage_size, ArgsT&&... args)
      : CellT(std::forward<ArgsT>(args)...), storage_(std::make_unique<char[]>(storage_size)) {
  }
  ~CellWithUniquePtrStorage() {
    CellT::destroy_storage(get_storage());
  }

  template <class... ArgsT>
  static std::unique_ptr<CellT> create(size_t storage_size, ArgsT&&... args) {
    return std::make_unique<CellWithUniquePtrStorage>(storage_size, std::forward<ArgsT>(args)...);
  }

 private:
  std::unique_ptr<char[]> storage_;

  const char* get_storage() const final {
    CHECK(storage_);
    return storage_.get();
  }
  char* get_storage() final {
    CHECK(storage_);
    return storage_.get();
  }
};

template <class CellT>
class CellWithPreAllocateStorage : public CellT {
 public:
  template <class... ArgsT>
  CellWithPreAllocateStorage(void* storage_ptr, size_t storage_size, ArgsT&&... args)
      : CellT(std::forward<ArgsT>(args)...)
      , storage_(reinterpret_cast<char*>(storage_ptr)) {
  }
  ~CellWithPreAllocateStorage() {
    CellT::destroy_storage(get_storage());
  }

  struct nop_deleter {
    void operator()(CellWithPreAllocateStorage const& obj) const noexcept {
      obj.~CellWithPreAllocateStorage();
    }
  };

#define ALIGN_SIZE(SIZE, ALIGNMENT) \
  ((((uint32_t)(SIZE)) + ((uint32_t)(ALIGNMENT)) - 1) & (~(((uint32_t)(ALIGNMENT)) - 1)))

  template <class... ArgsT>
  static std::unique_ptr<CellT> create(size_t storage_size, ArgsT&&... args) {

    if (storage_size > CellT::max_storage_size) {
      LOG(FATAL) << "request size > CellT::max_storage_size";
      UNREACHABLE();
    }

    if (NULL == holder_ptr) {
      LOG(FATAL) << "holder_ptr NOT allocated";
      UNREACHABLE();
    }

  size_t align_size = ALIGN_SIZE(sizeof(CellWithPreAllocateStorage) + storage_size, 16);

  uint32_t offset = holder_offset.fetch_add(align_size) % max_holder_size;

  void* ptr = &holder_ptr[offset];
  void* storage_ptr = &holder_ptr[offset + ALIGN_SIZE(sizeof(CellWithPreAllocateStorage), 8)];

  return std::unique_ptr<CellWithPreAllocateStorage>(
      new (ptr) CellWithPreAllocateStorage(storage_ptr, storage_size, std::forward<ArgsT>(args)...));
  }

 private:

  static constexpr auto max_holder_size = 100 * 1024 * 1024;

  static uint8_t* holder_ptr;

  static std::atomic<long> holder_offset;

  char* storage_;

  const char* get_storage() const final {
    CHECK(storage_);
    return storage_;
  }
  char* get_storage() final {
    CHECK(storage_);
    return storage_;
  }
};

template <class CellT>
std::atomic<long> CellWithPreAllocateStorage<CellT>::holder_offset(0);

template <class CellT>
uint8_t* CellWithPreAllocateStorage<CellT>::holder_ptr =
    new (std::nothrow) uint8_t[max_holder_size + CellT::max_storage_size];  //+max cell for buffer roll

}  // namespace detail
}  // namespace vm
