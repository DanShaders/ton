
#pragma once

/// \file Thread Local Policy: memory.

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

inline IAllocator* get_default_allocator() {
  static DefaultAllocator obj;
  return &obj;
}

/// \brief Sets a localized memory allocation and deallocation policy.
///   It can be used to change the allocation mechanism for a specific
///   place in the code without affecting the behavior elsewhere.
class Policy {
 private:
  Policy() = default;

  static IAllocator*& instance() {
    static TD_THREAD_LOCAL IAllocator* obj = nullptr;
    if (obj == nullptr) {
      obj = get_default_allocator();
    }
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
