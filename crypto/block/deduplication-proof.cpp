/*
    This file is part of TON Blockchain source code.

    TON Blockchain is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    TON Blockchain is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with TON Blockchain.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "deduplication-proof.h"
#include "td/utils/benchmark.h"
#include "vm/cells/CellSlice.h"
#include "vm/excno.hpp"
#include <queue>

namespace block {

using detail::Treap;

namespace {

struct Element {
  static Element min() {
    return {0, 0, 0, 0};
  }

  static Element max() {
    static constexpr auto value = std::numeric_limits<td::uint64>::max();
    return {value, value, value, value};
  }

  static Element from_vm_hash(vm::CellHash hash) {
    Element result;
    std::memcpy(&result, hash.as_slice().begin(), vm::CellTraits::hash_bytes);
    return result;
  }

  Element priority() const {
    return {td::bswap64(d), td::bswap64(c), td::bswap64(b), td::bswap64(a)};
  }

  std::strong_ordering operator<=>(Element const&) const = default;

  td::uint64 a, b, c, d;
};

static_assert(sizeof(Element) == vm::CellTraits::hash_bytes);

std::ostream& operator<<(std::ostream& out, Element const& element) {
  return out << "{" << element.a << " " << element.b << " " << element.c << " " << element.d << "}";
}

}  // namespace

namespace detail {

struct Node {
  Ref<vm::Cell> cell;

  Element key;
  Element priority;
  td::uint64 reference_count{0};
  int merkle_depth{0};

  bool is_loaded{false};

  Treap left{nullptr};
  Treap right{nullptr};
};

}  // namespace detail

namespace {

void load_node_if_needed(Treap const& node) {
  if (node->is_loaded)
    return;

  auto cs = vm::load_cell_slice(node->cell);

  if (cs.size() != (vm::CellTraits::hash_bytes + 8) * 8 + 4) {
    throw vm::VmError{vm::Excno::cell_und, "Invalid deduplication proof node: bit length is not 324 bits"};
  }

  int off = 0;
  std::memcpy(&node->key, cs.data() + off, vm::CellTraits::hash_bytes);
  off += vm::CellTraits::hash_bytes;

  node->priority = node->key.priority();

  std::memcpy(&node->reference_count, cs.data() + off, sizeof(td::uint64));
  off += sizeof(td::uint64);
  td::bswap64(node->reference_count);

  node->merkle_depth = cs.data()[off] & 3;
  unsigned child_mask = cs.data()[off] >> 2 & 3;

  if (std::popcount(child_mask) != static_cast<int>(cs.size_refs())) {
    throw vm::VmError{vm::Excno::cell_und,
                      "Invalid deduplication proof node: child mask does not match number of references"};
  }

  if (child_mask & 1) {
    node->left = std::make_shared<detail::Node>(cs.fetch_ref());
  }
  if (child_mask & 2) {
    node->right = std::make_shared<detail::Node>(cs.fetch_ref());
  }

  node->is_loaded = true;
}

detail::OverlayEntry find(Treap const& root, Element const& key) {
  load_node_if_needed(root);

  if (key == root->key) {
    return {
        .ref_cnt = root->reference_count,
        .pending_ref_cnt = root->reference_count,
        .merkle_depth = root->merkle_depth,
    };
  } else if (key < root->key) {
    if (root->left) {
      return find(root->left, key);
    }
  } else {
    if (root->right) {
      return find(root->right, key);
    }
  }

  return {};
}

Treap node_with_new_children(Treap node, Treap&& left, Treap&& right) {
  return std::make_shared<detail::Node>(Ref<vm::Cell>{}, node->key, node->priority, node->reference_count,
                                        node->merkle_depth, true, std::move(left), std::move(right));
}

Treap merge(Treap const& left, Treap const& right) {
  if (!left) {
    return right;
  }
  if (!right) {
    return left;
  }

  load_node_if_needed(left);
  load_node_if_needed(right);

  if (left->priority < right->priority) {
    return node_with_new_children(left, std::shared_ptr{left->left}, merge(left->right, right));
  } else {
    return node_with_new_children(right, merge(left, right->left), std::shared_ptr{right->right});
  }
}

std::pair<Treap, Treap> split(Treap const& root, Element const& key) {
  if (!root) {
    return {nullptr, nullptr};
  }

  load_node_if_needed(root);

  if (key == root->key) {
    return {root->left, root->right};
  } else if (key < root->key) {
    auto [left, right] = split(root->left, key);
    return {std::move(left), node_with_new_children(root, std::move(right), std::shared_ptr{root->right})};
  } else {
    auto [left, right] = split(root->right, key);
    return {node_with_new_children(root, std::shared_ptr{root->left}, std::move(left)), std::move(right)};
  }
}

}  // namespace

DeduplicationProof::~DeduplicationProof() = default;

std::unique_ptr<DeduplicationProof> DeduplicationProof::create_empty() {
  return create({}, 0, 0, {});
}

std::unique_ptr<DeduplicationProof> DeduplicationProof::create(Ref<vm::Cell>&& treap, td::uint64 cells, td::uint64 bits,
                                                               td::Span<Ref<vm::Cell>> roots) {
  return std::unique_ptr<DeduplicationProof>{new DeduplicationProof{std::move(treap), cells, bits, roots}};
}

std::unique_ptr<DeduplicationProofMutator> DeduplicationProof::start_transaction() const {
  CHECK(!transaction_in_progress_);
  transaction_in_progress_ = true;
  CHECK(transaction_idx_ != std::numeric_limits<int>::max());
  ++transaction_idx_;
  return std::unique_ptr<DeduplicationProofMutator>{new DeduplicationProofMutator{this}};
}

void DeduplicationProof::commit(std::unique_ptr<DeduplicationProofMutator> mutator) {
  CHECK(!mutator->is_commited_ && transaction_in_progress_);

  mutator->is_commited_ = true;
  cells_ = mutator->cells_;
  bits_ = mutator->bits_;
  roots_ = std::move(mutator->roots_);
  treap_ = std::move(mutator->treap_);
  treap_is_up_to_date_ = mutator->rollback_list_.size() == mutator->treap_rollback_idx_;
  mutator->rollback_list_.clear();
  transaction_in_progress_ = false;
}

void DeduplicationProof::rollback(DeduplicationProofMutator& mutator) const {
  CHECK(!mutator.is_commited_ && transaction_in_progress_);

  mutator.is_commited_ = true;
  transaction_in_progress_ = false;

  for (size_t i = mutator.rollback_list_.size(); i--;) {
    auto [hash, pending_ref_cnt] = mutator.rollback_list_[i];
    overlay_[hash].pending_ref_cnt = pending_ref_cnt;
  }

  mutator.rollback_list_.clear();
}

DeduplicationProof::DeduplicationProof(Ref<vm::Cell>&& treap, td::uint64 cells, td::uint64 bits,
                                       td::Span<Ref<vm::Cell>> roots)
    : cells_(cells)
    , bits_(bits)
    , roots_(roots.begin(), roots.end())
    , treap_(treap.not_null() ? std::make_shared<detail::Node>(std::move(treap)) : nullptr)
    , overlay_is_complete_(treap.is_null()) {
  if (!treap_) {
    CHECK(cells_ == 0 && bits_ == 0);
    for (auto const& root : roots_) {
      CHECK(root.is_null());
    }
  }
}

DeduplicationProofMutator::~DeduplicationProofMutator() {
  if (!is_commited_) {
    proof_->rollback(*this);
  }
}

void DeduplicationProofMutator::update(td::Span<Ref<vm::Cell>> new_roots) {
  CHECK(!is_commited_);

  for (auto [from, to, delta] : {
           std::tuple{td::Span{roots_}, new_roots, 1},
           {new_roots, roots_, -1},
       }) {
    for (auto const& root : to) {
      if (root.is_null() || std::find(from.begin(), from.end(), root) != from.end())
        continue;
      try {
        update_cell(root, delta);
      } catch (...) {
        proof_->rollback(*this);
        throw;
      }
    }
  }
  roots_ = std::vector(new_roots.begin(), new_roots.end());
}

Ref<vm::Cell> DeduplicationProofMutator::materialize() {
  CHECK(!is_commited_);

  if (treap_rollback_idx_ > rollback_list_.size()) {
    LOG(ERROR) << "FIXME: materialize is not implemented for not up-to-date treap on transaction entry";
    return {};
  }

  if (treap_rollback_idx_ == rollback_list_.size()) {
    return treap_ ? treap_->cell : Ref<vm::Cell>{};
  }

  // LOG_CHECK(treap_rollback_idx_ <= rollback_list_.size())
  //     << "FIXME: materialize is not implemented for not up-to-date treap on transaction entry";

  try {
    // apply changes from treap_rollback_idx_ to rollback_list_.size().
    struct Update {
      Element key;
      td::uint64 ref_cnt;
      int merkle_depth;
    };

    std::vector<Update> updates;
    updates.reserve(rollback_list_.size() - treap_rollback_idx_);

    for (size_t i = treap_rollback_idx_; i < rollback_list_.size(); ++i) {
      auto [hash, _] = rollback_list_[i];
      auto& entry = overlay_[hash];
      // std::cout << Element::from_vm_hash(hash) << " -> " << entry.pending_ref_cnt << " " << entry.ref_cnt << std::endl;
      updates.push_back({
          .key = Element::from_vm_hash(hash),
          .ref_cnt = entry.pending_ref_cnt,
          .merkle_depth = entry.merkle_depth,
      });
    }

    std::sort(updates.begin(), updates.end(), [](Update const& a, Update const& b) { return a.key < b.key; });

    size_t n = updates.size();

    std::vector<std::pair<Element, size_t>> segment_tree(2 * n);

    for (size_t i = 0; i < n; ++i) {
      segment_tree[n + i] = {updates[i].key.priority(), i};
    }
    for (size_t i = n - 1; i > 0; --i) {
      segment_tree[i] = std::min(segment_tree[2 * i], segment_tree[2 * i + 1]);
    }

    struct CurrentNode {
      bool operator>(CurrentNode const& other) const {
        return priority > other.priority;
      }

      Element priority;

      Treap node;
      std::optional<Element> range_left;
      std::optional<Element> range_right;
    };

    std::priority_queue<CurrentNode, std::vector<CurrentNode>, std::greater<CurrentNode>> current_nodes;

    auto push_node = [&](Treap const& node, std::optional<Element> range_left, std::optional<Element> range_right) {
      if (!node) {
        return;
      }

      load_node_if_needed(node);
      current_nodes.push({
          .priority = node->priority,
          .node = node,
          .range_left = range_left,
          .range_right = range_right,
      });
    };

    push_node(treap_, {}, {});

    treap_ = nullptr;

    // std::cout << "materialize" << std::endl;

    while (!current_nodes.empty() || segment_tree[1].second != n) {
      size_t st_index = segment_tree[1].second;
      auto& st_priority = segment_tree[1].first;
      bool st_empty = st_index == n;
      bool cn_empty = current_nodes.empty();

      auto find_segment = [&](Element const& key) {
        // std::cout << "trying to find segment for " << key << std::endl;
        Treap* node = &treap_;
        std::optional<Element> range_left;
        std::optional<Element> range_right;

        while (node->get() != nullptr) {
          // std::cout << "at " << node << " " << node->get() << " " << (*node)->is_loaded << std::endl;
          CHECK((*node)->is_loaded && (*node)->key != key);
          if (key < (*node)->key) {
            range_right = (*node)->key;
            node = &(*node)->left;
          } else {
            range_left = (*node)->key;
            node = &(*node)->right;
          }
        }
        return std::tuple{node, range_left, range_right};
      };

      auto maybe_leave_alone = [&](Treap const& treap, std::optional<Element> original_left,
                                   std::optional<Element> original_right) {};

      auto remove_update = [&] {
        segment_tree[n + st_index] = {Element::max(), n};
        size_t i = n + st_index;
        while (i /= 2) {
          segment_tree[i] = std::min(segment_tree[2 * i], segment_tree[2 * i + 1]);
        }
      };

      if (cn_empty || (!st_empty && st_priority < current_nodes.top().priority)) {
        auto& update = updates[st_index];
        if (update.ref_cnt != 0) {
          // std::cout << "adding update " << update.key << std::endl;
          auto [place, _1, _2] = find_segment(update.key);
          *place = std::make_shared<detail::Node>(Ref<vm::Cell>{}, update.key, st_priority, update.ref_cnt,
                                                  update.merkle_depth, true);
        }
        remove_update();
      } else if (auto cn_priority = current_nodes.top().priority; !st_empty && st_priority == cn_priority) {
        auto [_, node, range_left, range_right] = current_nodes.top();
        auto& update = updates[st_index];
        if (node->reference_count == update.ref_cnt) {
          // std::cout << "leaving node alone" << node->key << std::endl;
          maybe_leave_alone(node, range_left, range_right);
        } else {
          if (update.ref_cnt != 0) {
            // std::cout << "applying update " << update.key << std::endl;
            auto [place, _1, _2] = find_segment(update.key);
            *place = std::make_shared<detail::Node>(Ref<vm::Cell>{}, update.key, st_priority, update.ref_cnt,
                                                    update.merkle_depth, true);
          }
          push_node(node->left, range_left, node->key);
          push_node(node->right, node->key, range_right);
        }
        remove_update();
        current_nodes.pop();
      } else {
        auto [_, node, range_left, range_right] = current_nodes.top();
        maybe_leave_alone(node, range_left, range_right);
        current_nodes.pop();
      }
    }
  } catch (...) {
    proof_->rollback(*this);
    throw;
  }

  treap_rollback_idx_ = rollback_list_.size();
  CHECK(transaction_idx_ != std::numeric_limits<int>::max());
  ++transaction_idx_;

  return treap_ ? treap_->cell : Ref<vm::Cell>{};
}

DeduplicationProofMutator::DeduplicationProofMutator(DeduplicationProof const* proof)
    : proof_(proof)
    , cells_(proof->cells_)
    , bits_(proof->bits_)
    , roots_(proof->roots_.begin(), proof->roots_.end())
    , treap_(proof->treap_)
    , overlay_(proof->overlay_)
    , overlay_is_complete_(proof->overlay_is_complete_)
    , transaction_idx_(proof->transaction_idx_)
    , treap_rollback_idx_(proof->treap_is_up_to_date_ ? 0 : std::numeric_limits<size_t>::max()) {
}

int DeduplicationProofMutator::update_cell(Ref<vm::Cell> const& cell, int delta) {
  DCHECK(delta != 0);

  auto hash = cell->get_hash();

  auto it = overlay_.find(hash);
  if (it == overlay_.end()) {
    detail::OverlayEntry entry{};
    if (!overlay_is_complete_ && treap_) {
      entry = find(treap_, Element::from_vm_hash(hash));
    }
    it = overlay_.insert({hash, entry}).first;
  }
  auto& pending_change = it->second;

  if (pending_change.version != transaction_idx_) {
    pending_change.rollback_index = rollback_list_.size();
    pending_change.version = transaction_idx_;
    rollback_list_.push_back({hash, pending_change.pending_ref_cnt});
  }
  pending_change.pending_ref_cnt += delta;

  if ((delta > 0 && pending_change.pending_ref_cnt != static_cast<td::uint64>(delta)) ||
      (delta < 0 && pending_change.pending_ref_cnt != 0)) {
    CHECK(pending_change.merkle_depth != -1);
    return pending_change.merkle_depth;
  }

  bool is_special;
  auto cs = vm::load_cell_slice_special(cell, is_special);

  if (delta > 0) {
    ++cells_;
    bits_ += cs.size();

    int new_merkle_depth = 0;
    for (unsigned i = 0; i < cs.size_refs(); ++i) {
      new_merkle_depth = td::max(new_merkle_depth, update_cell(cs.prefetch_ref(i), 1));
    }

    if (cs.special_type() == vm::Cell::SpecialType::MerkleProof ||
        cs.special_type() == vm::Cell::SpecialType::MerkleUpdate) {
      ++new_merkle_depth;
    }

    ++merkle_depths_[td::min(merkle_depth_cutoff, new_merkle_depth)];
    int& merkle_depth_in_overlay = overlay_[hash].merkle_depth;
    CHECK(merkle_depth_in_overlay == -1 || merkle_depth_in_overlay == new_merkle_depth)
    return merkle_depth_in_overlay = new_merkle_depth;
  } else {
    --cells_;
    bits_ -= cs.size();

    --merkle_depths_[td::min(merkle_depth_cutoff, pending_change.merkle_depth)];

    for (unsigned i = 0; i < cs.size_refs(); ++i) {
      update_cell(cs.prefetch_ref(i), -1);
    }
    return -1;
  }
}

}  // namespace block
