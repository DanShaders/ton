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
#pragma once

#include "common/bitstring.h"
#include "common/refcnt.hpp"
#include "td/utils/HashMap.h"
#include "td/utils/Span.h"
#include "vm/cells/Cell.h"

namespace block {
using td::Ref;

namespace detail {

struct Node;
using Treap = std::shared_ptr<Node>;

struct OverlayEntry {
  td::uint64 ref_cnt{0};
  td::uint64 pending_ref_cnt{0};
  int merkle_depth{-1};

  int version{0};
  size_t rollback_index{0};
};

using Overlay = td::HashMap<vm::CellHash, OverlayEntry>;

}  // namespace detail

class DeduplicationProofMutator;

class DeduplicationProof {
 public:
  ~DeduplicationProof();

  static std::unique_ptr<DeduplicationProof> create_empty();

  static std::unique_ptr<DeduplicationProof> create(Ref<vm::Cell>&& treap, td::uint64 cells, td::uint64 bits,
                                                    td::Span<Ref<vm::Cell>> roots);

  std::unique_ptr<DeduplicationProofMutator> start_transaction() const;
  void commit(std::unique_ptr<DeduplicationProofMutator> mutator);
  void rollback(DeduplicationProofMutator& mutator) const;

 private:
  friend DeduplicationProofMutator;

  DeduplicationProof(Ref<vm::Cell>&& treap, td::uint64 cells, td::uint64 bits, td::Span<Ref<vm::Cell>> roots);

  mutable bool transaction_in_progress_{false};
  mutable int transaction_idx_{0};

  td::uint64 cells_{0};
  td::uint64 bits_{0};
  std::vector<Ref<vm::Cell>> roots_;
  detail::Treap treap_;
  bool overlay_is_complete_;
  bool treap_is_up_to_date_{true};
  mutable detail::Overlay overlay_;
};

class DeduplicationProofMutator {
 public:
  static constexpr int merkle_depth_cutoff = 4;

  DeduplicationProofMutator(DeduplicationProofMutator const&) = delete;
  DeduplicationProofMutator(DeduplicationProofMutator&&) = delete;

  ~DeduplicationProofMutator();

  void update(td::Span<Ref<vm::Cell>> new_roots);
  Ref<vm::Cell> materialize();

  td::uint64 cells() const {
    CHECK(!is_commited_);
    return cells_;
  }

  td::uint64 bits() const {
    CHECK(!is_commited_);
    return bits_;
  }

  int max_merkle_depth_of_new_cell() const {
    CHECK(!is_commited_);
    for (int i = merkle_depth_cutoff; i > 0; --i) {
      if (merkle_depths_[i] != 0) {
        return i;
      }
    }
    return 0;
  }

 private:
  friend DeduplicationProof;

  DeduplicationProofMutator(DeduplicationProof const* proof);

  int update_cell(Ref<vm::Cell> const& cell, int delta);

  DeduplicationProof const* proof_;

  td::uint64 cells_;
  td::uint64 bits_;
  std::vector<Ref<vm::Cell>> roots_;
  detail::Treap treap_;
  detail::Overlay& overlay_;
  td::uint64 merkle_depths_[merkle_depth_cutoff + 1]{};
  std::vector<std::pair<vm::CellHash, td::uint64>> rollback_list_;
  bool is_commited_{false};
  bool overlay_is_complete_;
  int& transaction_idx_;
  size_t treap_rollback_idx_;
};

}  // namespace block
