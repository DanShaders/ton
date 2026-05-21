/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include "consensus/chain-state.h"
#include "td/utils/buffer.h"

namespace ton::validator::consensus::test {

// A block produced by `extend`, carrying everything downstream layers need:
// its id, its parsed BlockData (for tests that hand it to consensus actors),
// the raw boc bytes (for assembling BlockCandidates), and the post-state cell.
struct BlockArtifact {
  BlockIdExt id;
  td::Ref<BlockData> data;
  td::BufferSlice raw_data;       // the boc serialization, kept so BlockCandidates can re-clone it
  td::Ref<vm::Cell> state_root;   // post-apply state cell
};

struct ExtendOptions {
  // Distinguishes otherwise-identical extensions of the same parent — used by
  // fork tests so two extensions produce two different block hashes.
  td::uint32 tag = 0;
  bool before_split = false;
  td::BufferSlice collated_data = {};
};

struct Extended {
  BlockArtifact block;
  ChainStateRef state;
};

// Build a zerostate over `shard`. The contained state cell carries no
// transactions — tests that need a particular initial state should construct
// a ChainState directly.
ChainStateRef zerostate(ShardIdFull shard, BlockIdExt min_mc_block_id = {});

// Produce one more block on top of `prev` and the corresponding ChainState.
// `prev` may be a zerostate or a normal tip. Each extension is deterministic
// in (prev, opts), so identical calls produce identical artifacts; bump
// `opts.tag` to fork.
Extended extend(ChainStateRef prev, ExtendOptions opts = {});

}  // namespace ton::validator::consensus::test
