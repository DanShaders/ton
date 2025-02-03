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
#include "vm/cells/DataCell.h"

#include "openssl/digest.hpp"

#include "td/utils/ScopeGuard.h"

#include "vm/cells/CellWithStorage.h"
#include <openssl/sha.h>

/// The experiment is disabled by default
#ifdef TON_EXPERIMENT_ISAL_CRYPTO
  /// ISA-L crypto
  #include <sha256_mb.h>      // isal_sha256_*
  #include <memcpy_inline.h>
  #include <endian_helper.h>  // to_be
#endif

/// The experiment is disabled by default
#ifdef TON_EXPERIMENT_SHANI
#ifndef TD_WINDOWS
  #include "third-party/flo-shani-aesni/sha256/flo-shani.h"
  #include "third-party/flo-shani-aesni/cpuid/flo-cpuid.h"
#endif
#endif

namespace vm {
thread_local bool DataCell::use_arena = false;

namespace {
template <class CellT>
struct ArenaAllocator {
  template <class T, class... ArgsT>
  std::unique_ptr<CellT> make_unique(ArgsT&&... args) {
    auto* ptr = fast_alloc(sizeof(T));
    T* obj = new (ptr) T(std::forward<ArgsT>(args)...);
    return std::unique_ptr<T>(obj);
  }
private:
  td::MutableSlice alloc_batch() {
    size_t batch_size = 1 << 20;
    auto batch = std::make_unique<char[]>(batch_size);
    return td::MutableSlice(batch.release(), batch_size);
  }
  char* fast_alloc(size_t size) {
    thread_local td::MutableSlice batch;
    auto aligned_size = (size + 7) / 8 * 8;
    if (batch.size() < size) {
      batch = alloc_batch();
    }
    auto res = batch.begin();
    batch.remove_prefix(aligned_size);
    return res;
  }
};
}

/// 7,594,000
std::unique_ptr<DataCell> DataCell::create_empty_data_cell(Info info) {
  const size_t storage_size = info.get_storage_size();

  if (use_arena) {
    ArenaAllocator<DataCell> allocator;
    auto res = detail::CellWithArrayStorage<DataCell>::create(allocator, storage_size, std::move(info));
    // this is dangerous
    Ref<DataCell>(res.get()).release();
    return res;
  }

  return detail::CellWithArrayStorage<DataCell>::create(storage_size, std::move(info)); // default allocator
}

void DataCell::destroy_storage(char* storage) {
  auto* refs = info_.get_refs(storage);
  for (size_t i = 0; i < get_refs_cnt(); i++) {
    Ref<Cell>(refs[i], Ref<Cell>::acquire_t{});  // call destructor
  }
}

/// 36,000
td::Result<Ref<DataCell>> DataCell::create(td::ConstBitPtr data, unsigned bits, td::Span<Ref<Cell>> refs,
                                           bool special) {
  /// Возможно, что эти действия лишние? Нужно копирование? Или перевод в другой формат??
  std::array<Ref<Cell>, max_refs> copied_refs;
  CHECK(refs.size() <= copied_refs.size());
  for (size_t i = 0; i < refs.size(); i++) {
    copied_refs[i] = refs[i];
  }
  return create(std::move(data), bits, td::MutableSpan<Ref<Cell>>(copied_refs.data(), refs.size()), special);
}

#ifdef TON_EXPERIMENT_ISAL_CRYPTO
/// \example https://github.com/intel/isa-l_crypto/blob/v2.25.0/sha256_mb/sha256_mb_test.c
class ABSL_ATTRIBUTE_FUNC_ALIGN(16) HasherSha256Isal {
 private:
  ABSL_ATTRIBUTE_FUNC_ALIGN(16) ISAL_SHA256_HASH_CTX_MGR mgr_;
  ABSL_ATTRIBUTE_FUNC_ALIGN(16) ISAL_SHA256_HASH_CTX ctx_in_;
  ABSL_ATTRIBUTE_FUNC_ALIGN(16) ISAL_SHA256_HASH_CTX *ctx_out_;
  int rc_ = 0;

