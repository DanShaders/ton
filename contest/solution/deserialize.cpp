#include "deserialize.hpp"

#include <openssl/sha.h>

#include "crypto/vm/cells/DataCell.h"
#include "profile.hpp"

struct CellSliceInfo {
	enum : uint32_t { boc_idx = 0x68ff65f3, boc_idx_crc32c = 0xacc3a728, boc_generic = 0xb5ee9c72 };

	uint64_t roots_offset, index_offset, data_offset, data_size;
	int root_count, cell_count;
	int ref_byte_size, offset_byte_size;
	bool has_index = true, has_cache_bits = false;

	CellSliceInfo(const uint8_t* data) {
		const uint32_t magic = (uint32_t)read_int(data, 4);
		const td::uint8 byte = data[4];
		if(magic == boc_generic) {
			has_index = (byte >> 7) % 2 == 1;
			has_cache_bits = (byte >> 5) % 2 == 1;
		}
		ref_byte_size = byte & 7;
		offset_byte_size = data[5];
		roots_offset = 6 + 3 * ref_byte_size + offset_byte_size;
		data += 6;
		cell_count = (int)read_ref(data);
		root_count = (int)read_ref(data + ref_byte_size);
		index_offset = roots_offset;
		if(magic == boc_generic) index_offset += (uint64_t)root_count * ref_byte_size;
		data_offset = index_offset;
		if(has_index) data_offset += (uint64_t)cell_count * offset_byte_size;
		data_size = read_offset(data + 3 * ref_byte_size);
	}

	uint64_t read_int(const uint8_t* ptr, uint32_t bytes) {
		uint64_t res = 0;
		while(bytes--) res = (res << 8) + *ptr++;
		return res;
	}
	uint64_t read_ref(const uint8_t* ptr) {
		return read_int(ptr, ref_byte_size);
	}
	uint64_t read_offset(const uint8_t* ptr) {
		return read_int(ptr, offset_byte_size);
	}
};

struct CellWithStorage : public vm::DataCell {
	using vm::DataCell::Info;
	constexpr static int STORAGE_SIZE = 228;
	inline static char* BIG_STORAGE = nullptr;
	inline static char* NEXT_STORAGE = nullptr;
	inline static size_t BIG_STORAGE_SIZE = 0;

	char* storage;
	CellWithStorage(const Info &info, char* n_storage):
		vm::DataCell(info), storage(NEXT_STORAGE) { NEXT_STORAGE = n_storage; }
	CellWithStorage(const CellWithStorage& other):
		vm::DataCell(other.info_), storage(other.storage) {}
	~CellWithStorage() {}
	const char* get_storage() const { return storage; }
	char* get_storage() { return storage; }
};
inline static CellWithStorage* CELLS = nullptr;
inline static CellWithStorage* NEXT_CELL = nullptr;
inline static size_t CELLS_SIZE = 0;
static void CLEAR_CELLS(const size_t cell_count) {
	const size_t desired_size = CellWithStorage::STORAGE_SIZE * cell_count;
	if(CellWithStorage::BIG_STORAGE_SIZE < desired_size) {
		if(CellWithStorage::BIG_STORAGE) ::operator delete[] (CellWithStorage::BIG_STORAGE, std::align_val_t(8));
		CellWithStorage::BIG_STORAGE = new(std::align_val_t(8)) char[desired_size];
		CellWithStorage::BIG_STORAGE_SIZE = desired_size;
	}
	CellWithStorage::NEXT_STORAGE = CellWithStorage::BIG_STORAGE;
	if(CELLS_SIZE < cell_count) {
		if(CELLS) free(CELLS);
		CELLS = (CellWithStorage*) malloc(cell_count * sizeof(CellWithStorage));
		CELLS_SIZE = cell_count;
	}
	NEXT_CELL = CELLS;
}

struct CellSerializationInfo {
	int data_offset;
	int data_len;
	int refs_offset;
	int refs_cnt;
	int end_offset;
	bool special;
	bool data_with_bits;

