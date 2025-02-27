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

#include "vm/cells/CellTraits.h"

#include "td/utils/int_types.h"
#include "td/utils/logging.h"
#include <functional>




namespace vm {


#include <cassert>
#include <atomic>
//#include <random>
#include "trbitfield.h"

//preshing's semaphore

#if defined(_WIN32)
//---------------------------------------------------------
// Semaphore (Windows)
//---------------------------------------------------------

#include <windows.h>
#undef min
#undef max

class Semaphore
{
private:
    HANDLE m_hSema;

    Semaphore(const Semaphore& other) = delete;
    Semaphore& operator=(const Semaphore& other) = delete;

public:
    Semaphore(int initialCount = 0)
    {
        assert(initialCount >= 0);
        m_hSema = CreateSemaphore(NULL, initialCount, MAXLONG, NULL);
    }

    ~Semaphore()
    {
        CloseHandle(m_hSema);
    }

    void wait()
    {
        WaitForSingleObject(m_hSema, INFINITE);
    }

    void signal(int count = 1)
    {
        ReleaseSemaphore(m_hSema, count, NULL);
    }
};


#elif defined(__MACH__)
//---------------------------------------------------------
// Semaphore (Apple iOS and OSX)
// Can't use POSIX semaphores due to http://lists.apple.com/archives/darwin-kernel/2009/Apr/msg00010.html
//---------------------------------------------------------

#include <mach/mach.h>

class Semaphore
{
private:
    semaphore_t m_sema;

    Semaphore(const Semaphore& other) = delete;
    Semaphore& operator=(const Semaphore& other) = delete;

public:
    Semaphore(int initialCount = 0)
    {
        assert(initialCount >= 0);
        semaphore_create(mach_task_self(), &m_sema, SYNC_POLICY_FIFO, initialCount);
    }

    ~Semaphore()
    {
        semaphore_destroy(mach_task_self(), m_sema);
    }

    void wait()
    {
        semaphore_wait(m_sema);
    }

    void signal()
    {
        semaphore_signal(m_sema);
    }

    void signal(int count)
    {
        while (count-- > 0)
        {
            semaphore_signal(m_sema);
        }
    }
};


#elif defined(__unix__)
//---------------------------------------------------------
// Semaphore (POSIX, Linux)
//---------------------------------------------------------

#include <semaphore.h>

class Semaphore
{
private:
    sem_t m_sema;

    Semaphore(const Semaphore& other) = delete;
    Semaphore& operator=(const Semaphore& other) = delete;

public:
    Semaphore(int initialCount = 0)
    {
        assert(initialCount >= 0);
        sem_init(&m_sema, 0, initialCount);
    }

    ~Semaphore()
    {
        sem_destroy(&m_sema);
    }

    void wait()
    {
        // http://stackoverflow.com/questions/2013181/gdb-causes-sem-wait-to-fail-with-eintr-error
        int rc;
        do
        {
            rc = sem_wait(&m_sema);
        }
        while (rc == -1 && errno == EINTR);
    }

    void signal()
    {
        sem_post(&m_sema);
    }

    void signal(int count)
    {
        while (count-- > 0)
        {
            sem_post(&m_sema);
        }
    }
};


#else

#error Unsupported platform!

#endif


//---------------------------------------------------------
// LightweightSemaphore
//---------------------------------------------------------
class LightweightSemaphore
{
private:
    std::atomic<int> m_count;
    Semaphore m_sema;

    void waitWithPartialSpinning()
    {
        int oldCount;
        // Is there a better way to set the initial spin count?
        // If we lower it to 1000, testBenaphore becomes 15x slower on my Core i7-5930K Windows PC,
        // as threads start hitting the kernel semaphore.
        int spin = 10000;
        while (spin--)
        {
            oldCount = m_count.load(std::memory_order_relaxed);
            if ((oldCount > 0) && m_count.compare_exchange_strong(oldCount, oldCount - 1, std::memory_order_acquire))
                return;
            std::atomic_signal_fence(std::memory_order_acquire);     // Prevent the compiler from collapsing the loop.
        }
        oldCount = m_count.fetch_sub(1, std::memory_order_acquire);
        if (oldCount <= 0)
        {
            m_sema.wait();
        }
    }

public:
    LightweightSemaphore(int initialCount = 0) : m_count(initialCount)
    {
        assert(initialCount >= 0);
    }

    bool tryWait()
    {
        int oldCount = m_count.load(std::memory_order_relaxed);
        return (oldCount > 0 && m_count.compare_exchange_strong(oldCount, oldCount - 1, std::memory_order_acquire));
    }

    void wait()
    {
        if (!tryWait())
            waitWithPartialSpinning();
    }

    void signal(int count = 1)
    {
        int oldCount = m_count.fetch_add(count, std::memory_order_release);
        int toRelease = -oldCount < count ? -oldCount : count;
        if (toRelease > 0)
        {
            m_sema.signal(toRelease);
        }
    }
};


typedef LightweightSemaphore DefaultSemaphoreType;

//---------------------------------------------------------
// NonRecursiveRWLock
//---------------------------------------------------------
class NonRecursiveRWLock
{
private:
    BEGIN_BITFIELD_TYPE(Status, uint32_t)
        ADD_BITFIELD_MEMBER(readers, 0, 10)
        ADD_BITFIELD_MEMBER(waitToRead, 10, 10)
        ADD_BITFIELD_MEMBER(writers, 20, 10)
    END_BITFIELD_TYPE()

