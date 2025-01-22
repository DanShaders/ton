
#pragma once

namespace td {
namespace tl_policies {

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

struct MultiLineAllocator : public IAllocator {
  explicit MultiLineAllocator(std::size_t _size) : size(_size) {
    cur = ptr = static_cast<std::uint8_t*>(malloc(size));
    if (!ptr) {
      throw std::bad_alloc();
    }
    end = ptr + size;
  }

  ~MultiLineAllocator() override {
    free(ptr);
  }

  void* allocate(std::size_t count) override {
    std::uint8_t* t = cur;
    cur += (count + 7) & -8;
    if (cur > end) {
      throw std::bad_alloc();
    }
    return (void*)t;
  }
  void deallocate(const void* ptr) override {
  }

 private:
  std::uint8_t *ptr, *cur, *end;
  std::size_t size;
};

/// \brief Аллокатор для локального применения. При помощи его можно изменить механизм аллокации для конкретного
///   места в коде, не влияя на поведение в других местах.
class PolicyAllocation {
 private:
  PolicyAllocation() = default;

  static IAllocator*& instance() {
    static TD_THREAD_LOCAL IAllocator* obj =
        new MultiLineAllocator{2 * 1024 * 1024ll * 1024ll};  //new DefaultAllocator;
    return obj;
  }

 public:
  static IAllocator* get() {
    return instance();
  }

  static void set(IAllocator* ptr) {
    if (instance() != nullptr) {
      delete instance();
    }
    instance() = ptr;
  }
};

template <typename T, typename... Args>
T* allocate(Args&&... args) {
  void* ptr = PolicyAllocation::get()->allocate(sizeof(T));
  return new (ptr) T(std::forward<Args>(args)...);
}

template <typename T>
void deallocate(const T* ptr) {
  ptr->~T();
  PolicyAllocation::get()->deallocate(static_cast<const void*>(ptr));
}

}  // namespace tl_policies
}  // namespace td