	CellSerializationInfo(const uint8_t* data, int ref_byte_size) {
		const td::uint8 d1 = data[0], d2 = data[1];
		refs_cnt = d1 & 7;
		special = (d1 & 8) != 0;
		const bool with_hashes = (d1 & 16) != 0;
		data_offset = 2;
		if(with_hashes) {
			const uint32_t n = vm::Cell::LevelMask(d1 >> 5).get_hashes_count();
			data_offset += n * (vm::Cell::hash_bytes+vm::Cell::depth_bytes);
		}
		data_len = (d2 >> 1) + (d2 & 1);
		data_with_bits = (d2 & 1) != 0;
		refs_offset = data_offset + data_len;
		end_offset = refs_offset + refs_cnt * ref_byte_size;
	}

	vm::Cell* create_data_cell(const uint8_t* cell_slice, const std::array<const vm::Cell*, 4> &refs) const {
		const uint8_t* const data = cell_slice + data_offset;
		const vm::Cell::SpecialType type = special ? static_cast<vm::Cell::SpecialType>(data[0])
										: vm::Cell::SpecialType::Ordinary;
		vm::Cell::LevelMask level_mask;
		switch(type) {
		case vm::Cell::SpecialType::Ordinary:
			for(int i = 0; i < refs_cnt; ++i)
				level_mask = level_mask.apply_or(refs[i]->get_level_mask());
			break;
		case vm::Cell::SpecialType::PrunnedBranch:
			level_mask = vm::Cell::LevelMask(data[1]);
			break;
		case vm::Cell::SpecialType::MerkleProof:
			level_mask = refs[0]->get_level_mask().shift_right();
			break;
		case vm::Cell::SpecialType::MerkleUpdate:
			level_mask = refs[0]->get_level_mask().apply_or(refs[1]->get_level_mask()).shift_right();
			break;
		default:
			break;
		}
		uint32_t hash_count, hash_i_offset;
		if(type == vm::Cell::SpecialType::PrunnedBranch) {
			hash_count = 1;
			hash_i_offset = level_mask.get_hashes_count() - 1;
		} else {
			hash_count = level_mask.get_hashes_count();
			hash_i_offset = 0;
		}
		CellWithStorage::Info info;
		info.bits_ = data_len * 8;
		if(data_with_bits) info.bits_ -= 1 + td::count_trailing_zeroes32(data[data_len - 1]);
		info.refs_count_ = refs_cnt & 0b111;
		info.is_special_ = special;
		info.level_mask_ = level_mask.get_mask() & 0b111;
		info.hash_count_ = hash_count & 0b111;
		info.virtualization_ = 0;
		// init data
		vm::Cell::Hash* hashes_ptr = (vm::Cell::Hash*) CellWithStorage::NEXT_STORAGE;
		const vm::Cell** refs_ptr = (const vm::Cell**) (hashes_ptr + hash_count);
		uint16_t* depth_ptr = (uint16_t*) (refs_ptr + refs_cnt);
		uint8_t* data_ptr = (uint8_t*) (depth_ptr + hash_count);
		std::memcpy(data_ptr, data, data_len);
		std::memcpy(refs_ptr, refs.data(), refs_cnt * sizeof(vm::Cell*));
		uint8_t tmp[2];
		tmp[1] = info.d2();
		for(td::uint32 level_i = 0, hash_i = 0, level = level_mask.get_level(); level_i <= level; ++level_i) {
			if(!level_mask.is_significant(level_i)) continue;
			if(hash_i < hash_i_offset) {
				++hash_i;
				continue;
			}
			const uint32_t dest_i = hash_i - hash_i_offset;
			tmp[0] = info.d1(level_mask.apply(level_i));
			#pragma GCC diagnostic push
			#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
			SHA256_CTX sha_ctx;
			SHA256_Init(&sha_ctx);
			SHA256_Update(&sha_ctx, tmp, 2);
			if(dest_i) SHA256_Update(&sha_ctx, hashes_ptr[dest_i - 1].as_array().begin(), vm::Cell::hash_bytes);
			else SHA256_Update(&sha_ctx, data_ptr, data_len);
			const uint32_t level_i_ref = (type == vm::Cell::SpecialType::MerkleProof || type == vm::Cell::SpecialType::MerkleUpdate) ? level_i + 1 : level_i;
			// calc depth
			uint16_t depth = 0;
			uint8_t child_depth_buf[8];
			for(int i = 0; i < refs_cnt; i++) {
				const uint16_t child_depth = refs_ptr[i]->get_depth(level_i_ref);
				child_depth_buf[(i<<1)] = (uint8_t) (child_depth>>8);
				child_depth_buf[(i<<1)+1] = (uint8_t) child_depth;
				depth = std::max(depth, child_depth);
			}
			SHA256_Update(&sha_ctx, child_depth_buf, vm::Cell::depth_bytes * refs_cnt);
			if(refs_cnt) ++depth;
			depth_ptr[dest_i] = depth;
			// children hash
			for(int i = 0; i < refs_cnt; i++)
				SHA256_Update(&sha_ctx, refs_ptr[i]->get_hash(level_i_ref).as_array().begin(), vm::Cell::hash_bytes);
			SHA256_Final(const_cast<uint8_t*>(hashes_ptr[dest_i].as_array().begin()), &sha_ctx);
			#pragma GCC diagnostic pop
			++hash_i;
		}
		uintptr_t data_end = (uintptr_t) data_ptr + data_len;
		if(data_end&7) data_end += 8-(data_end&7);
		return new(NEXT_CELL++) CellWithStorage(info, reinterpret_cast<char*>(data_end));
	}
};

