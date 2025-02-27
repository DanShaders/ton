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
#include "vm/cells/MerkleProof.h"
#include "vm/cells/CellBuilder.h"
#include "vm/cells/CellSlice.h"
#include "vm/boc.h"

#include "td/utils/HashMap.h"
#include "td/utils/HashSet.h"
#include <mutex>
#include <queue>
#include <thread>

//#define PUSH_THREAD_POOL_DBG_ENABLED

class ContestValidateQuery;

namespace vm {
namespace detail {

push_thread_pool_data push_thread_pool::task_pushed_data[8];
std::atomic<int> push_thread_pool::thread_ready_bitset = { 0xFF }; //reset to all threads available
std::thread push_thread_pool::worker_threads[8];


uint32_t push_thread_pool::try_find_and_set_thread_work(std::function<void()> in_data_exec_lambda, thread_pool_out_data* out_data_arr)
{
  int old_bitset = thread_ready_bitset.load(std::memory_order_relaxed);
  
  int thread_idx = 0;
  for(;;) //bit search in a CAS loop
  {
    if(old_bitset == 0)
    {
      return 0xFF; //no ready threads
    }
    
    thread_idx = __builtin_ctz(old_bitset);
    int new_bitset = old_bitset & ~( 1 << thread_idx );
    
    //acq rel on success to make sure there is no reordering of data modification going after the cas
    if(thread_ready_bitset.compare_exchange_weak(old_bitset, new_bitset, std::memory_order_acq_rel, std::memory_order_relaxed))
    {
      break;
    }
    
    //compare exchange failed, likely because bitset has changed in the meantime, retry operation
  }
  
  //TODO: push data for dfs
  task_pushed_data[thread_idx].in_data_exec_lambda = in_data_exec_lambda;
  //task_pushed_data[thread_idx].in_data_cell = in_dat_cell;
  //task_pushed_data[thread_idx].in_data_merkle_depth = in_dat_merkle_depth;
  
  //it's assumed that we won't delete memory it points to until this function is done, which is reasonable
  task_pushed_data[thread_idx].out_data_memptr = out_data_arr + thread_idx;
  
  //now that we pushed data, set flag and notify thread
  
  //also reset out flag, store after acts as a barrier for it to not go under
  out_data_arr[thread_idx].out_data_flag.store(0, std::memory_order_relaxed);
  task_pushed_data[thread_idx].flag.store(1, std::memory_order_release); //release to make sure data is flushed, no reorder
  task_pushed_data[thread_idx].sema.signal();
  
#ifdef PUSH_THREAD_POOL_DBG_ENABLED
  printf("successfully pushed work, t_idx: %d\n", thread_idx);
#endif
  return thread_idx; //success
}


void push_thread_pool::tpool_thread_main(uint32_t thread_idx)
{
  int flag_expected = 1;
  
  while (true) {
    //TODO: potentially some spinning before wait to reduce switching
    
    flag_expected = 1;
    
    if(task_pushed_data[thread_idx].flag.compare_exchange_strong(flag_expected, 0, std::memory_order_acquire))
    {
      //flag was 1, execute func, because of acquire - release semantics we can be sure that in_data is visible here
      
      //ContestValidateQuery* context_obj = task_pushed_data[thread_idx].in_data_context_obj;
      //Ref<Cell> ret_cell = context_obj->dfs(task_pushed_data[thread_idx].in_data_cell, task_pushed_data[thread_idx].in_data_merkle_depth);
      
      std::function<void()> cur_work = task_pushed_data[thread_idx].in_data_exec_lambda;
      cur_work();
      
      thread_pool_out_data* out_data_ptr = task_pushed_data[thread_idx].out_data_memptr;
      //out_data_ptr->out_data_cell = ret_cell;
      out_data_ptr->out_data_flag.store(1, std::memory_order_release); //release to make sure data is flushed, no reorder from before to after
      out_data_ptr->out_data_sema.signal();
      
      //TODO: semaphore reinit?
    }
    else
    {
      //was 0, wait for signal
      thread_ready_bitset.fetch_or( 1 << thread_idx, std::memory_order_release);
      
      task_pushed_data[thread_idx].sema.wait();
    }
    
  }
}

void push_thread_pool::tpool_init()
{
#ifdef PUSH_THREAD_POOL_DBG_ENABLED
  printf("initializing push_thread_pool\n");
#endif
  thread_ready_bitset.store(0xFF, std::memory_order_relaxed);
  
  for(int idx = 0; idx < 8; ++idx)
  {
    std::thread worker_th(tpool_thread_main, idx);
    
    worker_threads[idx] = std::move(worker_th);
  }
}

void push_thread_pool::tpool_prepare()
{
#ifdef PUSH_THREAD_POOL_DBG_ENABLED
  printf("preparing push_thread_pool for reuse\n");
#endif
  thread_ready_bitset.store(0xFF, std::memory_order_relaxed);
  
  for(int idx = 0; idx < 8; ++idx)
  {
    //task_pushed_data[idx].in_data_context_obj = 0;
    //task_pushed_data[idx].in_data_merkle_depth = 0;
    //task_pushed_data[idx].out_data_memptr = 0;
    task_pushed_data[idx].flag.store(0, std::memory_order_relaxed);
  }
}


//Ref<Cell> out_data_cell;
//std::atomic<int> out_data_flag = { 0 };
//Semaphore out_data_sema;

void push_thread_pool::tpool_thread_wait_for_out_data(thread_pool_out_data* out_data_ptr)
{
  int flag_expected = 1;
  
  while (true) {
    //TODO: initial spin to reduce switches
    
    flag_expected = 1;
    
    if(out_data_ptr->out_data_flag.compare_exchange_strong(flag_expected, 0, std::memory_order_acquire))
    {
      //out data flag was 1, meaning that because of release->acquire sync we definitely got out_data that was written before flag release store
      
      return; //out_data_ptr->out_data_cell;
    }
    else
    {
      //printf("thread is not done..\n");
      out_data_ptr->out_data_sema.wait();
    }
    
  }
}

void push_thread_pool::tpool_wait_for_all_threads(thread_pool_out_data* out_data, uint8_t* thread_was_used)
{
  for(int idx = 0; idx < 8; ++idx)
  {
    if(thread_was_used[idx])
    {
      tpool_thread_wait_for_out_data(out_data + idx);
    }
  }
}




