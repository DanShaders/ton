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
#include "vm/cells/Cell.h"
#include "vm/cells/ArenaAllocator.h"

namespace vm {
class VirtualCell : public Cell {
 private:
  struct PrivateTag {};

  struct ArenaAllocator : public ArenaAllocatorBase {
    template <class... ArgsT>
    VirtualCell* alloc(ArgsT&&... args) {
      std::pair<char*, int*> res = fast_alloc(sizeof(VirtualCell));
      VirtualCell* obj = new (res.first) VirtualCell(std::forward<ArgsT>(args)...);
      obj->arena_counter_ = res.second;
      return obj;
    }
  };

  friend struct ArenaAllocator;

 public:
  static thread_local bool use_arena;
  static Ref<Cell> create(VirtualizationParameters virt, Ref<Cell> cell) {
    if (cell->get_level() <= virt.get_level()) {
      return cell;
    }
    if (use_arena) {
      thread_local ArenaAllocator alloc;
      VirtualCell* ptr = alloc.alloc(virt, std::move(cell), PrivateTag{});
      return Ref<VirtualCell>{ptr, Ref<VirtualCell>::acquire_virtualcell_t{}};
    }
    return Ref<VirtualCell>{true, virt, std::move(cell), PrivateTag{}};
  }

  VirtualCell(VirtualizationParameters virt, Ref<Cell> cell, PrivateTag) : virt_(virt), cell_(std::move(cell)) {
    CHECK(cell_->get_virtualization() <= virt_.get_virtualization());
  }
  ~VirtualCell() override {
    if (arena_counter_) {
      --*arena_counter_;
    }
  }

  // load interface
  td::Result<LoadedCell> load_cell() const override {
    TRY_RESULT(loaded_cell, cell_->load_cell());
    loaded_cell.virt = loaded_cell.virt.apply(virt_);
    return std::move(loaded_cell);
  }

  Ref<Cell> virtualize(VirtualizationParameters virt) const override {
    auto new_virt = virt_.apply(virt);
    if (new_virt == virt_) {
      return Ref<Cell>(this);
    }
    return create(new_virt, cell_);
  }

  td::uint32 get_virtualization() const override {
    return virt_.get_virtualization();
  }

  CellUsageTree::NodePtr get_tree_node() const override {
    return cell_->get_tree_node();
  }

  bool is_loaded() const override {
    return cell_->is_loaded();
  }

  // hash and level
  LevelMask get_level_mask() const override {
    return cell_->get_level_mask().apply(virt_.get_level());
  }

 protected:
  const Hash do_get_hash(td::uint32 level) const override {
    return cell_->get_hash(fix_level(level));
  }
  td::uint16 do_get_depth(td::uint32 level) const override {
    return cell_->get_depth(fix_level(level));
  }

 private:
  VirtualizationParameters virt_;
  Ref<Cell> cell_;
  int* arena_counter_ = nullptr;

  int fix_level(int level) const {
    return get_level_mask().apply(level).get_level();
  }
};

inline thread_local bool VirtualCell::use_arena = true;
}  // namespace vm
