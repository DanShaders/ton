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
#include "td/utils/RwSpinLock.h"
#include "td/utils/CompileMacro.h"
#include <functional>

namespace vm {

class DataCell;

class CellUsageTree : public std::enable_shared_from_this<CellUsageTree> {
 public:
  static bool ENABLE_THREAD_SAFE;

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

  NodePtr root_ptr() {
    if (TD_UNLIKELY(ENABLE_THREAD_SAFE)) {
      auto lock = mutex_.lock_write();
      return root_ptr_impl();
    }
    return root_ptr_impl();
  }

  NodeId root_id() const {
    if (TD_UNLIKELY(ENABLE_THREAD_SAFE)) {
      auto lock = mutex_.lock_read();
      return root_id_impl();
    }
    return root_id_impl();
  }

  bool is_loaded(NodeId node_id) const {
    if (TD_UNLIKELY(ENABLE_THREAD_SAFE)) {
      auto lock = mutex_.lock_read();
      return is_loaded_impl(node_id);
    }
    return is_loaded_impl(node_id);
  }

  bool has_mark(NodeId node_id) const {
    if (TD_UNLIKELY(ENABLE_THREAD_SAFE)) {
      auto lock = mutex_.lock_read();
      return has_mark_impl(node_id);
    }
    return has_mark_impl(node_id);
  }

  void set_mark(NodeId node_id, bool mark = true) {
    if (TD_UNLIKELY(ENABLE_THREAD_SAFE)) {
      auto lock = mutex_.lock_write();
      set_mark_impl(node_id, mark);
    } else {
      set_mark_impl(node_id, mark);
    }
  }

  void mark_path(NodeId node_id) {
    if (TD_UNLIKELY(ENABLE_THREAD_SAFE)) {
      auto lock = mutex_.lock_write();
      mark_path_impl(node_id);
    } else {
      mark_path_impl(node_id);
    }
  }

  NodeId get_parent(NodeId node_id) {
    if (TD_UNLIKELY(ENABLE_THREAD_SAFE)) {
      auto lock = mutex_.lock_read();
      return get_parent_impl(node_id);
    }
    return get_parent_impl(node_id);
  }

  NodeId get_child(NodeId node_id, unsigned ref_id) {
    if (TD_UNLIKELY(ENABLE_THREAD_SAFE)) {
      auto lock = mutex_.lock_read();
      return get_child_impl(node_id, ref_id);
    }
    return get_child_impl(node_id, ref_id);
  }

  void set_use_mark_for_is_loaded(bool use_mark = true) {
    if (TD_UNLIKELY(ENABLE_THREAD_SAFE)) {
      auto lock = mutex_.lock_write();
      set_use_mark_for_is_loaded_impl(use_mark);
    } else {
      set_use_mark_for_is_loaded_impl(use_mark);
    }
  }

  NodeId create_child(NodeId node_id, unsigned ref_id) {
    if (TD_UNLIKELY(ENABLE_THREAD_SAFE)) {
      auto lock = mutex_.lock_write();
      return create_child_impl(node_id, ref_id);
    }
    return create_child_impl(node_id, ref_id);
  }