  MerkleProofImpl::MerkleProofImpl(MerkleProof::IsPrunnedFunction is_prunned) : is_prunned_(std::move(is_prunned)) {
    push_thread_pool::tpool_prepare();
  }
  MerkleProofImpl::MerkleProofImpl(CellUsageTree *usage_tree) : usage_tree_(usage_tree) {
    push_thread_pool::tpool_prepare();
  }

  Ref<Cell> MerkleProofImpl::create_from(Ref<Cell> cell) {
    if (!is_prunned_) {
      CHECK(usage_tree_);
      dfs_usage_tree(cell, usage_tree_->root_id());
      is_prunned_ = [this](const Ref<Cell> &cell) { return visited_cells_.count(cell->get_hash()) == 0; };
    }
    try {
      return dfs(cell, cell->get_level());
    } catch (CellBuilder::CellWriteError &) {
      return {};
    } catch (CellBuilder::CellCreateError &) {
      return {};
    }
  }


  void MerkleProofImpl::dfs_usage_tree(Ref<Cell> cell, CellUsageTree::NodeId node_id) {
    if (!usage_tree_->is_loaded(node_id)) {
      return;
    }
    visited_cells_.insert(cell->get_hash());
    CellSlice cs(NoVm(), cell);
    //printf("dfs_tree_refs: %d\n", cs.size_refs()); //2,3, havent seen more in logs
    for (unsigned i = 0; i < cs.size_refs(); i++) {
      dfs_usage_tree(cs.prefetch_ref(i), usage_tree_->get_child(node_id, i));
    }
  }

//ORIG DFS
Ref<Cell> MerkleProofImpl::dfs(Ref<Cell> cell, int merkle_depth) {
  CHECK(cell.not_null());
  Key key{cell->get_hash(), merkle_depth};
  {
    auto it = cells_.find(key);
    if (it != cells_.end()) {
      CHECK(it->second.not_null());
      return it->second;
    }
  }

  if (is_prunned_(cell)) {
    auto res = CellBuilder::create_pruned_branch(cell, merkle_depth + 1);
    CHECK(res.not_null());
    cells_.emplace(key, res);
    return res;
  }
  CellSlice cs(NoVm(), cell);
  int children_merkle_depth = cs.child_merkle_depth(merkle_depth);
  CellBuilder cb;
  cb.store_bits(cs.fetch_bits(cs.size()));
  for (unsigned i = 0; i < cs.size_refs(); i++) {
    cb.store_ref(dfs(cs.prefetch_ref(i), children_merkle_depth));
  }
  auto res = cb.finalize(cs.is_special());
  CHECK(res.not_null());
  cells_.emplace(key, res);
  return res;
}
//DFS END

/*
  Ref<Cell> MerkleProofImpl::dfs(Ref<Cell> cell, int merkle_depth) {
    CHECK(cell.not_null());
    Key key{cell->get_hash(), merkle_depth};
    {
      std::lock_guard<std::mutex> clock(cells_hm_mutex);
      auto it = cells_.find(key);
      if (it != cells_.end()) {
        CHECK(it->second.not_null());
        return it->second;
      }
    }

    if (is_prunned_(cell)) {
      auto res = CellBuilder::create_pruned_branch(cell, merkle_depth + 1);
      CHECK(res.not_null());
      {
        std::lock_guard<std::mutex> clock(cells_hm_mutex);
        cells_.emplace(key, res);
      }
      return res;
    }
    CellSlice cs(NoVm(), cell);
    int children_merkle_depth = cs.child_merkle_depth(merkle_depth);
    CellBuilder cb;
    cb.store_bits(cs.fetch_bits(cs.size()));
    
    //printf("dfs_refs: %d, children_merkle_depth: %d\n", cs.size_refs(), children_merkle_depth);
    //for (unsigned i = 0; i < cs.size_refs(); i++) {
      //for (unsigned i = 1; i < cs.size_refs(); i++) {
      //should be dispatched to other threads
      //after 0s dfs we put wait for signal from other threads (so after it finishes it waits for other threads)
      
      //we stop forking if we reached hardware concurrency, so we're not destroying performance with switches
      //cb.store_ref(dfs(cs.prefetch_ref(i), children_merkle_depth));
    //}
    
    if(cs.size_refs() > 0)
    {
      unsigned forked_cell_futures_len = cs.size_refs() - 1;
      
#ifdef PUSH_THREAD_POOL_DBG_ENABLED
      printf("creating %u forks\n", forked_cell_futures_len);
#endif
      thread_pool_out_data forked_cell_futures[forked_cell_futures_len];
      bool forked_cell_futures_success_bools[forked_cell_futures_len];
      
      for (unsigned i = 1; i < cs.size_refs(); i++) {
#ifdef PUSH_THREAD_POOL_DBG_ENABLED
        printf("dispatched %u\n", i - 1);
#endif
        
        forked_cell_futures_success_bools[i - 1] = push_thread_pool::try_find_and_set_thread_work(this, cs.prefetch_ref(i), children_merkle_depth, forked_cell_futures + (i - 1));
        //if failed to push into pool, no free threads
        //do it in this thread later
        
        //forked_cell_futures[i - 1] = tpool.submit(&MerkleProofImpl::dfs, this, cs.prefetch_ref(i), children_merkle_depth);
      }
      cb.store_ref(dfs(cs.prefetch_ref(0), children_merkle_depth));
      //when we get those tasks back, store refs
      for(unsigned ix = 0; ix < forked_cell_futures_len; ++ix)
      {
        bool has_work_been_delegated = forked_cell_futures_success_bools[ix];
        if(has_work_been_delegated)
        {
#ifdef PUSH_THREAD_POOL_DBG_ENABLED
          printf("waiting for result from out_data_memptr[%d]..\n", ix);
#endif
          cb.store_ref(push_thread_pool::tpool_thread_wait_for_out_data(forked_cell_futures + ix));
#ifdef PUSH_THREAD_POOL_DBG_ENABLED
          printf("got result from out_data_memptr %u\n", ix);
#endif
        }
        else
        {
          cb.store_ref(dfs(cs.prefetch_ref(ix + 1), children_merkle_depth));
#ifdef PUSH_THREAD_POOL_DBG_ENABLED
          printf("calculated result in current thread, %u\n", ix);
#endif
        }
      }
    }
    
    auto res = cb.finalize(cs.is_special());
    CHECK(res.not_null());
    
    {
      std::lock_guard<std::mutex> clock(cells_hm_mutex);
      cells_.emplace(key, res);
    }
    return res;
  }*/
}  // namespace detail

Ref<Cell> MerkleProof::generate_raw(Ref<Cell> cell, IsPrunnedFunction is_prunned) {
  return detail::MerkleProofImpl(is_prunned).create_from(cell);
}

Ref<Cell> MerkleProof::generate_raw(Ref<Cell> cell, CellUsageTree *usage_tree) {
  return detail::MerkleProofImpl(usage_tree).create_from(cell);
}

Ref<Cell> MerkleProof::virtualize_raw(Ref<Cell> cell, Cell::VirtualizationParameters virt) {
  return cell->virtualize(virt);
}

Ref<Cell> MerkleProof::generate(Ref<Cell> cell, IsPrunnedFunction is_prunned) {
  int cell_level = cell->get_level();
  if (cell_level != 0) {
    return {};
  }
  auto raw = generate_raw(std::move(cell), is_prunned);
  return CellBuilder::create_merkle_proof(std::move(raw));
}

Ref<Cell> MerkleProof::generate(Ref<Cell> cell, CellUsageTree *usage_tree) {
  int cell_level = cell->get_level();
  if (cell_level != 0) {
    return {};
  }
  auto raw = generate_raw(std::move(cell), usage_tree);
  if (raw.is_null()) {
    return {};
  }
  return CellBuilder::create_merkle_proof(std::move(raw));
}

td::Result<Ref<Cell>> unpack_proof(Ref<Cell> cell) {
  CHECK(cell.not_null());
  td::uint8 level = static_cast<td::uint8>(cell->get_level());
  if (level != 0) {
    return td::Status::Error("Level of MerkleProof must be zero");
  }
  CellSlice cs(NoVm(), std::move(cell));
  if (cs.special_type() != Cell::SpecialType::MerkleProof) {
    return td::Status::Error("Not a MekleProof cell");
  }
  return cs.fetch_ref();
}

Ref<Cell> MerkleProof::virtualize(Ref<Cell> cell, int virtualization) {
  auto r_raw = unpack_proof(std::move(cell));
  if (r_raw.is_error()) {
    return {};
  }
  return virtualize_raw(r_raw.move_as_ok(), {0 /*level*/, static_cast<td::uint8>(virtualization)});
}

class MerkleProofCombineFast {
 public:
  MerkleProofCombineFast(Ref<Cell> a, Ref<Cell> b) : a_(std::move(a)), b_(std::move(b)) {
  }
  td::Result<Ref<Cell>> run() {
    if (a_.is_null()) {
      return b_;
    } else if (b_.is_null()) {
      return a_;
    }
    TRY_RESULT_ASSIGN(a_, unpack_proof(a_));
    TRY_RESULT_ASSIGN(b_, unpack_proof(b_));
    TRY_RESULT(res, run_raw());
    return CellBuilder::create_merkle_proof(std::move(res));
  }

