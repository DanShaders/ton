#include "vm/cells/Cell.h"
#include "vm/cells/CellBuilder.h"
#include "vm/cells/CellSlice.h"
#include "td/utils/buffer.h"
#include "vm/boc.h"

#include "td/utils/HashMap.h"
#include "td/utils/HashSet.h"

#include "merkle.hpp"


namespace solution {


class MyMerkleProofImpl {
 public:
  explicit MyMerkleProofImpl(BS::thread_pool<>& pool, MyMerkleProof::IsPrunnedFunction is_prunned) :
    pool(pool), is_prunned_(std::move(is_prunned)) {
  }
  explicit MyMerkleProofImpl(BS::thread_pool<>& pool, CellUsageTree *usage_tree) :
    pool(pool), usage_tree_(usage_tree) {
  }

  Ref<Cell> create_from(Ref<Cell> cell) {
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

 private:
  BS::thread_pool<>& pool;

  using Key = std::pair<Cell::Hash, int>;
  td::HashMap<Key, Ref<Cell>> cells_;
  td::HashSet<Cell::Hash> visited_cells_;
  CellUsageTree *usage_tree_{nullptr};
  MyMerkleProof::IsPrunnedFunction is_prunned_;

  void dfs_usage_tree(Ref<Cell> cell, CellUsageTree::NodeId node_id) {
    if (!usage_tree_->is_loaded(node_id)) {
      return;
    }
    visited_cells_.insert(cell->get_hash());
    CellSlice cs(vm::NoVm(), cell);
    for (unsigned i = 0; i < cs.size_refs(); i++) {
      dfs_usage_tree(cs.prefetch_ref(i), usage_tree_->get_child(node_id, i));
    }
  }

  Ref<Cell> dfs(Ref<Cell> cell, int merkle_depth) {
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
    CellSlice cs(vm::NoVm(), cell);
    int children_merkle_depth = cs.child_merkle_depth(merkle_depth);

    CellBuilder cb;

    // Original:
    cb.store_bits(cs.fetch_bits(cs.size()));
    for (unsigned i = 0; i < cs.size_refs(); i++) {
      cb.store_ref(dfs(cs.prefetch_ref(i), children_merkle_depth));
    }

    // Threaded:
    // vector<future<Ref<Cell>>> futures;
    // for (unsigned i = 1; i < cs.size_refs(); i++) {
    //   futures.push_back(async(
    //     [this, &cs, i, children_merkle_depth] {
    //       return dfs(cs.prefetch_ref(i), children_merkle_depth);
    //     }
    //   ));
    // }

    // cb.store_bits(cs.fetch_bits(cs.size()));
    // if (cs.have_refs()) {
    //   cb.store_ref(dfs(cs.prefetch_ref(0), children_merkle_depth));
    // }
    // for (auto& f : futures) {
    //   cb.store_ref(f.get());
    // }

    auto res = cb.finalize(cs.is_special());
    CHECK(res.not_null());
    cells_.emplace(key, res);
    return res;
  }
};



Ref<Cell> MyMerkleUpdate::generate(BS::thread_pool<>& pool, Ref<Cell> from, Ref<Cell> to, CellUsageTree *usage_tree) {
  auto from_level = from->get_level();
  auto to_level = to->get_level();
  if (from_level != 0 || to_level != 0) {
    return {};
  }
  auto res = generate_raw(pool, std::move(from), std::move(to), usage_tree);
  if (res.first.is_null() || res.second.is_null()) {
    return {};
  }
  return CellBuilder::create_merkle_update(res.first, res.second);
}



std::pair<Ref<Cell>, Ref<Cell>> MyMerkleUpdate::generate_raw(BS::thread_pool<>& pool, Ref<Cell> from, Ref<Cell> to, CellUsageTree *usage_tree) {
  // create Merkle update cell->new_cell
  auto update_to = MyMerkleProof::generate_raw(pool, to, [tree = usage_tree](const Ref<Cell> &cell) {

    auto loaded_cell = cell->load_cell().move_as_ok();  // FIXME
    bool has_been_loaded = loaded_cell.data_cell->is_fresh; // loaded_cell.data_cell->has_been_loaded;
    // if (!has_been_loaded) {
    //   LOG(ERROR) << "---------------------------------------- has_been_loaded=false"
    //              << ", special_type(): " << loaded_cell.data_cell->special_type()
    //              << ", size_refs(): " << loaded_cell.data_cell->size_refs();
    // }
    if (loaded_cell.data_cell->size_refs() == 0) {
      return false;
    }
    if (!loaded_cell.tree_node.empty() != has_been_loaded) {
      LOG(ERROR) << "mismatch: !loaded_cell.tree_node.empty()=" << !loaded_cell.tree_node.empty()
                 << ", but has_been_loaded=" << has_been_loaded;
    }
    return !loaded_cell.tree_node.empty();
    return has_been_loaded; // ??
    return !loaded_cell.tree_node.empty() && loaded_cell.tree_node.mark_path(tree);
  });
  usage_tree->set_use_mark_for_is_loaded(true);
  auto update_from = MyMerkleProof::generate_raw(pool, from, usage_tree);

  return {std::move(update_from), std::move(update_to)};
}


Ref<Cell> MyMerkleProof::generate_raw(BS::thread_pool<>& pool, Ref<Cell> cell, IsPrunnedFunction is_prunned) {
  return MyMerkleProofImpl(pool, is_prunned).create_from(cell);
}

Ref<Cell> MyMerkleProof::generate_raw(BS::thread_pool<>& pool, Ref<Cell> cell, CellUsageTree *usage_tree) {
  return MyMerkleProofImpl(pool, usage_tree).create_from(cell);
}



}
