/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

// Test-only equality overrides discovered by td::actor::events_equal via ADL.
// Production types in this namespace intentionally do not define operator==
// (most carry td::Ref/td::BufferSlice fields where bitwise/identity equality
// would be misleading). The overrides below give the test harness the
// content-based comparison it needs without leaking into production code:
// the test binaries are the only TUs that include this header.

#include <memory>

#include "td/actor/BusUtils.h"
#include "td/utils/buffer.h"
#include "validator/consensus/bus.h"
#include "validator/consensus/simplex/certificate.h"
#include "validator/consensus/simplex/votes.h"
#include "validator/consensus/stats.h"
#include "validator/consensus/types.h"

namespace ton {

inline bool events_equal_tag(const BlockCandidate& a, const BlockCandidate& b) {
  return a.pubkey == b.pubkey && a.id == b.id && a.collated_file_hash == b.collated_file_hash &&
         a.data.as_slice() == b.data.as_slice() && a.collated_data.as_slice() == b.collated_data.as_slice();
}

}  // namespace ton

namespace ton::validator::consensus {

inline bool events_equal_tag(const ProtocolMessage& a, const ProtocolMessage& b) {
  return a.data.as_slice() == b.data.as_slice();
}

inline bool events_equal_tag(const Candidate& a, const Candidate& b) {
  if (a.id != b.id || a.parent_id != b.parent_id || a.leader != b.leader ||
      a.signature.as_slice() != b.signature.as_slice() || a.block.index() != b.block.index()) {
    return false;
  }
  return std::visit(
      [&]<typename U>(const U& va) -> bool { return td::actor::events_equal(va, std::get<U>(b.block)); }, a.block);
}

namespace stats {

// Event is a virtual hierarchy with no value-equality. Tests construct fresh
// instances on each side of the comparison, so pointer identity wouldn't work.
// Round-trip through to_string() — equivalent to what test failure messages
// already display.
inline bool events_equal_tag(const Event& a, const Event& b) {
  return a.to_string() == b.to_string();
}

}  // namespace stats

namespace simplex {

template <ValidVote T>
bool events_equal_tag(const Certificate<T>& a, const Certificate<T>& b) {
  if (!(a.vote == b.vote) || a.signatures.size() != b.signatures.size()) {
    return false;
  }
  for (size_t i = 0; i < a.signatures.size(); ++i) {
    if (a.signatures[i].validator != b.signatures[i].validator ||
        a.signatures[i].signature.as_slice() != b.signatures[i].signature.as_slice()) {
      return false;
    }
  }
  return true;
}

}  // namespace simplex

}  // namespace ton::validator::consensus
