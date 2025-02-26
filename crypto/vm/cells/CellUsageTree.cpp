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
namespace details {
// namespace fn_on_tree_traits {
//   struct ReturnSameToFn{};
//   struct ReturnWithOriginalPointer {};

//   template <class>
//   inline constexpr bool always_false_v = false;

//   template <typename ReturnValue, typename PtrType, typename ReturnTrait = fn_on_tree_traits::ReturnSameToFn>
//   constexpr auto return_carry(ReturnValue&& value, PtrType&& ptr) {
//     if constexpr (std::is_same_v<ReturnTrait, fn_on_tree_traits::ReturnSameToFn>) {
//       return std::forward<ReturnValue>(value);
//     } else if constexpr (std::is_same_v<ReturnTrait, fn_on_tree_traits::ReturnWithOriginalPointer>) {
//       return std::make_tuple(std::forward<ReturnValue>(value), std::forward<PtrType>(ptr));
//     } else {
//       static_assert(always_false_v<ReturnValue>, "unexpected trait");
//     }
//   }
// }

// clang 16 much slower here then clang 18
// template <typename Functor>
// auto fn_on_tree(const CellUsageTree::NodePtr::TreeVariant& tree_var, Functor&& fn) {
//   if (auto weak_var = std::get_if<CellUsageTree::NodePtr::TreeWeakPtr>(&tree_var)) {
//     auto tree = weak_var->lock();
//     return fn(tree.get());
//   } else if (auto ptr_var = std::get_if<CellUsageTree*>(&tree_var)) {
//     return fn(*ptr_var);
//   } else {
//     return fn(nullptr);
//   }
// }

template <typename Functor>
auto fn_on_tree(const CellUsageTree::NodePtr::TreeVariant& tree_var, Functor&& fn) {
  if (tree_var.is_weak()) {
    auto tree = tree_var.weak.lock();
    return fn(tree.get());
  } else {
    return fn(tree_var.ptr);
  }
}

}  // namespace details
//
// CellUsageTree::NodePtr
//
bool CellUsageTree::NodePtr::empty() const {
  if (node_id_ == 0) {
    return true;
  }

  return details::fn_on_tree(trees_variant_, [&](CellUsageTree* tree) { return tree == nullptr; });
}
bool CellUsageTree::NodePtr::on_load(const td::Ref<vm::DataCell>& cell) const {
  return details::fn_on_tree(trees_variant_, [&](CellUsageTree* tree) {
    if (tree == nullptr) {
      return false;
    }
    tree->on_load(node_id_, cell);
    return true;
  });
}

CellUsageTree::NodePtr CellUsageTree::NodePtr::create_child(unsigned ref_id) const {
  if (trees_variant_.is_weak()) {
    auto tree = trees_variant_.weak.lock();
    if (!tree) {
      return {};
    }
    return {trees_variant_.weak, tree->create_child(node_id_, ref_id)};
  } else {
    if (!trees_variant_.ptr) {
      return {};
    }
    return {trees_variant_.ptr, trees_variant_.ptr->create_child(node_id_, ref_id)};
  }
}

bool CellUsageTree::NodePtr::is_from_tree(const CellUsageTree* master_tree) const {
  DCHECK(master_tree);
  return details::fn_on_tree(trees_variant_, [&](CellUsageTree* tree) { return tree == master_tree; });
}

bool CellUsageTree::NodePtr::mark_path(CellUsageTree* master_tree) const {
  DCHECK(master_tree);
  return details::fn_on_tree(trees_variant_, [&](CellUsageTree* tree) {
    if (tree != master_tree) {
      return false;
    }
    master_tree->mark_path(node_id_);
    return true;
  });
}

//
// CellUsageTree
//
CellUsageTree::NodePtr CellUsageTree::root_ptr() {
  return {shared_from_this(), 1};
}

CellUsageTree::NodePtr CellUsageTree::root_ptr_persisten() {
  return {this, 1};
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
  if (cell_load_callback_) {
    cell_load_callback_(cell);
  }
}

CellUsageTree::NodeId CellUsageTree::create_child(NodeId node_id, unsigned ref_id) {
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
  NodeId res = static_cast<NodeId>(nodes_.size());
  nodes_.emplace_back();
  nodes_.back().parent = parent;
  return res;
}

}  // namespace vm
