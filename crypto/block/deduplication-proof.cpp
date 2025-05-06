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
#include "vm/cells/CellSlice.h"
#include "vm/excno.hpp"

namespace block {

using detail::Treap;

namespace {

void load_node_if_needed(Treap const& node) {
  if (node->is_loaded)
    return;

  auto cs = vm::load_cell_slice(node->cell);

  if (cs.size() != (vm::CellTraits::hash_bytes + 8) * 8 + 4) {
    throw vm::VmError{vm::Excno::cell_und, "Invalid deduplication proof node: bit length is not 324 bits"};
  }

  int off = 0;
  std::memcpy(node->hash.as_slice().data(), cs.data() + off, vm::CellTraits::hash_bytes);
  off += vm::CellTraits::hash_bytes;

  node->priority = node->hash;
  std::reverse(node->priority.as_slice().begin(), node->priority.as_slice().end());

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

detail::OverlayEntry find(Treap const& root, vm::CellHash hash) {
  load_node_if_needed(root);

  if (hash == root->hash) {
    return {
        .ref_cnt = root->reference_count,
        .pending_ref_cnt = root->reference_count,
        .merkle_depth = root->merkle_depth,
    };
  } else if (hash < root->hash) {
    if (root->left) {
      return find(root->left, hash);
    }
  } else {
    if (root->right) {
      return find(root->right, hash);
    }
  }

  return {};
}

Treap node_with_new_children(Treap node, Treap&& left, Treap&& right) {
  return std::make_shared<detail::Node>(Ref<vm::Cell>{}, node->hash, node->priority, node->reference_count,
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

std::pair<Treap, Treap> split(Treap const& root, vm::CellHash hash) {
  if (!root) {
    return {nullptr, nullptr};
  }

  load_node_if_needed(root);

  if (hash == root->hash) {
    return {root->left, root->right};
  } else if (hash < root->hash) {
    auto [left, right] = split(root->left, hash);
    return {std::move(left), node_with_new_children(root, std::move(right), std::shared_ptr{root->right})};
  } else {
    auto [left, right] = split(root->right, hash);
    return {node_with_new_children(root, std::shared_ptr{root->left}, std::move(left)), std::move(right)};
  }
}

}  // namespace

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

  // LOG_CHECK(treap_rollback_idx_ <= rollback_list_.size())
  //     << "FIXME: materialize is not implemented for not up-to-date treap on transaction entry";

  try {
    // apply changes from treap_rollback_idx_ to rollback_list_.size().
    for (size_t i = treap_rollback_idx_; i < rollback_list_.size(); ++i) {
      auto [hash, _] = rollback_list_[i];
      auto [left, right] = split(treap_, hash);
      auto& entry = overlay_[hash];
      if (entry.pending_ref_cnt == 0) {
        treap_ = merge(left, right);
      } else {
        auto priority = hash;
        std::reverse(priority.as_slice().begin(), priority.as_slice().end());
        auto node = std::make_shared<detail::Node>(Ref<vm::Cell>{}, hash, priority, entry.pending_ref_cnt,
                                                   entry.merkle_depth, true);
        treap_ = merge(left, merge(node, right));
      }
    }
  } catch (...) {
    proof_->rollback(*this);
    throw;
  }

  treap_rollback_idx_ = rollback_list_.size();
  CHECK(transaction_idx_ != std::numeric_limits<int>::max());
  ++transaction_idx_;

  return treap_->cell;
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
    auto entry = overlay_is_complete_ || !treap_ ? detail::OverlayEntry{} : find(treap_, hash);
    it = overlay_.insert({hash, entry}).first;
  }
  auto& pending_change = it->second;

  if (pending_change.version != transaction_idx_) {
    pending_change.rollback_index = rollback_list_.size();
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