std::vector<td::Ref<vm::Cell>> deserialize(const td::Slice& data) {
	// PROFILER("deserialize");
	CellSliceInfo info(data.ubegin());
	const uint8_t* index_ptr = nullptr;
	const uint8_t* const cells_ptr = data.ubegin() + info.data_offset;
	std::vector<uint64_t> custom_index;
	if(info.has_index) {
		index_ptr = data.ubegin() + info.index_offset;
	} else {
		uint64_t cur = 0;
		custom_index.resize(info.cell_count);
		for(int i = 0; i < info.cell_count; ++i) {
			custom_index[i] = cur;
			CellSerializationInfo cell_info(cells_ptr + cur, info.ref_byte_size);
			cur += cell_info.end_offset;
		}
	}

	CLEAR_CELLS(info.cell_count);
	std::vector<std::pair<vm::Cell*, int>> cell_list(info.cell_count);
	const auto get_idx_entry = [&](int index)->uint64_t {
		uint64_t raw;
		if(info.has_index) {
			if(!index) return 0;
			raw = info.read_offset(index_ptr + (long)(index-1) * info.offset_byte_size);
		} else raw = custom_index[index];
		if(info.has_cache_bits) raw /= 2;
		return raw;
	};
	for(int i = info.cell_count-1; i >= 0; --i) {
		const uint8_t* const cell_ptr = cells_ptr + get_idx_entry(i);
		std::array<const vm::Cell*, 4> refs;
		CellSerializationInfo cell_info(cell_ptr, info.ref_byte_size);
		for(int k = 0; k < cell_info.refs_cnt; k++) {
			auto &[c, r] = cell_list[info.read_ref(cell_ptr + cell_info.refs_offset + k * info.ref_byte_size)];
			refs[k] = c;
			++ r;
		}
		cell_list[i].first = cell_info.create_data_cell(cell_ptr, refs);
	}

	std::vector<td::Ref<vm::Cell>> roots(info.root_count);
	const uint8_t* roots_ptr = data.substr(info.roots_offset).ubegin();
	for(int i = 0; i < info.root_count; ++i) {
		auto &[c, r] = cell_list[info.read_ref(roots_ptr)];
		roots_ptr += info.ref_byte_size;
		roots[i] = td::Ref<vm::Cell>(c, td::Ref<vm::Cell>::acquire_t{});
		++ r;
	}
	for(const auto &[c, r] : cell_list) if(r)
		td::Ref<vm::Cell>::acquire_shared(c, r);
	return roots;
}