    std::atomic<uint32_t> m_status;
    DefaultSemaphoreType m_readSema;
    DefaultSemaphoreType m_writeSema;

public:
    NonRecursiveRWLock() : m_status(0) {}
    
    void lockReader()
    {
        Status oldStatus = m_status.load(std::memory_order_relaxed);
        Status newStatus;
        do
        {
            newStatus = oldStatus;
            if (oldStatus.writers > 0)
            {
                newStatus.waitToRead++;
            }
            else
            {
                newStatus.readers++;
            }
            // CAS until successful. On failure, oldStatus will be updated with the latest value.
        }
        while (!m_status.compare_exchange_weak(oldStatus, newStatus,
                                               std::memory_order_acquire, std::memory_order_relaxed));

        if (oldStatus.writers > 0)
        {
            m_readSema.wait();
        }
    }

    void unlockReader()
    {
        Status oldStatus = m_status.fetch_sub(Status().readers.one(), std::memory_order_release);
        assert(oldStatus.readers > 0);
        if (oldStatus.readers == 1 && oldStatus.writers > 0)
        {
            m_writeSema.signal();
        }
    }

    void lockWriter()
    {
        Status oldStatus = m_status.fetch_add(Status().writers.one(), std::memory_order_acquire);
        assert(oldStatus.writers + 1 <= Status().writers.maximum());
        if (oldStatus.readers > 0 || oldStatus.writers > 0)
        {
            m_writeSema.wait();
        }
    }

    void unlockWriter()
    {
        Status oldStatus = m_status.load(std::memory_order_relaxed);
        Status newStatus;
        uint32_t waitToRead = 0;
        do
        {
            assert(oldStatus.readers == 0);
            newStatus = oldStatus;
            newStatus.writers--;
            waitToRead = oldStatus.waitToRead;
            if (waitToRead > 0)
            {
                newStatus.waitToRead = 0;
                newStatus.readers = waitToRead;
            }
            // CAS until successful. On failure, oldStatus will be updated with the latest value.
        }
        while (!m_status.compare_exchange_weak(oldStatus, newStatus,
                                               std::memory_order_release, std::memory_order_relaxed));

        if (waitToRead > 0)
        {
            m_readSema.signal(waitToRead);
        }
        else if (oldStatus.writers > 1)
        {
            m_writeSema.signal();
        }
    }
};


//---------------------------------------------------------
// ReadLockGuard
//---------------------------------------------------------
template <class LockType>
class ReadLockGuard
{
private:
    LockType& m_lock;

public:
    ReadLockGuard(LockType& lock) : m_lock(lock)
    {
        m_lock.lockReader();
    }

    ~ReadLockGuard()
    {
        m_lock.unlockReader();
    }
};


//---------------------------------------------------------
// WriteLockGuard
//---------------------------------------------------------
template <class LockType>
class WriteLockGuard
{
private:
    LockType& m_lock;

public:
    WriteLockGuard(LockType& lock) : m_lock(lock)
    {
        m_lock.lockWriter();
    }

    ~WriteLockGuard()
    {
        m_lock.unlockWriter();
    }
};


class DataCell;

class CellUsageTree : public std::enable_shared_from_this<CellUsageTree> {
 public:
  using NodeId = td::uint32;

  struct NodePtr {
   public:
    NodePtr() = default;
    NodePtr(std::weak_ptr<CellUsageTree> tree_weak, NodeId node_id)
        : tree_weak_(std::move(tree_weak)), node_id_(node_id) {
    }
    bool empty() const {
      return node_id_ == 0 || tree_weak_.expired();
    }

    bool on_load(const td::Ref<vm::DataCell>& cell) const;
    NodePtr create_child(unsigned ref_id) const;
    bool mark_path(CellUsageTree* master_tree) const;
    bool is_from_tree(const CellUsageTree* master_tree) const;

   private:
    std::weak_ptr<CellUsageTree> tree_weak_;
    NodeId node_id_{0};
  };

  NodePtr root_ptr();
  NodeId root_id();
  bool is_loaded(NodeId node_id);
  bool has_mark(NodeId node_id);
  void set_mark(NodeId node_id, bool mark = true);
  void mark_path(NodeId node_id);
  NodeId get_parent(NodeId node_id);
  NodeId get_child(NodeId node_id, unsigned ref_id);
  void set_use_mark_for_is_loaded(bool use_mark = true);
  NodeId create_child(NodeId node_id, unsigned ref_id);

  void set_cell_load_callback(std::function<void(const td::Ref<vm::DataCell>&)> f) {
    cell_load_callback_ = std::move(f);
  }

 private:
  struct Node {
    bool is_loaded{false};
    bool has_mark{false};
    NodeId parent{0};
    std::array<td::uint32, CellTraits::max_refs> children{};
  };
  bool use_mark_{false};
  std::vector<Node> nodes_{2};
  std::function<void(const td::Ref<vm::DataCell>&)> cell_load_callback_;
  
  std::recursive_mutex nodes_mtx;

  void on_load(NodeId node_id, const td::Ref<vm::DataCell>& cell);
  NodeId create_node(NodeId parent);
};
}  // namespace vm
