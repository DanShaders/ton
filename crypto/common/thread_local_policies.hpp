
#pragma once

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
  explicit MultiPagedFixedBlockAllocator(std::size_t _size) : size(_size) {
    cur = ptr = static_cast<std::uint8_t*>(malloc(size));
    if (!ptr) {
      throw std::bad_alloc();
    }
    end = ptr + size;
  }

  ~MultiPagedFixedBlockAllocator() override {
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

inline IAllocator* get_default_allocator() {
  static TD_THREAD_LOCAL DefaultAllocator obj;
  return &obj;
}

inline IAllocator* get_multipaged_fixed_block_allocator() {
  static TD_THREAD_LOCAL MultiPagedFixedBlockAllocator obj{static_cast<uint64_t>(1.5 * 1024.0 * 1024.0 * 1024.0)};
  return &obj;
}

/// \brief Установка локализованной политики выделения и освобождения памяти.
///   При помощи ее можно изменить механизм аллокации для конкретного
///   места в коде, не влияя на поведение в других местах.
class Policy {
 private:
  Policy() = default;

  static IAllocator*& instance() {
    static TD_THREAD_LOCAL IAllocator* obj = get_multipaged_fixed_block_allocator(); // get_default_allocator();
    return obj;
  }

 public:
  static IAllocator* get() {
    return instance();
  }

  static void set(IAllocator* ptr) {
    /// \remark не удаляем предыдущий аллокатор, т.к. PolicyAllocation не должен им владеть
    ///   потоки могут завершиться в любой момент и выделенная память не должна от этого зависеть.
    instance() = ptr;
  }
};

inline void* allocate(std::size_t count) {
  return Policy::get()->allocate(count);
}

inline void deallocate(const void* ptr) {
  Policy::get()->deallocate(ptr);
}

}  // namespace memory
}  // namespace tl_policies
}  // namespace td
