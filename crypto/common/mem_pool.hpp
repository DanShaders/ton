#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <vector>

namespace td {

template <std::size_t obj_size>
class MemoryPool {
 public:
  void* allocate() {
    if (!first_free_block_)
      allocate_page();

    assert(first_free_block_);
    assert(first_free_block_->size > 0);

    --first_free_block_->size;
    void* res = reinterpret_cast<char*>(first_free_block_) + obj_size * first_free_block_->size;
    if (first_free_block_->size == 0)
      first_free_block_ = first_free_block_->next;

    return res;
  }

  void release(void* obj) {
    FreeBlockHeader* hdr = reinterpret_cast<FreeBlockHeader*>(obj);
    hdr->size = 1;
    hdr->next = first_free_block_;
    first_free_block_ = hdr;
  }

 private:
  struct FreeBlockHeader {
    FreeBlockHeader* next;
    std::size_t size;
  };

  static_assert(sizeof(FreeBlockHeader) <= obj_size);

  void allocate_page() {
    constexpr std::size_t page_size = 4096 * 8;
    std::unique_ptr<char[]> page = std::make_unique<char[]>(page_size);
    FreeBlockHeader* hdr = reinterpret_cast<FreeBlockHeader*>(page.get());
    hdr->size = page_size / obj_size;
    hdr->next = first_free_block_;
    first_free_block_ = hdr;
    pages_.push_back(std::move(page));
  }

  std::vector<std::unique_ptr<char[]>> pages_;
  FreeBlockHeader* first_free_block_ = nullptr;
};

template <typename T>
auto& getMemoryPool() {
  static MemoryPool<sizeof(T)> instance;
  return instance;
}

}  // namespace td
