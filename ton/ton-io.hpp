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

#include "td/utils/base64.h"

#include "ton-types.h"

namespace td {

inline td::StringBuilder &operator<<(td::StringBuilder &stream, const ton::Bits256 &x) {
  return stream << td::base64_encode(td::Slice(x.data(), x.size() / 8));
}

inline td::StringBuilder &operator<<(td::StringBuilder &stream, const ton::ShardIdFull &x) {
  return stream << x.to_str();
}

inline td::StringBuilder &operator<<(td::StringBuilder &stream, const ton::AccountIdPrefixFull &x) {
  return stream << x.to_str();
}

inline td::StringBuilder &operator<<(td::StringBuilder &stream, const ton::BlockId &x) {
  return stream << x.to_str();
}

inline td::StringBuilder &operator<<(td::StringBuilder &stream, const ton::BlockIdExt &x) {
  return stream << x.to_str();
}

inline td::StringBuilder &operator<<(td::StringBuilder &stream, const ton::BlockCandidate &x) {
  return stream << "BlockCandidate{id=" << x.id << ", block_size=" << x.data.size()
                << ", collated_size=" << x.collated_data.size() << ", collated_file_hash=" << x.collated_file_hash
                << ", pubkey=" << x.pubkey.as_bits256() << "}";
}

inline td::StringBuilder &operator<<(td::StringBuilder &stream, const ton::NewConsensusConfig::NoncriticalParams &x) {
  stream << "NoncriticalParams{";
  bool first = true;
  auto add_comma_if_not_first = [&]() {
    if (!first) {
      stream << ", ";
    }
    first = false;
  };
#define APPEND_PARAM(_, name, value) \
  add_comma_if_not_first();          \
  stream << #name << "=" << x.name;
#define APPEND_DURATION(_, name, value) \
  add_comma_if_not_first();             \
  stream << #name << "=" << x.name.count() << "ms";
  ENUMERATE_NONCRITICAL_PARAMS(APPEND_PARAM, APPEND_PARAM, APPEND_DURATION)
#undef APPEND_PARAM
#undef APPEND_DURATION
  return stream << "}";
}

}  // namespace td
