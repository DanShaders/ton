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

#include "vm/cells/CellSlice.h"
#include "vm/cells/DataCell.h"

#include "td/utils/misc.h"
#include "td/utils/format.h"

#include "openssl/digest.hpp"

namespace vm {

using td::Ref;
using td::RefAny;

/*
 * 
 *   CELL BUILDERS
 * 
 */

Ref<Cell> CellBuilder::create_pruned_branch(Ref<Cell> cell, td::uint32 new_level, td::uint32 virt_level) {
  if (cell->is_loaded() && cell->get_level() <= virt_level && cell->get_virtualization() == 0) {
    CellSlice cs(NoVm{}, cell);
    if (cs.size_refs() == 0) {
      return cell;
    }
  }
  return do_create_pruned_branch(std::move(cell), new_level, virt_level);
}

bool CellBuilder::append_cellslice_bool(const CellSlice& cs) {
  unsigned len = cs.size();
  if (can_extend_by(len, cs.size_refs())) {
    int pos = bits;
    ensure_throw(prepare_reserve(len));
    td::bitstring::bits_memcpy(td::BitPtr{data, pos}, cs.data_bits(), len);
    for (unsigned i = 0; i < cs.size_refs(); i++) {
      refs[refs_cnt++] = cs.prefetch_ref(i);
    }
    return true;
  } else {
    return false;
  }
}

bool CellBuilder::append_cellslice_chk(const CellSlice& cs, unsigned size_ext) {
  return cs.size_ext() == size_ext && append_cellslice_bool(cs);
}

CellSlice CellSlice::clone() const {
  CellBuilder cb;
  Ref<Cell> cell;
  if (cb.append_cellslice_bool(*this) && cb.finalize_to(cell)) {
    return CellSlice{NoVmOrd(), std::move(cell)};
  } else {
    return {};
  }
}

CellSlice CellBuilder::as_cellslice() const& {
  return CellSlice{finalize_copy()};
}

CellSlice CellBuilder::as_cellslice() && {
  return CellSlice{finalize()};
}

bool CellBuilder::contents_equal(const CellSlice& cs) const {
  if (size() != cs.size() || size_refs() != cs.size_refs()) {
    return false;
  }
  if (td::bitstring::bits_memcmp(data_bits(), cs.data_bits(), size())) {
    return false;
  }
  for (unsigned i = 0; i < size_refs(); i++) {
    if (refs[i]->get_hash() != cs.prefetch_ref(i)->get_hash()) {
      return false;
    }
  }
  return true;
}

Ref<CellSlice> CellBuilder::as_cellslice_ref() const& {
  return Ref<CellSlice>{true, finalize_copy()};
}

Ref<CellSlice> CellBuilder::as_cellslice_ref() && {
  return Ref<CellSlice>{true, finalize()};
}

CellBuilder& CellBuilder::append_cellslice(Ref<CellSlice> cs) {
  return ensure_pass(append_cellslice_bool(cs));
}

bool CellBuilder::append_cellslice_chk(Ref<CellSlice> cs_ref, unsigned size_ext) {
  return cs_ref.not_null() && append_cellslice_chk(*cs_ref, size_ext);
}

CellBuilder& CellBuilder::append_cellslice(const CellSlice& cs) {
  return ensure_pass(append_cellslice_bool(cs));
}

bool CellBuilder::append_cellslice_bool(Ref<CellSlice> cs_ref) {
  return cs_ref.not_null() && append_cellslice_bool(*cs_ref);
}

}  // namespace vm
