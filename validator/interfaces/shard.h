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

#include "crypto/common/refcnt.hpp"
#include "crypto/block/mc-config.h"
#include "ton/ton-shard.h"
#include "ton/ton-types.h"
#include "block.h"
#include "message-queue.h"
#include "vm/cells.h"

namespace ton {

namespace validator {

class ShardState : public td::CntObject {
 public:
  virtual ~ShardState() = default;

  virtual bool disable_boc() const = 0;

  virtual UnixTime get_unix_time() const = 0;
  virtual LogicalTime get_logical_time() const = 0;
  virtual td::int32 get_global_id() const = 0;
  virtual ShardIdFull get_shard() const = 0;
  virtual BlockSeqno get_seqno() const = 0;
  virtual BlockIdExt get_block_id() const = 0;
  virtual RootHash root_hash() const = 0;
  virtual td::Ref<vm::Cell> root_cell() const = 0;
  virtual td::optional<BlockIdExt> get_master_ref() const = 0;

  virtual td::Status validate_deep() const = 0;

  virtual bool before_split() const = 0;
  virtual td::Result<td::Ref<MessageQueue>> message_queue() const = 0;

  virtual td::Result<td::Ref<ShardState>> merge_with(const ShardState& with) const = 0;
};

class MasterchainState : virtual public ShardState {
 public:
  virtual ~MasterchainState() = default;

  virtual bool workchain_is_active(WorkchainId workchain_id) const = 0;
  virtual td::uint32 monitor_min_split_depth(WorkchainId workchain_id) const = 0;
  virtual td::uint32 min_split_depth(WorkchainId workchain_id) const = 0;
  virtual BlockSeqno min_ref_masterchain_seqno() const = 0;
  virtual bool ancestor_is_valid(BlockIdExt id) const = 0;
  virtual BlockIdExt last_key_block_id() const = 0;
  virtual BlockIdExt next_key_block_id(BlockSeqno seqno) const = 0;
  virtual BlockIdExt prev_key_block_id(BlockSeqno seqno) const = 0;
  virtual bool is_key_state() const = 0;
  virtual bool get_old_mc_block_id(ton::BlockSeqno seqno, ton::BlockIdExt& blkid,
                                   ton::LogicalTime* end_lt = nullptr) const = 0;
  virtual bool check_old_mc_block_id(const ton::BlockIdExt& blkid, bool strict = false) const = 0;
  virtual block::WorkchainSet get_workchain_list() const = 0;
  virtual td::Status prepare() {
    return td::Status::OK();
  }
  virtual block::SizeLimitsConfig::ExtMsgLimits get_ext_msg_limits() const = 0;
  virtual block::ImportedMsgQueueLimits get_imported_msg_queue_limits(bool is_masterchain) const = 0;
};

}  // namespace validator

}  // namespace ton
