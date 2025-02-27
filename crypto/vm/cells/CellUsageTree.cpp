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
#include "vm/cells/CellUsageTree.h"

namespace vm {
//
// CellUsageTree::NodePtr
//
bool CellUsageTree::NodePtr::on_load(const td::Ref<vm::DataCell>& cell) const {
  //WriteLockGuard<NonRecursiveRWLock> lock(tree_mtx);
  
  auto tree = tree_weak_.lock();
  if (!tree) {
    return false;
  }
  tree->on_load(node_id_, cell);
  return true;
}

CellUsageTree::NodePtr CellUsageTree::NodePtr::create_child(unsigned ref_id) const {
  //WriteLockGuard<NonRecursiveRWLock> lock(tree_mtx);
  
  auto tree = tree_weak_.lock();
  if (!tree) {
    return {};
  }
  return {tree_weak_, tree->create_child(node_id_, ref_id)};
}

bool CellUsageTree::NodePtr::is_from_tree(const CellUsageTree* master_tree) const {
  //WriteLockGuard<NonRecursiveRWLock> lock(tree_mtx);
  
  DCHECK(master_tree);
  auto tree = tree_weak_.lock();
  if (tree.get() != master_tree) {
    return false;
  }
  return true;
}

bool CellUsageTree::NodePtr::mark_path(CellUsageTree* master_tree) const {
  //WriteLockGuard<NonRecursiveRWLock> lock(tree_mtx);
  
  DCHECK(master_tree);
  auto tree = tree_weak_.lock();
  if (tree.get() != master_tree) {
    return false;
  }
  master_tree->mark_path(node_id_);
  return true;
}

//
// CellUsageTree
//
CellUsageTree::NodePtr CellUsageTree::root_ptr() {
  //WriteLockGuard<NonRecursiveRWLock> lock(nodes_mtx);
  return {shared_from_this(), 1};
}

CellUsageTree::NodeId CellUsageTree::root_id() {
  return 1;
};

bool CellUsageTree::is_loaded(NodeId node_id) {
  std::lock_guard<std::recursive_mutex> lock(nodes_mtx);
  if (use_mark_) {
    return nodes_[node_id].has_mark;
  }
  return nodes_[node_id].is_loaded;
}

bool CellUsageTree::has_mark(NodeId node_id) {
  std::lock_guard<std::recursive_mutex> lock(nodes_mtx);
  return nodes_[node_id].has_mark;
}

void CellUsageTree::set_mark(NodeId node_id, bool mark) {
  std::lock_guard<std::recursive_mutex> lock(nodes_mtx);
  if (node_id == 0) {
    return;
  }
  nodes_[node_id].has_mark = mark;
}

void CellUsageTree::mark_path(NodeId node_id) {
  std::lock_guard<std::recursive_mutex> lock(nodes_mtx);
  auto cur_node_id = get_parent(node_id);
  while (cur_node_id != 0) {
    if (has_mark(cur_node_id)) {
      break;
    }
    set_mark(cur_node_id);
    cur_node_id = get_parent(cur_node_id);
  }
}

CellUsageTree::NodeId CellUsageTree::get_parent(NodeId node_id) {
  std::lock_guard<std::recursive_mutex> lock(nodes_mtx);
  return nodes_[node_id].parent;
}

CellUsageTree::NodeId CellUsageTree::get_child(NodeId node_id, unsigned ref_id) {
  std::lock_guard<std::recursive_mutex> lock(nodes_mtx);
  DCHECK(ref_id < CellTraits::max_refs);
  return nodes_[node_id].children[ref_id];
}

void CellUsageTree::set_use_mark_for_is_loaded(bool use_mark) {
  std::lock_guard<std::recursive_mutex> lock(nodes_mtx);
  use_mark_ = use_mark;
}

void CellUsageTree::on_load(NodeId node_id, const td::Ref<vm::DataCell>& cell) {
  std::lock_guard<std::recursive_mutex> lock(nodes_mtx);
  if (nodes_[node_id].is_loaded) {
    return;
  }
  nodes_[node_id].is_loaded = true;
  if (cell_load_callback_) {
    cell_load_callback_(cell);
  }
}

CellUsageTree::NodeId CellUsageTree::create_child(NodeId node_id, unsigned ref_id) {
  std::lock_guard<std::recursive_mutex> lock(nodes_mtx);
  DCHECK(ref_id < CellTraits::max_refs);
  NodeId res = nodes_[node_id].children[ref_id];
  if (res) {
    return res;
  }
  res = create_node(node_id);
  nodes_[node_id].children[ref_id] = res;
  return res;
}

CellUsageTree::NodeId CellUsageTree::create_node(NodeId parent) {
  std::lock_guard<std::recursive_mutex> lock(nodes_mtx);
  NodeId res = static_cast<NodeId>(nodes_.size());
  nodes_.emplace_back();
  nodes_.back().parent = parent;
  return res;
}

}  // namespace vm