  void init() {
    isal_hash_ctx_init(&ctx_in_);
  }

 public:
  HasherSha256Isal() {
    rc_ = isal_sha256_ctx_mgr_init(&mgr_);
    if (rc_ != 0) {
      isal_hash_ctx_init(&ctx_in_);
    }
  }

  int sha256(const uint8_t* data, const size_t size, uint8_t* digest) {
    init();
    rc_ = isal_sha256_ctx_mgr_submit(&mgr_, &ctx_in_, &ctx_out_, data, size, ISAL_HASH_ENTIRE);
    if (rc_ != 0) {
      return rc_;
    }

    rc_ = isal_sha256_ctx_mgr_flush(&mgr_, &ctx_out_);
    if (rc_ != 0) {
      return rc_;
    }

    /// https://github.com/intel/isa-l_crypto/blob/v2.25.0/sha256_mb/sha256_mb_vs_ossl_perf.c
    uint32_t* out = reinterpret_cast<uint32_t*>(digest);
    out[0] = to_be32(ctx_out_->job.result_digest[0]);
    out[1] = to_be32(ctx_out_->job.result_digest[1]);
    out[2] = to_be32(ctx_out_->job.result_digest[2]);
    out[3] = to_be32(ctx_out_->job.result_digest[3]);
    out[4] = to_be32(ctx_out_->job.result_digest[4]);
    out[5] = to_be32(ctx_out_->job.result_digest[5]);
    out[6] = to_be32(ctx_out_->job.result_digest[6]);
    out[7] = to_be32(ctx_out_->job.result_digest[7]);
    return 0;
  }

