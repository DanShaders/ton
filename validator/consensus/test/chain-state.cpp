/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "validator/consensus/test/chain-state.h"

#include "common/checksum.h"
#include "crypto/block/fixtures.h"
#include "crypto/vm/boc.h"
#include "crypto/vm/cells/CellBuilder.h"
#include "validator/fabric.h"

namespace ton::validator::consensus::test {

namespace {

td::Ref<vm::Cell> make_state_cell(ShardIdFull shard, BlockSeqno seqno, td::uint32 tag) {
  // Distinguishable state cells per (shard, seqno, tag) so merkle updates
  // between successive seqnos are non-trivial, and forks (tag != 0) yield
  // different post-states.
  return vm::CellBuilder()
      .store_long(shard.workchain, 32)
      .store_long(shard.shard, 64)
      .store_long(seqno, 32)
      .store_long(tag, 32)
      .finalize_novm();
}

td::Ref<vm::Cell> zerostate_root(ShardIdFull shard) {
  return make_state_cell(shard, /*seqno=*/0, /*tag=*/0);
}

}  // namespace

ChainStateRef zerostate(ShardIdFull shard, BlockIdExt min_mc_block_id) {
  BlockIdExt zero_id{BlockId{shard, /*seqno=*/0}, /*root_hash=*/{}, /*file_hash=*/{}};
  return td::make_ref<ChainState>(ChainState::ZerostateTip{zero_id, zerostate_root(shard)}, min_mc_block_id);
}

Extended extend(ChainStateRef prev, ExtendOptions opts) {
  CHECK(prev.not_null());

  // Pull the things we need out of prev. `state()[0]` is the source root for
  // the merkle update; `block_ids()[0].shard_full()` carries the shard
  // through; `next_seqno()` gives us the seqno for the new block.
  auto prev_state_root = prev->state()[0];
  auto block_ids = prev->block_ids();
  CHECK(!block_ids.empty());
  ShardIdFull shard = block_ids[0].shard_full();
  BlockSeqno seqno = prev->next_seqno();

  auto new_state_root = make_state_cell(shard, seqno, opts.tag);
  auto state_update = block::test::make_merkle_update(prev_state_root, new_state_root);
  auto block_cell = block::test::make_block_cell(shard, seqno, std::move(state_update), opts.before_split);
  auto block_bytes = vm::std_boc_serialize(block_cell, 31).move_as_ok();

  BlockIdExt id{
      BlockId{shard, seqno},
      td::Bits256(block_cell->get_hash().bits()),
      td::sha256_bits256(block_bytes.as_slice()),
  };
  auto block_data = create_block(id, block_bytes.clone()).move_as_ok();

  // Build a temporary BlockCandidate solely to drive ChainState::apply. The
  // pubkey/collated_file_hash fields don't matter for state application — only
  // `id` and `data` do.
  BlockCandidate apply_candidate{
      Ed25519_PublicKey{}, id,
      /*collated_file_hash=*/td::sha256_bits256(opts.collated_data.as_slice()),
      block_bytes.clone(), opts.collated_data.clone(),
  };
  ChainStateRef new_state = prev->apply(apply_candidate);

  return Extended{
      BlockArtifact{
          .id = id,
          .data = std::move(block_data),
          .raw_data = std::move(block_bytes),
          .state_root = std::move(new_state_root),
      },
      std::move(new_state),
  };
}

}  // namespace ton::validator::consensus::test