  td::Result<Ref<Cell>> run_raw() {
    if (a_->get_hash(0) != b_->get_hash(0)) {
      return td::Status::Error("Can't combine MerkleProofs with different roots");
    }
    return merge(a_, b_, 0);
  }

 private:
  Ref<Cell> a_;
  Ref<Cell> b_;

  Ref<Cell> merge(Ref<Cell> a, Ref<Cell> b, td::uint32 merkle_depth) {
    if (a->get_hash() == b->get_hash()) {
      return a;
    }
    if (a->get_level() == merkle_depth) {
      return a;
    }
    if (b->get_level() == merkle_depth) {
      return b;
    }

    CellSlice csa(NoVm(), a);
    CellSlice csb(NoVm(), b);

    if (csa.is_special() && csa.special_type() == vm::Cell::SpecialType::PrunnedBranch) {
      return b;
    }
    if (csb.is_special() && csb.special_type() == vm::Cell::SpecialType::PrunnedBranch) {
      return a;
    }

    CHECK(csa.size_refs() != 0);

    auto child_merkle_depth = csa.child_merkle_depth(merkle_depth);

    CellBuilder cb;
    cb.store_bits(csa.fetch_bits(csa.size()));
    for (unsigned i = 0; i < csa.size_refs(); i++) {
      cb.store_ref(merge(csa.prefetch_ref(i), csb.prefetch_ref(i), child_merkle_depth));
    }
    return cb.finalize(csa.is_special());
  }
};

class MerkleProofCombine {
 public:
  MerkleProofCombine(Ref<Cell> a, Ref<Cell> b) : a_(std::move(a)), b_(std::move(b)) {
  }
  td::Result<Ref<Cell>> run() {
    if (a_.is_null()) {
      return b_;
    } else if (b_.is_null()) {
      return a_;
    }
    TRY_RESULT_ASSIGN(a_, unpack_proof(a_));
    TRY_RESULT_ASSIGN(b_, unpack_proof(b_));
    TRY_RESULT(res, run_raw());
    return CellBuilder::create_merkle_proof(std::move(res));
  }

