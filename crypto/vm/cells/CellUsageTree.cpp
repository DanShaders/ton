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
  auto tree = tree_weak_.lock();
  if (!tree) {
    LOG(ERROR) << "on_load !tree at " << this->node_id_; // !! TODO: remove
    return false;
  }
  tree->on_load(node_id_, cell);
  return true;
}

CellUsageTree::NodePtr CellUsageTree::NodePtr::create_child(unsigned ref_id) const {
  auto tree = tree_weak_.lock();
  if (!tree) {
    LOG(ERROR) << "create_child !tree at " << this->node_id_; // !! TODO: remove
    return {};
  }
  return {tree_weak_, tree->create_child(node_id_, ref_id)};
}

bool CellUsageTree::NodePtr::is_from_tree(const CellUsageTree* master_tree) const {
  DCHECK(master_tree);
  auto tree = tree_weak_.lock();
  if (tree.get() != master_tree) {
    LOG(ERROR) << "is_from_tree (tree.get() != master_tree) at " << this->node_id_; // !! TODO: remove
    return false;
  }
  return true;
}

bool CellUsageTree::NodePtr::mark_path(CellUsageTree* master_tree) const {
  DCHECK(master_tree);
  auto tree = tree_weak_.lock();
  if (tree.get() != master_tree) {
    LOG(ERROR) << "mark_path (tree.get() != master_tree) at " << this->node_id_; // !! TODO: remove
    return false;
  }
  master_tree->mark_path(node_id_);
  return true;
}

//
// CellUsageTree
//
CellUsageTree::CellUsageTree() {
  // LOG(ERROR) << "CellUsageTree constructor";
  // nodes_.resize(600000); // !TEMP_THREAD
  nodes_ = new Node[600000];

  // Based on (https://answers.ton.org/question/1555820646674468864/what-is-the-byte-size-of-a-smart-contract-that-can-be-deployed-on-ton)
  // (not a good source, but the only one I found)
  // the maximum block size is 2Mb
  // In order to store 600 000 cells in BOC, even if they only have refs without data,
  // at least 2.7Mb is needed:
  //     Per cell: 2 descriptor bytes + at least 20 addressing bits = 4.5 bytes
  //            x 600 000 cells
  // Preallocating everything seems better for performance, and it's not that much data.
}
CellUsageTree::~CellUsageTree() {
  delete[] nodes_;
}


CellUsageTree::NodePtr CellUsageTree::root_ptr() {
  return {shared_from_this(), 1};
}

CellUsageTree::NodeId CellUsageTree::root_id() const {
  return 1;
};

bool CellUsageTree::is_loaded(NodeId node_id) const {
  if (use_mark_) {
    return nodes_[node_id].has_mark;
  }
  return nodes_[node_id].is_loaded;
}

bool CellUsageTree::has_mark(NodeId node_id) const {
  return nodes_[node_id].has_mark;
}

void CellUsageTree::set_mark(NodeId node_id, bool mark) {
  if (node_id == 0) {
    return;
  }
  nodes_[node_id].has_mark = mark;
}

void CellUsageTree::mark_path(NodeId node_id) {
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
  return nodes_[node_id].parent;
}

CellUsageTree::NodeId CellUsageTree::get_child(NodeId node_id, unsigned ref_id) {
  DCHECK(ref_id < CellTraits::max_refs);
  return nodes_[node_id].children[ref_id];
}

void CellUsageTree::set_use_mark_for_is_loaded(bool use_mark) {
  use_mark_ = use_mark;
}

void CellUsageTree::on_load(NodeId node_id, const td::Ref<vm::DataCell>& cell) {
  if (nodes_[node_id].is_loaded) {
    return;
  }
  nodes_[node_id].is_loaded = true;
  if (cell_load_callback_) { // Not thread-safe, can be called several times, but it's not used in the challenge
    cell_load_callback_(cell);
  }
}

CellUsageTree::NodeId CellUsageTree::create_child(NodeId node_id, unsigned ref_id) {
  DCHECK(ref_id < CellTraits::max_refs);
  NodeId res = nodes_[node_id].children[ref_id];
  if (res) {
    return res;
  }
  std::lock_guard _(mt);
  res = nodes_[node_id].children[ref_id];
  if (res) {
    return res;
  }

  // res = create_node(node_id);

  // // nodes_.emplace_back( { false, false, node_id, {0, 0, 0, 0} } );
  // res = nodes_[node_id].children[ref_id] = (td::uint32)nodes_.size();
  // nodes_.push_back({ false, false, node_id, {0, 0, 0, 0} });
  // // nodes_.emplace_back(false, false, node_id, std::array<td::uint32, CellTraits::max_refs>{0, 0, 0, 0}); // Doesn't compile
  // dynamic vector times:
  // Passed 30000/300 tests
  // Total time (only passed valid tests): 653.57978
  // Total CPU time (only passed valid tests): 888.27448

  res = nodes_count++;
  nodes_[res].parent = node_id;
  nodes_[node_id].children[ref_id] = res;
  return res;
}

// CellUsageTree::NodeId CellUsageTree::create_node(NodeId parent) {
//   // NodeId res = static_cast<NodeId>(nodes_.size());
//   // nodes_.emplace_back();
//   // nodes_.back().parent = parent;

//   NodeId res;
//   {
//     // std::lock_guard<std::mutex> g(mt);
//     res = nodes_count_++; // static_cast<NodeId>(nodes_.size());
//     if (res >= 160000) {
//       LOG(ERROR) << "CellUsageTree reached 160 000 elements!"; 
//     }
//     CellUsageTree::Node& newNode = nodes_[res]; // nodes_.emplace_back();
//     newNode.parent = parent;
//   }
//   return res;
// }

}  // namespace vm
