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

namespace vm {
namespace detail {

struct DefaultAllocator {
  static void* allocate(size_t x) {
    return operator new(x);
  }
};

template <class CellT>
class CellWithInlineStorage {
  CellT* self() { return static_cast<CellT*>(this); }
  const CellT* self() const { return static_cast<const CellT*>(this); }
 public:
  ~CellWithInlineStorage() {
    self()->destroy_storage(self()->get_storage());
  }

  template <typename A = DefaultAllocator, class... ArgsT>
  static std::unique_ptr<CellT> create_alloc(size_t storage_size, ArgsT&&... args) {
    static_assert(alignof(CellWithInlineStorage) <= 8); // assumed by InMemoryBagOfCellsDb.cpp
    void* ptr = A::allocate(sizeof(CellT) + storage_size);
    CellT* ret = new(ptr) CellT(std::forward<ArgsT>(args)...);
    return std::unique_ptr<CellT>(ret);
  }

  // ensure that sized deallocation is not used for this class
  static void operator delete(void* ptr) {
    ::operator delete(ptr);
  }

 protected:
  const char* get_storage() const {
    return (const char*)(self() + 1);
  }
  char* get_storage() {
    return (char*)(self() + 1);
  }
};
}  // namespace detail
}  // namespace vm