  td::Result<Ref<Cell>> run_raw() {
    if (a_->get_hash(0) != b_->get_hash(0)) {
      return td::Status::Error("Can't combine MerkleProofs with different roots");
    }
    dfs(a_, 0);
    dfs(b_, 0);
    return create_A(a_, 0, 0);
  }

 private:
  Ref<Cell> a_;
  Ref<Cell> b_;

  struct Info {
    Ref<Cell> cell_;
    Ref<Cell> prunned_cells_[Cell::max_level];  // Cache prunned cells with different levels to reuse them

    Ref<Cell> get_prunned_cell(int depth) {
      if (depth < Cell::max_level) {
        return prunned_cells_[depth];
      }
      return {};
    }
    Ref<Cell> get_any_cell() const {
      if (cell_.not_null()) {
        return cell_;
      }
      for (auto &cell : prunned_cells_) {
        if (cell.not_null()) {
          return cell;
        }
      }
      UNREACHABLE();
    }
  };

  using Key = std::pair<Cell::Hash, int>;
  td::HashMap<Cell::Hash, Info> cells_;
  td::HashMap<Key, Ref<Cell>> create_A_res_;
  td::HashSet<Key> visited_;

  void dfs(Ref<Cell> cell, int merkle_depth) {
    if (!visited_.emplace(cell->get_hash(), merkle_depth).second) {
      return;
    }

    auto &info = cells_[cell->get_hash(merkle_depth)];
    CellSlice cs(NoVm(), cell);
    // check if prunned cell is bounded
    if (cs.special_type() == Cell::SpecialType::PrunnedBranch && static_cast<int>(cell->get_level()) > merkle_depth) {
      info.prunned_cells_[cell->get_level() - 1] = std::move(cell);
      return;
    }
    info.cell_ = std::move(cell);

    auto child_merkle_depth = cs.child_merkle_depth(merkle_depth);
    for (size_t i = 0, size = cs.size_refs(); i < size; i++) {
      dfs(cs.fetch_ref(), child_merkle_depth);
    }
  }

