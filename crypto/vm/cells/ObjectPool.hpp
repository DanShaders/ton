#pragma once
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>

template <typename T, size_t N>
class GlobalObjectPool;

template <typename T, size_t N>
class LocalObjectPool {
public:
  LocalObjectPool() = default;

  T *acquire();
  void retire(T *);

  ~LocalObjectPool();

  friend class GlobalObjectPool<T, N>;

  T *pool_[N];
  size_t size_{0};
  // size_t all_{0};
};

template <typename T, size_t N>
class LocalObjectPoolGuard;

template <typename T, size_t N>
class GlobalObjectPool {
public:
  GlobalObjectPool() = delete;

  static std::unique_ptr<LocalObjectPool<T, N>> obtain_local();
  static void retire_local(std::unique_ptr<LocalObjectPool<T, N>> pool);

private:
  static std::mutex mutex_;
  static std::deque<std::unique_ptr<LocalObjectPool<T, N>>> available_;
};

template<typename T, size_t N>
std::mutex GlobalObjectPool<T, N>::mutex_ {};

template<typename T, size_t N>
std::deque<std::unique_ptr<LocalObjectPool<T, N>>> GlobalObjectPool<T, N>::available_ {};

template <typename T, size_t N>
class LocalObjectPoolGuard {
public:
  explicit LocalObjectPoolGuard(std::unique_ptr<LocalObjectPool<T, N>> pool) : pool_(std::move(pool)) {}
  ~LocalObjectPoolGuard();

  LocalObjectPool<T, N> *operator->() {
    return pool_.get();
  }

private:
  std::unique_ptr<LocalObjectPool<T, N>> pool_;
};

template <typename T, size_t N>
T *LocalObjectPool<T, N>::acquire() {
  if (size_ == 0) {
    // all_++;
    return static_cast<T*>(malloc(sizeof(T)));
  }
  T *ptr = pool_[--size_];
  return ptr;
}

template <typename T, size_t N>
void LocalObjectPool<T, N>::retire(T *ptr) {
  if (size_ == N) {
    return free(ptr);
  }
  pool_[size_++] = ptr;
}

template <typename T, size_t N>
LocalObjectPool<T, N>::~LocalObjectPool() {
  for (size_t pos = 0; pos < size_; pos++) {
    free(pool_[pos]);
  }
  // static std::mutex dbg_mutex;
  // std::lock_guard lock(dbg_mutex);
  // std::cerr << "stats: " << all_ << std::endl;
}

template <typename T, size_t N>
std::unique_ptr<LocalObjectPool<T, N>> GlobalObjectPool<T, N>::obtain_local() {
  std::lock_guard lock(mutex_);
  if (available_.empty()) {
    return std::make_unique<LocalObjectPool<T, N>>();
  }
  std::unique_ptr<LocalObjectPool<T, N>> pool = std::move(available_.back());
  available_.pop_back();
  return pool;
}
template <typename T, size_t N>
void GlobalObjectPool<T, N>::retire_local(std::unique_ptr<LocalObjectPool<T, N>> pool) {
  std::lock_guard lock(mutex_);
  available_.push_back(std::move(pool));
}

template <typename T, size_t N>
LocalObjectPoolGuard<T, N>::~LocalObjectPoolGuard() {
  GlobalObjectPool<T, N>::retire_local(std::move(pool_));
}

