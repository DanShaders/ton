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
#include "vm/cells/Cell.h"
#include "td/utils/buffer.h"

#include "td/utils/HashMap.h"
#include "td/utils/HashSet.h"

#include <utility>
#include <functional>
#include <thread>

namespace vm {

class MerkleProof {
 public:
  using IsPrunnedFunction = std::function<bool(const Ref<Cell> &)>;

  // works with proofs wrapped in MerkleProof special cell
  // cells must have zero level
  static Ref<Cell> generate(Ref<Cell> cell, IsPrunnedFunction is_prunned);
  static Ref<Cell> generate(Ref<Cell> cell, CellUsageTree *usage_tree);

  // cell must have zero level and must be a MerkleProof
  static Ref<Cell> virtualize(Ref<Cell> cell, int virtualization);

  static Ref<Cell> combine(Ref<Cell> a, Ref<Cell> b);
  static td::Result<Ref<Cell>> combine_status(Ref<Cell> a, Ref<Cell> b);
  static Ref<Cell> combine_fast(Ref<Cell> a, Ref<Cell> b);
  static td::Result<Ref<Cell>> combine_fast_status(Ref<Cell> a, Ref<Cell> b);

  // works with upwrapped proofs
  // works fine with cell of non-zero level, but this is not supported (yet?) in MerkeProof special cell
  static Ref<Cell> generate_raw(Ref<Cell> cell, IsPrunnedFunction is_prunned);
  static Ref<Cell> generate_raw(Ref<Cell> cell, CellUsageTree *usage_tree);
  static Ref<Cell> virtualize_raw(Ref<Cell> cell, Cell::VirtualizationParameters virt);
  static Ref<Cell> combine_raw(Ref<Cell> a, Ref<Cell> b);
  static Ref<Cell> combine_fast_raw(Ref<Cell> a, Ref<Cell> b);
};

namespace detail {



class ContestValidateQuery;

class alignas(128) thread_pool_out_data
{
public:
  //Ref<Cell> out_data_cell;
  
  std::atomic<int> out_data_flag = { 0 };
  Semaphore out_data_sema;
};


//64 is based on x86's hardware destructive interference size, should also be 128 for arm
class alignas(128) push_thread_pool_data
{
public:
  
  //we know we will do dfs, just store data
  //ContestValidateQuery* in_data_context_obj = 0;
  //const StdSmcAddress& in_data_acc_addr;
  //Ref<vm::CellSlice> in_data_acc_blk_root = 0;
  
  thread_pool_out_data* out_data_memptr = 0;
  std::function<void()> in_data_exec_lambda;
  
  std::atomic<int> flag = { 0 };
  Semaphore sema;
};

class push_thread_pool
{
public:
  static push_thread_pool_data task_pushed_data[8];
  
  static std::atomic<int> thread_ready_bitset; //we use this for search from pusher's perspective and we use separated flag values for false sharing prevention
  
  //find first bit set
  //set bit through | 1 << bit_num_from_zero
  //check bit through __builtin_ctz(bitset), if bitset is 0 it would be ub
  
  static std::thread worker_threads[8];
  
  static uint32_t try_find_and_set_thread_work(std::function<void()> in_data_exec_lambda, thread_pool_out_data* out_data_arr);
  static void tpool_thread_main(uint32_t thread_idx);
  
  static void tpool_init();
  static void tpool_prepare(); //for reuse
  
  static void tpool_thread_wait_for_out_data(thread_pool_out_data* out_data_ptr);
  static void tpool_wait_for_all_threads(thread_pool_out_data* out_data, uint8_t* thread_was_used);
};

class MerkleProofImpl {
public:
  explicit MerkleProofImpl(MerkleProof::IsPrunnedFunction is_prunned);
  explicit MerkleProofImpl(CellUsageTree *usage_tree);
  
  Ref<Cell> create_from(Ref<Cell> cell);
  
  using Key = std::pair<Cell::Hash, int>;
  td::HashMap<Key, Ref<Cell>> cells_;
  td::HashSet<Cell::Hash> visited_cells_;
  CellUsageTree *usage_tree_{nullptr};
  MerkleProof::IsPrunnedFunction is_prunned_;
  
  std::mutex cells_hm_mutex;
  std::mutex visited_cells_mutex;
  std::mutex usage_tree_mutex;
  
  void dfs_usage_tree(Ref<Cell> cell, CellUsageTree::NodeId node_id);
  
  Ref<Cell> dfs(Ref<Cell> cell, int merkle_depth);
};
}

class MerkleProofBuilder {
  std::shared_ptr<CellUsageTree> usage_tree;
  Ref<vm::Cell> orig_root, usage_root;

 public:
  MerkleProofBuilder() = default;
  MerkleProofBuilder(Ref<Cell> root);
  Ref<Cell> init(Ref<Cell> root);
  bool clear();
  Ref<Cell> root() const {
    return usage_root;
  }
  td::Result<Ref<Cell>> extract_proof() const;
  bool extract_proof_to(Ref<Cell> &proof_root) const;
  td::Result<td::BufferSlice> extract_proof_boc() const;

  void set_cell_load_callback(std::function<void(const td::Ref<vm::DataCell>&)> f) {
    usage_tree->set_cell_load_callback(std::move(f));
  }
};

}  // namespace vm