  void set_cell_load_callback(std::function<void(const td::Ref<vm::DataCell>&)> f) {
    if (TD_UNLIKELY(ENABLE_THREAD_SAFE)) {
      auto lock = mutex_.lock_write();
      cell_load_callback_ = std::move(f);
    } else {
      cell_load_callback_ = std::move(f);
    }
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
  mutable td::RwSpinLock mutex_;

  // Implementation methods
  TD_FORCE_INLINE NodePtr root_ptr_impl();
  TD_FORCE_INLINE NodeId root_id_impl() const;
  TD_FORCE_INLINE bool is_loaded_impl(NodeId node_id) const;
  TD_FORCE_INLINE bool has_mark_impl(NodeId node_id) const;
  TD_FORCE_INLINE void set_mark_impl(NodeId node_id, bool mark = true);
  TD_FORCE_INLINE void mark_path_impl(NodeId node_id);
  TD_FORCE_INLINE NodeId get_parent_impl(NodeId node_id);
  TD_FORCE_INLINE NodeId get_child_impl(NodeId node_id, unsigned ref_id);
  TD_FORCE_INLINE void set_use_mark_for_is_loaded_impl(bool use_mark);
  TD_FORCE_INLINE NodeId create_child_impl(NodeId node_id, unsigned ref_id);
  TD_FORCE_INLINE void on_load(NodeId node_id, const td::Ref<vm::DataCell>& cell);
  TD_FORCE_INLINE NodeId create_node(NodeId parent);
};

// Implementation of CellUsageTree::NodePtr methods
TD_FORCE_INLINE bool CellUsageTree::NodePtr::on_load(const td::Ref<vm::DataCell>& cell) const {
  auto tree = tree_weak_.lock();
  if (!tree) {
    return false;
  }
  if (TD_UNLIKELY(ENABLE_THREAD_SAFE)) {
    auto lock = tree->mutex_.lock_write();
    tree->on_load(node_id_, cell);
  } else {
    tree->on_load(node_id_, cell);
  }
  return true;
}

TD_FORCE_INLINE CellUsageTree::NodePtr CellUsageTree::NodePtr::create_child(unsigned ref_id) const {
  auto tree = tree_weak_.lock();
  if (!tree) {
    return {};
  }
  if (TD_UNLIKELY(ENABLE_THREAD_SAFE)) {
    auto lock = tree->mutex_.lock_write();
    return {tree_weak_, tree->create_child_impl(node_id_, ref_id)};
  }
  return {tree_weak_, tree->create_child_impl(node_id_, ref_id)};
}

TD_FORCE_INLINE bool CellUsageTree::NodePtr::is_from_tree(const CellUsageTree* master_tree) const {
  DCHECK(master_tree);
  auto tree = tree_weak_.lock();
  if (tree.get() != master_tree) {
    return false;
  }
  return true;
}

TD_FORCE_INLINE bool CellUsageTree::NodePtr::mark_path(CellUsageTree* master_tree) const {
  DCHECK(master_tree);
  auto tree = tree_weak_.lock();
  if (tree.get() != master_tree) {
    return false;
  }
  if (TD_UNLIKELY(ENABLE_THREAD_SAFE)) {
    auto lock = master_tree->mutex_.lock_write();
    master_tree->mark_path_impl(node_id_);
  } else {
    master_tree->mark_path_impl(node_id_);
  }
  return true;
}

// Implementation of CellUsageTree private methods
TD_FORCE_INLINE CellUsageTree::NodePtr CellUsageTree::root_ptr_impl() {
  return {shared_from_this(), 1};
}

TD_FORCE_INLINE CellUsageTree::NodeId CellUsageTree::root_id_impl() const {
  return 1;
}

TD_FORCE_INLINE bool CellUsageTree::is_loaded_impl(NodeId node_id) const {
  if (use_mark_) {
    return nodes_[node_id].has_mark;
  }
  return nodes_[node_id].is_loaded;
}

TD_FORCE_INLINE bool CellUsageTree::has_mark_impl(NodeId node_id) const {
  return nodes_[node_id].has_mark;
}

TD_FORCE_INLINE void CellUsageTree::set_mark_impl(NodeId node_id, bool mark) {
  if (node_id == 0) {
    return;
  }
  nodes_[node_id].has_mark = mark;
}

TD_FORCE_INLINE void CellUsageTree::mark_path_impl(NodeId node_id) {
  auto cur_node_id = get_parent_impl(node_id);
  while (cur_node_id != 0) {
    if (has_mark_impl(cur_node_id)) {
      break;
    }
    set_mark_impl(cur_node_id);
    cur_node_id = get_parent_impl(cur_node_id);
  }
}

TD_FORCE_INLINE CellUsageTree::NodeId CellUsageTree::get_parent_impl(NodeId node_id) {
  return nodes_[node_id].parent;
}

TD_FORCE_INLINE CellUsageTree::NodeId CellUsageTree::get_child_impl(NodeId node_id, unsigned ref_id) {
  DCHECK(ref_id < CellTraits::max_refs);
  return nodes_[node_id].children[ref_id];
}

TD_FORCE_INLINE void CellUsageTree::set_use_mark_for_is_loaded_impl(bool use_mark) {
  use_mark_ = use_mark;
}

TD_FORCE_INLINE void CellUsageTree::on_load(NodeId node_id, const td::Ref<vm::DataCell>& cell) {
  if (nodes_[node_id].is_loaded) {
    return;
  }
  nodes_[node_id].is_loaded = true;
  if (cell_load_callback_) {
    cell_load_callback_(cell);
  }
}

TD_FORCE_INLINE CellUsageTree::NodeId CellUsageTree::create_child_impl(NodeId node_id, unsigned ref_id) {
  DCHECK(ref_id < CellTraits::max_refs);
  NodeId res = nodes_[node_id].children[ref_id];
  if (res) {
    return res;
  }
  res = create_node(node_id);
  nodes_[node_id].children[ref_id] = res;
  return res;
}

TD_FORCE_INLINE CellUsageTree::NodeId CellUsageTree::create_node(NodeId parent) {
  NodeId res = static_cast<NodeId>(nodes_.size());
  nodes_.emplace_back();
  nodes_.back().parent = parent;
  return res;
}

}  // namespace vm