  bool is_ok() {
    return rc_ == 0; 
  }
};
#endif

/// [PERF] 18.44%
/// 7,594,000
td::Result<Ref<DataCell>> DataCell::create(td::ConstBitPtr data, unsigned bits, td::MutableSpan<Ref<Cell>> refs,
                                           bool special) {
  for (auto& ref : refs) {
    if (ref.is_null()) {
      return td::Status::Error("Has null cell reference");
    }
  }

  SpecialType type = SpecialType::Ordinary;
  if (special) {
    if (bits < 8) {
      return td::Status::Error("Not enough data for a special cell");
    }
    type = static_cast<SpecialType>(td::bitstring::bits_load_ulong<8>(data));
    if (type == SpecialType::Ordinary) {
      return td::Status::Error("Special cell has Ordinary type");
    }
  }

  LevelMask level_mask;
  td::uint32 virtualization = 0;
  switch (type) {
    case SpecialType::Ordinary: {
      for (auto& ref : refs) {
        level_mask = level_mask.apply_or(ref->get_level_mask());
        virtualization = td::max(virtualization, ref->get_virtualization());
      }
      break;
    }

    case SpecialType::PrunnedBranch: {
      if (refs.size() != 0) {
        return td::Status::Error("PrunnedBranch special cell has a cell reference");
      }
      if (bits < 16) {
        return td::Status::Error("Not enough data for a PrunnedBranch special cell");
      }
      level_mask = LevelMask((td::bitstring::bits_load_ulong<8>(data + 8)) & 0xff);
      auto level = level_mask.get_level();
      if (level > max_level || level == 0) {
        return td::Status::Error("Prunned Branch has an invalid level");
      }
      if (bits != (2 + level_mask.apply(level - 1).get_hashes_count() * (hash_bytes + depth_bytes)) * 8) {
        return td::Status::Error("Not enouch data for a PrunnedBranch special cell");
      }
      // depth will be checked later!
      break;
    }

    case SpecialType::Library: {
      if (bits != 8 + hash_bytes * 8) {
        return td::Status::Error("Not enouch data for a Library special cell");
      }
      if (!refs.empty()) {
        return td::Status::Error("Library special cell has a cell reference");
      }
      break;
    }

    case SpecialType::MerkleProof: {
      if (bits != 8 + (hash_bytes + depth_bytes) * 8) {
        return td::Status::Error("Not enouch data for a MerkleProof special cell");
      }
      if (refs.size() != 1) {
        return td::Status::Error("Wrong references count for a MerkleProof special cell");
      }
      if (td::bitstring::bits_memcmp(data + 8, refs[0]->get_hash(0).as_bitslice().get_ptr(), hash_bits) != 0) {
        return td::Status::Error("Hash mismatch in a MerkleProof special cell");
      }
      if (td::bitstring::bits_load_ulong(data + 8 + hash_bits, depth_bytes * 8) != refs[0]->get_depth(0)) {
        return td::Status::Error("Depth mismatch in a MerkleProof special cell");
      }
      level_mask = refs[0]->get_level_mask().shift_right();
      virtualization = refs[0]->get_virtualization();
      break;
    }

    case SpecialType::MerkleUpdate: {
      if (bits != 8 + (hash_bytes + depth_bytes) * 8 * 2) {
        return td::Status::Error("Not enouch data for a MerkleUpdate special cell");
      }
      if (refs.size() != 2) {
        return td::Status::Error("Wrong references count for a MerkleUpdate special cell");
      }
      if (td::bitstring::bits_memcmp(data + 8, refs[0]->get_hash(0).as_bitslice().get_ptr(), hash_bits) != 0) {
        return td::Status::Error("First hash mismatch in a MerkleProof special cell");
      }
      if (td::bitstring::bits_memcmp(data + 8 + hash_bits, refs[1]->get_hash(0).as_bitslice().get_ptr(), hash_bits) !=
          0) {
        return td::Status::Error("Second hash mismatch in a MerkleProof special cell");
      }
      if (td::bitstring::bits_load_ulong(data + 8 + 2 * hash_bits, depth_bytes * 8) != refs[0]->get_depth(0)) {
        return td::Status::Error("First depth mismatch in a MerkleProof special cell");
      }
      if (td::bitstring::bits_load_ulong(data + 8 + 2 * hash_bits + depth_bytes * 8, depth_bytes * 8) !=
          refs[1]->get_depth(0)) {
        return td::Status::Error("Second depth mismatch in a MerkleProof special cell");
      }

      level_mask = refs[0]->get_level_mask().apply_or(refs[1]->get_level_mask()).shift_right();
      virtualization = td::max(refs[0]->get_virtualization(), refs[1]->get_virtualization());
      break;
    }

    default:
      return td::Status::Error("Unknown special cell type");
  }

  /// 8 байт
  Info info;
  if (td::unlikely(bits > max_bits)) {
    return td::Status::Error("Too many bits");
  }
  if (td::unlikely(refs.size() > max_refs)) {
    return td::Status::Error("Too many cell references");
  }
  if (td::unlikely(virtualization > max_virtualization)) {
    return td::Status::Error("Too big virtualization");
  }

  CHECK(level_mask.get_level() <= max_level);

  auto hash_count = type == SpecialType::PrunnedBranch ? 1 : level_mask.get_hashes_count();
  DCHECK(hash_count <= max_level + 1);

  info.bits_ = bits;
  info.refs_count_ = refs.size() & 7;
  info.is_special_ = special;
  info.level_mask_ = level_mask.get_mask() & 7;
  info.hash_count_ = hash_count & 7;
  info.virtualization_ = virtualization & 7;

  /// [PERF] 5.53%
  auto data_cell = create_empty_data_cell(info);
  auto* storage = data_cell->get_storage();

  // init data
  auto* data_ptr = info.get_data(storage);
  td::BitPtr{data_ptr}.copy_from(data, bits);
  // prepare for serialization
  if (bits & 7) {
    int m = (0x80 >> (bits & 7));
    unsigned l = bits / 8;
    data_ptr[l] = static_cast<unsigned char>((data_ptr[l] & -m) | m);
  }

  // init refs
  auto refs_ptr = info.get_refs(storage);
  for (size_t i = 0; i < refs.size(); i++) {
    refs_ptr[i] = refs[i].release();
  }

  // init hashes and depth
  auto* hashes_ptr = info.get_hashes(storage);
  auto* depth_ptr = info.get_depth(storage);

  // NB: be careful with special cells
  auto total_hash_count = level_mask.get_hashes_count();
  auto hash_i_offset = total_hash_count - hash_count;

  /// Делаем вычисление за одну операцию, это быстрее и для SHA256_CTX, и digest::SHA256
  static ABSL_CACHELINE_ALIGNED TD_THREAD_LOCAL uint8_t buffer[1024] = {};
  for (td::uint32 level_i = 0, hash_i = 0, level = level_mask.get_level(); level_i <= level; level_i++) {
    if (!level_mask.is_significant(level_i)) {
      continue;
    }
    SCOPE_EXIT {
      hash_i++;
    };
    if (hash_i < hash_i_offset) {
      continue;
    }

    buffer[0] = info.d1(level_mask.apply(level_i));
    buffer[1] = info.d2();
    size_t buffer_size = 2;

    if (hash_i == hash_i_offset) {
      DCHECK(level_i == 0 || type == SpecialType::PrunnedBranch);
      memcpy(buffer + 2, data_ptr, (bits + 7) >> 3);
      buffer_size += (bits + 7) >> 3;
    } else {
      DCHECK(level_i != 0 && type != SpecialType::PrunnedBranch);
      memcpy(buffer + 2, hashes_ptr[hash_i - hash_i_offset - 1].as_slice().ubegin(), sizeof(Hash));
      buffer_size += sizeof(Hash);
    }

    auto dest_i = hash_i - hash_i_offset;
    const auto level_i_val = (type == SpecialType::MerkleProof || type == SpecialType::MerkleUpdate) ? level_i + 1 : level_i;

    // calc depth
    td::uint16 depth = 0;
    for (int i = 0; i < info.refs_count_; i++) {
      const td::uint16 child_depth = refs_ptr[i]->get_depth(level_i_val);

      // add depth into hash
      store_depth(buffer + buffer_size, child_depth);
      buffer_size += depth_bytes;

      depth = std::max(depth, child_depth);
    }
    if (info.refs_count_ != 0) {
      if (depth >= max_depth) {
        return td::Status::Error("Depth is too big");
      }
      depth++;
    }
    depth_ptr[dest_i] = depth;

    // children hash
    for (int i = 0; i < info.refs_count_; ++i) {
      assert(buffer_size + sizeof(Hash) < std::size(buffer));
      memcpy(buffer + buffer_size, refs_ptr[i]->get_hash(level_i_val).as_slice().ubegin(), sizeof(Hash));
      buffer_size += sizeof(Hash);
    }

    /// Хешер для процессоров Intel
#ifdef TON_EXPERIMENT_ISAL_CRYPTO
    static ABSL_ATTRIBUTE_FUNC_ALIGN(16) TD_THREAD_LOCAL HasherSha256Isal hasherIsal;
    if (hasherIsal.is_ok()) {
      const int rc = hasherIsal.sha256(buffer, buffer_size, hashes_ptr[dest_i].as_slice().ubegin());
      DCHECK(0 == rc);
    }
#endif

#ifdef TON_EXPERIMENT_SHANI
    static bool has_shani = hasSHANI();

    if (has_shani) {
      sha256_update_shani(buffer, buffer_size, hashes_ptr[dest_i].as_slice().ubegin());
    } else
#endif

    /// В зависимости от размера блока данных выбираем hasher:
    /// SHA256_CTX намного быстрее для малых данных чем digest::SHA256
    /// И под Win32 и под Linux.
    if (buffer_size < 512) {
      static ABSL_ATTRIBUTE_FUNC_ALIGN(16) TD_THREAD_LOCAL SHA256_CTX ctx;
      SHA256_Init(&ctx);
      SHA256_Update(&ctx, buffer, buffer_size);
      SHA256_Final(hashes_ptr[dest_i].as_slice().ubegin(), &ctx);
    } else {
      static TD_THREAD_LOCAL digest::SHA256* hasher;
      td::init_thread_local<digest::SHA256>(hasher);
      hasher->reset();
      hasher->feed(buffer, buffer_size);
      auto extracted_size = hasher->extract(hashes_ptr[dest_i].as_slice());
      DCHECK(extracted_size == hash_bytes);
    }
  }

  return Ref<DataCell>(data_cell.release(), Ref<DataCell>::acquire_t{});
}

/// 24,100,000 на всех блоках! Почему так много??? - потому, что создается 7.5М объектов DataCell и у каждого 3 level hash
const DataCell::Hash& DataCell::do_get_hash(td::uint32 level) const {
#if !defined(NDEBUG) && 0
  {
    static size_t count = 0;
    ++count;
    if (count % 1000 == 0)
    ::OutputDebugStringA(std::format("[DataCell::do_get_hash] {}\n", count).c_str());
  }
#endif
  auto hash_i = get_level_mask().apply(level).get_hash_i();
  if (special_type() == SpecialType::PrunnedBranch) {
    auto this_hash_i = get_level_mask().get_hash_i();
    if (hash_i != this_hash_i) {
      return reinterpret_cast<const Hash*>(info_.get_data(get_storage()) + 2)[hash_i];
    }
    hash_i = 0;
  }
  return info_.get_hashes(get_storage())[hash_i];
}

td::uint16 DataCell::do_get_depth(td::uint32 level) const {
  auto hash_i = get_level_mask().apply(level).get_hash_i();
  if (special_type() == SpecialType::PrunnedBranch) {
    auto this_hash_i = get_level_mask().get_hash_i();
    if (hash_i != this_hash_i) {
      return load_depth(info_.get_data(get_storage()) + 2 + hash_bytes * this_hash_i + hash_i * depth_bytes);
    }
    hash_i = 0;
  }
  return info_.get_depth(get_storage())[hash_i];
}

int DataCell::serialize(unsigned char* buff, int buff_size, bool with_hashes) const {
  int len = get_serialized_size(with_hashes);
  if (len > buff_size) {
    return 0;
  }
  buff[0] = static_cast<unsigned char>(info_.d1() | (with_hashes * 16));
  buff[1] = info_.d2();
  int hs = 0;
  if (with_hashes) {
    hs = (get_level_mask().get_hashes_count()) * (hash_bytes + depth_bytes);
    assert(len >= 2 + hs);
    std::memset(buff + 2, 0, hs);
    auto dest = td::MutableSlice(buff + 2, hs);
    auto level = get_level();
    // TODO: optimize for prunned brandh
    for (unsigned i = 0; i <= level; i++) {
      if (!get_level_mask().is_significant(i)) {
        continue;
      }
      dest.copy_from(get_hash(i).as_slice());
      dest.remove_prefix(hash_bytes);
    }
    for (unsigned i = 0; i <= level; i++) {
      if (!get_level_mask().is_significant(i)) {
        continue;
      }
      store_depth(dest.ubegin(), get_depth(i));
      dest.remove_prefix(depth_bytes);
    }
    // buff[2] = 0;  // for testing hash verification in deserialization
    buff += hs;
    len -= hs;
  }
  std::memcpy(buff + 2, get_data(), len - 2);
  return len + hs;
}

std::string DataCell::serialize() const {
  unsigned char buff[max_serialized_bytes];
  int len = serialize(buff, sizeof(buff));
  return std::string(buff, buff + len);
}

std::string DataCell::to_hex() const {
  unsigned char buff[max_serialized_bytes];
  int len = serialize(buff, sizeof(buff));
  char hex_buff[max_serialized_bytes * 2 + 1];
  for (int i = 0; i < len; i++) {
    snprintf(hex_buff + 2 * i, sizeof(hex_buff) - 2 * i, "%02x", buff[i]);
  }
  return hex_buff;
}

std::ostream& operator<<(std::ostream& os, const DataCell& c) {
  return os << c.to_hex();
}

}  // namespace vm
