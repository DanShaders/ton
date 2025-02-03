
#pragma once

#include "refcnt.hpp"

namespace td {
namespace tl_policies {
namespace memory {

struct IAllocator {
  virtual ~IAllocator() = default;
  virtual void* allocate(std::size_t size) = 0;
  virtual void deallocate(const void* ptr) = 0;
};

struct DefaultAllocator : public IAllocator {
  void* allocate(std::size_t size) override {
    return malloc(size);
  }
  void deallocate(const void* ptr) override {
    free((void*)ptr);
  }
};

struct MultiPagedFixedBlockAllocator : public IAllocator {
  explicit MultiPagedFixedBlockAllocator(std::size_t size) : size_(size) {
    cur_ = ptr_ = static_cast<std::uint8_t*>(malloc(size_));
    if (!ptr_) {
      throw std::bad_alloc();
    }
    end_ = ptr_ + size;
  }

  ~MultiPagedFixedBlockAllocator() override {
    free(ptr_);
  }

  void* allocate(std::size_t count) override {
    std::uint8_t* t = cur_;
    cur_ += (count + 7) & -8;
    if (cur_ > end_) {
      throw std::bad_alloc();
    }
    return (void*)t;
  }
  void deallocate(const void* ptr) override {
  }
  std::size_t used() const {
    return cur_ - ptr_;
  }
  void clear() {
    cur_ = ptr_;
  }
 private:
  std::uint8_t *ptr_, *cur_, *end_;
  std::size_t size_;
};

inline IAllocator* get_default_allocator() {
  static DefaultAllocator obj;
  return &obj;
}

inline IAllocator* get_multipaged_fixed_block_allocator() {
  static TD_THREAD_LOCAL MultiPagedFixedBlockAllocator obj{static_cast<uint64_t>(1.5 * 1024.0 * 1024.0 * 1024.0)};
  return &obj;
}

/// \brief Sets a localized memory allocation and deallocation policy.
///   It can be used to change the allocation mechanism for a specific
///   place in the code without affecting the behavior elsewhere.
class Policy {
 private:
  Policy() = default;

  static IAllocator*& instance() {
    static TD_THREAD_LOCAL IAllocator* obj = 
      //get_multipaged_fixed_block_allocator();
      get_default_allocator();
    return obj;
  }

 public:
  static IAllocator* get() {
    return instance();
  }

  static void set(IAllocator* ptr) {
    instance() = ptr;
  }
};

inline void* allocate(std::size_t count) {
  return Policy::get()->allocate(count);
}

struct PolicyHolder {
  explicit PolicyHolder(IAllocator* allocator) {
    Policy::set(allocator);
  }
  ~PolicyHolder() {
    Policy::set(get_default_allocator());
  }
};

/// Cannot be used for classes that have virtual inheritance, such as:
///   MasterchainState->public virtual->ShardState->CntObject
class Switchable : public CntObject {
 public:
  virtual ~Switchable() = default;

  static void* operator new(std::size_t count) {
    return allocate(count);
  }

  static void operator delete(void* ptr) {
    Switchable* p = static_cast<Switchable*>(ptr);
    p->allocator_->deallocate(ptr);
  }

 private:
  IAllocator* allocator_ = Policy::get();
};

}  // namespace memory
}  // namespace tl_policies
}  // namespace td