  Ref<Cell> create_A(Ref<Cell> cell, int merkle_depth, int a_merkle_depth) {
    merkle_depth = cell->get_level_mask().apply(merkle_depth).get_level();
    auto key = Key(cell->get_hash(merkle_depth), a_merkle_depth);
    auto it = create_A_res_.find(key);
    if (it != create_A_res_.end()) {
      return it->second;
    }

    auto res = do_create_A(std::move(cell), merkle_depth, a_merkle_depth);
    create_A_res_.emplace(key, res);
    return res;
  }

  Ref<Cell> do_create_A(Ref<Cell> cell, int merkle_depth, int a_merkle_depth) {
    auto &info = cells_[cell->get_hash(merkle_depth)];

    if (info.cell_.is_null()) {
      Ref<Cell> res = info.get_prunned_cell(a_merkle_depth);
      if (res.is_null()) {
        res = CellBuilder::create_pruned_branch(info.get_any_cell(), a_merkle_depth + 1, merkle_depth);
      }
      return res;
    }

    CHECK(info.cell_.not_null());
    CellSlice cs(NoVm(), info.cell_);

    //CHECK(cs.size_refs() != 0);
    if (cs.size_refs() == 0) {
      return info.cell_;
    }

    auto child_merkle_depth = cs.child_merkle_depth(merkle_depth);
    auto child_a_merkle_depth = cs.child_merkle_depth(a_merkle_depth);

    CellBuilder cb;
    cb.store_bits(cs.fetch_bits(cs.size()));
    for (unsigned i = 0; i < cs.size_refs(); i++) {
      cb.store_ref(create_A(cs.prefetch_ref(i), child_merkle_depth, child_a_merkle_depth));
    }
    return cb.finalize(cs.is_special());
  }
};

Ref<Cell> MerkleProof::combine(Ref<Cell> a, Ref<Cell> b) {
  auto res = MerkleProofCombine(std::move(a), std::move(b)).run();
  if (res.is_error()) {
    return {};
  }
  return res.move_as_ok();
}

td::Result<Ref<Cell>> MerkleProof::combine_status(Ref<Cell> a, Ref<Cell> b) {
  return MerkleProofCombine(std::move(a), std::move(b)).run();
}

Ref<Cell> MerkleProof::combine_fast(Ref<Cell> a, Ref<Cell> b) {
  auto res = MerkleProofCombineFast(std::move(a), std::move(b)).run();
  if (res.is_error()) {
    return {};
  }
  return res.move_as_ok();
}

td::Result<Ref<Cell>> MerkleProof::combine_fast_status(Ref<Cell> a, Ref<Cell> b) {
  return MerkleProofCombineFast(std::move(a), std::move(b)).run();
}

Ref<Cell> MerkleProof::combine_raw(Ref<Cell> a, Ref<Cell> b) {
  auto res = MerkleProofCombine(std::move(a), std::move(b)).run_raw();
  if (res.is_error()) {
    return {};
  }
  return res.move_as_ok();
}

Ref<Cell> MerkleProof::combine_fast_raw(Ref<Cell> a, Ref<Cell> b) {
  auto res = MerkleProofCombineFast(std::move(a), std::move(b)).run_raw();
  if (res.is_error()) {
    return {};
  }
  return res.move_as_ok();
}

MerkleProofBuilder::MerkleProofBuilder(Ref<Cell> root)
    : usage_tree(std::make_shared<CellUsageTree>()), orig_root(std::move(root)) {
  usage_root = UsageCell::create(orig_root, usage_tree->root_ptr());
}

Ref<Cell> MerkleProofBuilder::init(Ref<Cell> root) {
  usage_tree = std::make_shared<CellUsageTree>();
  orig_root = std::move(root);
  usage_root = UsageCell::create(orig_root, usage_tree->root_ptr());
  return usage_root;
}

bool MerkleProofBuilder::clear() {
  usage_tree.reset();
  orig_root.clear();
  usage_root.clear();
  return true;
}

td::Result<Ref<Cell>> MerkleProofBuilder::extract_proof() const {
  Ref<Cell> proof = MerkleProof::generate(orig_root, usage_tree.get());
  if (proof.is_null()) {
    return td::Status::Error("cannot create Merkle proof");
  }
  return proof;
}

bool MerkleProofBuilder::extract_proof_to(Ref<Cell> &proof_root) const {
  if (orig_root.is_null()) {
    return false;
  }
  auto R = extract_proof();
  if (R.is_error()) {
    return false;
  }
  proof_root = R.move_as_ok();
  return true;
}

td::Result<td::BufferSlice> MerkleProofBuilder::extract_proof_boc() const {
  TRY_RESULT(proof_root, extract_proof());
  return std_boc_serialize(std::move(proof_root));
}

}  // namespace vm
