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
  if (!tree_) {
    return false;
  }
  tree_->on_load(node_id_, cell);
  return true;
}

CellUsageTree::NodePtr CellUsageTree::NodePtr::create_child(unsigned ref_id) const {
  if (!tree_) {
    return {};
  }
  return {tree_, tree_->create_child(node_id_, ref_id)};
}

bool CellUsageTree::NodePtr::is_from_tree(const CellUsageTree* master_tree) const {
  DCHECK(master_tree);
  if (tree_ != master_tree) {
    return false;
  }
  return true;
}

bool CellUsageTree::NodePtr::mark_path(CellUsageTree* master_tree) const {
  DCHECK(master_tree);
  if (tree_ != master_tree) {
    return false;
  }
  master_tree->mark_path(node_id_);
  return true;
}

//
// CellUsageTree
//
CellUsageTree::NodePtr CellUsageTree::root_ptr() {
  CHECK(root_);
  return {this, root_};
}

CellUsageTree::NodeId CellUsageTree::root_id() const {
  CHECK(root_);
  return root_;
};

bool CellUsageTree::is_loaded(NodeId node_id) const {
  if (node_id == nullptr) {
    return false;
  }
  if (use_mark_) {
    return node_id->has_mark;
  }
  return node_id->is_loaded;
}

bool CellUsageTree::has_mark(NodeId node_id) const {
  if (node_id == nullptr) {
    return false;
  }
  return node_id->has_mark;
}

void CellUsageTree::set_mark(NodeId node_id, bool mark) {
  if (node_id == nullptr) {
    return;
  }
  node_id->has_mark = mark;
}

void CellUsageTree::mark_path(NodeId node_id) {
  if (node_id == nullptr) {
    return;
  }
  auto cur_node_id = get_parent(node_id);
  while (cur_node_id != nullptr) {
    if (has_mark(cur_node_id)) {
      break;
    }
    set_mark(cur_node_id);
    cur_node_id = get_parent(cur_node_id);
  }
}

CellUsageTree::NodeId CellUsageTree::get_parent(NodeId node_id) {
  if (node_id == nullptr) {
    return nullptr;
  }
  return node_id->parent;
}

CellUsageTree::NodeId CellUsageTree::get_child(NodeId node_id, unsigned ref_id) {
  DCHECK(ref_id < CellTraits::max_refs);
  if (node_id == nullptr) {
    return nullptr;
  }
  return node_id->children[ref_id];
}

void CellUsageTree::set_use_mark_for_is_loaded(bool use_mark) {
  use_mark_ = use_mark;
}

void CellUsageTree::on_load(NodeId node_id, const td::Ref<vm::DataCell>& cell) {
  if (node_id == nullptr) {
    return;
  }

  bool expected = false;
  if (node_id->is_loaded.compare_exchange_strong(expected, true)) {
    if (cell_load_callback_) {
      cell_load_callback_(cell);
    }
  }
}

CellUsageTree::NodeId CellUsageTree::create_child(NodeId node_id, unsigned ref_id) {
  DCHECK(ref_id < CellTraits::max_refs);
  CHECK(node_id);

  if (node_id->children[ref_id] != nullptr) {
    return node_id->children[ref_id];
  }

  auto new_node = new Node();
  new_node->parent = node_id;

  Node* expected = nullptr;
  if (!node_id->children[ref_id].compare_exchange_strong(expected, new_node)) {
    delete new_node;
    return expected;
  }

  return new_node;
}

}  // namespace vm
