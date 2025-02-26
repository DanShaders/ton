#pragma once

#include <mutex>
#include <utility>

namespace ton {

template<typename T>
class WrappingMutexLock {
  T& content_;
  std::unique_lock<std::mutex> lock_;

public:
  WrappingMutexLock(T& content, std::unique_lock<std::mutex>&& lock)
    : content_(content)
    , lock_(std::move(lock)) {}

  T* operator->() {
    return &content_;
  }

  const T* operator->() const {
    return &content_;
  }

  T& operator*() {
    return content_;
  }

  const T& operator*() const {
    return content_;
  }
};

template<typename T>
class WrappingMutex {
  T& content_;
  mutable std::mutex mutex_;

public:
  explicit WrappingMutex(T& content)
    : content_(content) {}

  WrappingMutexLock<T> lock() {
    return WrappingMutexLock<T>(content_, std::unique_lock<std::mutex>(mutex_));
  }

  WrappingMutexLock<const T> lock() const {
    return WrappingMutexLock<const T>(content_, std::unique_lock<std::mutex>(mutex_));
  }
};

template<typename T>
class WrappingWeakMutexLock {
  T* content_;
  std::unique_lock<std::mutex> lock_;

public:
  WrappingWeakMutexLock(T* content, std::unique_lock<std::mutex>&& lock)
    : content_(content)
    , lock_(std::move(lock)) {}

  T* operator->() {
    return content_;
  }

  const T* operator->() const {
    return content_;
  }

  T* get() {
    return content_;
  }

  const T* get() const {
    return content_;
  }

  explicit operator bool() const {
    return content_ != nullptr;
  }
};

template<typename T>
class WrappingWeakMutex {
  T* content_;
  mutable std::mutex mutex_;

public:
  explicit WrappingWeakMutex(T* content = nullptr)
    : content_(content) {}

  void reset(T* content = nullptr) {
    auto guard = std::unique_lock<std::mutex>(mutex_);
    content_ = content;
  }

  WrappingWeakMutexLock<T> lock() {
    return WrappingWeakMutexLock<T>(content_, std::unique_lock<std::mutex>(mutex_));
  }

  WrappingWeakMutexLock<const T> lock() const {
    return WrappingWeakMutexLock<const T>(content_, std::unique_lock<std::mutex>(mutex_));
  }
};

} 