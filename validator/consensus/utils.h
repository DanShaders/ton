/*
 * Copyright (c) 2025-2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include "interfaces/block.h"
#include "interfaces/external-message.h"
#include "td/actor/common.h"
#include "td/utils/Status.h"
#include "ton/ton-types.h"

#include "types.h"

namespace ton::validator::consensus {

td::Result<double> get_candidate_gen_utime_exact(const BlockCandidate& candidate);

td::Result<std::vector<td::Ref<ExtMessage>>> extract_accepted_externals(CandidateRef candidate);

}  // namespace ton::validator::consensus
