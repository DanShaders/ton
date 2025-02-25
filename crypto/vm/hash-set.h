#pragma once

#include "cells/CellHash.h"

namespace vm {

struct HashSet {
private:
	struct alignas(8) Element {
		vm::CellHash h;
		int nxt;
		Element(const vm::CellHash &h, int nxt): h(h), nxt(nxt) {}
	};
	std::vector<int> buckets;
	std::vector<Element> hs;
	inline static uint64_t hash2int(const vm::CellHash &h) {
		uint64_t i;
		std::memcpy(&i, h.as_array().data(), sizeof(i));
		return i;
	}
	
	public:
	bool emplace(const vm::CellHash &h) {
		if(buckets.empty()) buckets.assign(128, -1);
		const uint64_t b = hash2int(h) & (buckets.size()-1);
		for(int i = buckets[b]; i != -1; i = hs[i].nxt)
			if(hs[i].h == h) return false;
		if(hs.size() >= buckets.size()) {
			hs.emplace_back(h, -1);
			buckets.assign(buckets.size()<<1, -1);
			const int H = (int) hs.size();
			const uint64_t m = buckets.size()-1;
			for(int i = 0; i < H; ++i) {
				const uint64_t b = (*(const uint64_t*) hs[i].h.as_array().data()) & m;
				hs[i].nxt = buckets[b];
				buckets[b] = i;
			}
		} else {
			hs.emplace_back(h, buckets[b]);
			buckets[b] = (int) hs.size() - 1;
		}
		return true;
	}

	bool count(const vm::CellHash &h) const {
		if(buckets.empty()) return false;
		const uint64_t b = hash2int(h) & (buckets.size()-1);
		for(int i = buckets[b]; i != -1; i = hs[i].nxt)
			if(hs[i].h == h) return true;
		return false;
	}

	size_t size() const {
		return hs.size();
	}

	void clear() {
		buckets.clear();
		hs.clear();
	}
};

template<typename T>
struct HashMap {
	static_assert((8 % alignof(T) == 0) || (alignof(T) % 8 == 0)); 
private:
	struct alignas(std::max(8, (int)alignof(T))) Element {
		vm::CellHash h;
		T v;
		int nxt;
		Element(const vm::CellHash &h, const T &v, int nxt): h(h), v(v), nxt(nxt) {}
	};
	std::vector<int> buckets;
	std::vector<Element> hs;
	inline static uint64_t hash2int(const vm::CellHash &h) {
		uint64_t i;
		std::memcpy(&i, h.as_array().data(), sizeof(i));
		return i;
	}

public:
	bool emplace(const vm::CellHash &h, const T &v) {
		if(buckets.empty()) buckets.assign(128, -1);
		const uint64_t b = hash2int(h) & (buckets.size()-1);
		for(int i = buckets[b]; i != -1; i = hs[i].nxt)
			if(hs[i].h == h) return false;
		if(hs.size() >= buckets.size()) {
			hs.emplace_back(h, v, -1);
			buckets.assign(buckets.size()<<1, -1);
			const int H = (int) hs.size();
			const uint64_t m = buckets.size()-1;
			for(int i = 0; i < H; ++i) {
				const uint64_t b = (*(const uint64_t*) (hs[i].h).as_array().data()) & m;
				hs[i].nxt = buckets[b];
				buckets[b] = i;
			}
		} else {
			hs.emplace_back(h, v, buckets[b]);
			buckets[b] = (int) hs.size() - 1;
		}
		return true;
	}

	T* find(const vm::CellHash &h) {
		if(buckets.empty()) return nullptr;
		const uint64_t b = hash2int(h) & (buckets.size()-1);
		for(int i = buckets[b]; i != -1; i = hs[i].nxt)
			if(hs[i].h == h) return &hs[i].v;
		return nullptr;
	}

	size_t size() const {
		return hs.size();
	}

	void clear() {
		buckets.clear();
		hs.clear();
	}
};

}