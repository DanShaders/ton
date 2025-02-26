#pragma once

#include "interfaces/validator-manager.h"
#include "vm/cells.h"
#include "vm/dict.h"
#include "block/mc-config.h"
#include "block/transaction.h"
#include "shard.hpp"
#include "signature-set.hpp"
#include <vector>
#include <string>
#include <map>
#include "common/global-version.h"
#include "tonlib/tonlib/ExtClient.h"
#include "td/utils/buffer.h"
#include "AccountCheckActor.hpp"

namespace solution {

using namespace ton;
using namespace ton::validator;

using td::Ref;

class ErrorCtxAdd;
class ErrorCtxSet;
class ContestValidator;


struct ErrorCtx {
 protected:
  friend class ErrorCtxAdd;
  friend class ErrorCtxSet;
  std::vector<std::string> entries_;

 public:
  ErrorCtx() = default;
  ErrorCtx(std::vector<std::string> str_list) : entries_(std::move(str_list)) {
  }
  ErrorCtx(std::string str) : entries_{str} {
  }
  std::string as_string() const;
  ErrorCtxAdd add_guard(std::string str_add);
  ErrorCtxSet set_guard(std::string str);
  ErrorCtxSet set_guard(std::vector<std::string> str_list);
};

class ErrorCtxAdd {
  ErrorCtx& ctx_;

 public:
  ErrorCtxAdd(ErrorCtx& ctx, std::string ctx_elem) : ctx_(ctx) {
    ctx_.entries_.push_back(std::move(ctx_elem));
  }
  ~ErrorCtxAdd() {
    ctx_.entries_.pop_back();
  }
};

class ErrorCtxSet {
  ErrorCtx& ctx_;
  std::vector<std::string> old_ctx_;

 public:
  ErrorCtxSet(ErrorCtx& ctx, std::vector<std::string> new_ctx) : ctx_(ctx) {
    old_ctx_ = std::move(ctx_.entries_);
    ctx_.entries_ = std::move(new_ctx);
  }
  ErrorCtxSet(ErrorCtx& ctx, std::string new_ctx) : ErrorCtxSet(ctx, std::vector<std::string>{new_ctx}) {
  }
  ~ErrorCtxSet() {
    ctx_.entries_ = std::move(old_ctx_);
  }
};

inline ErrorCtxAdd ErrorCtx::add_guard(std::string str) {
  return ErrorCtxAdd(*this, std::move(str));
}

inline ErrorCtxSet ErrorCtx::set_guard(std::string str) {
  return ErrorCtxSet(*this, std::move(str));
}

inline ErrorCtxSet ErrorCtx::set_guard(std::vector<std::string> str_list) {
  return ErrorCtxSet(*this, std::move(str_list));
}

struct State {
  State(BlockIdExt block_id, td::BufferSlice block_data, td::BufferSlice collated_data,
                       td::Promise<td::BufferSlice> promise)
    : shard_(block_id.shard_full())
    , id_(block_id)
    , block_data(std::move(block_data))
    , collated_data(std::move(collated_data))
    , main_promise(std::move(promise))
    , shard_pfx_(shard_.shard)
    , shard_pfx_len_(ton::shard_prefix_length(shard_)) 
    , action_phase_cfg_() {
        collated_roots_.reserve(30);
        msg_proc_lt_.reserve(100);
        msg_emitted_lt_.reserve(100);
    }
  
  const ShardIdFull shard_;
  const td::BitArray<64> shard_pfx_;
  const int shard_pfx_len_;
  const block::ActionPhaseConfig action_phase_cfg_;
  const bool after_merge_{false};
  const bool before_split_{false};
  const block::ComputePhaseConfig compute_phase_cfg_;
  const bool deferring_messages_enabled_ = false;
  const bool msg_metadata_enabled_ = false;
  const UnixTime now_{~0u};
  const BlockIdExt id_;

  // KEK:: const candidates:
  int verbosity{0};
  bool after_split_{false};
  bool want_split_{false};
  bool want_merge_{false};
  bool is_key_block_{false};
  bool update_shard_cc_{false};
  bool prev_key_block_exists_{false};
  bool debug_checks_{false};
  bool outq_cleanup_partial_{false};
  bool have_extra_collated_data_ = false;
  bool ihr_enabled_{false};
  bool create_stats_enabled_{false};
  bool processed_upto_updated_{false};
  bool accept_msgs_{true};

  bool out_msg_queue_size_known_ = false;
  bool have_out_msg_queue_size_in_state_ = false;

  bool store_out_msg_queue_size_ = false;

  td::uint64 processed_account_dispatch_queues_ = 0;
  bool have_unprocessed_account_dispatch_queue_ = false;

  // KEK:: non-const fields:
  int pending{0};
  std::atomic<int> pending_account_check{0};
  std::vector<BlockIdExt> prev_blocks;
  std::vector<Ref<ShardState>> prev_states;
  td::BufferSlice block_data, collated_data;
  td::Promise<td::BufferSlice> main_promise;
  BlockSeqno prev_key_seqno_{~0u};
  int stage_{0};
  td::Bits256 created_by_;

  Ref<vm::Cell> prev_state_root_;
  std::shared_ptr<vm::CellUsageTree> state_usage_tree_;  // used to construct Merkle update

  ErrorCtx error_ctx_;

  td::Ref<MasterchainStateQ> mc_state_;
  td::Ref<vm::Cell> mc_state_root_;
  BlockIdExt mc_blkid_;
  ton::BlockSeqno mc_seqno_{0};

  Ref<vm::Cell> block_root_;
  std::vector<Ref<vm::Cell>> collated_roots_;
  std::map<RootHash, Ref<vm::Cell>> virt_roots_;
  std::unique_ptr<vm::Dictionary> top_shard_descr_dict_;
  block::gen::ExtraCollatedData::Record extra_collated_data_;

  std::unique_ptr<block::ConfigInfo> config_;
  std::unique_ptr<block::ShardConfig> old_shard_conf_;  // from reference mc state
  std::unique_ptr<block::ShardConfig> new_shard_conf_;  // from shard_hashes_ in mc blocks
  Ref<block::WorkchainInfo> wc_info_;
  Ref<vm::Cell> old_mparams_;

  ton::BlockSeqno min_shard_ref_mc_seqno_{~0U};
  ton::LogicalTime max_shard_lt_{0};

  int global_id_{0};
  ton::BlockSeqno vert_seqno_{~0U};
  ton::BlockSeqno prev_key_block_seqno_;
  ton::BlockIdExt prev_key_block_;
  ton::LogicalTime prev_key_block_lt_;
  std::unique_ptr<const block::BlockLimits> block_limits_;
  std::unique_ptr<block::BlockLimitStatus> block_limit_status_;

  LogicalTime end_lt_;

  ton::Bits256 rand_seed_;
  td::RefInt256 masterchain_create_fee_, basechain_create_fee_;

  std::vector<block::McShardDescr> neighbors_;
  std::map<BlockSeqno, Ref<MasterchainStateQ>> aux_mc_states_;

  std::unique_ptr<vm::AugmentedDictionary> sibling_out_msg_queue_;
  std::shared_ptr<block::MsgProcessedUptoCollection> sibling_processed_upto_;

  std::map<td::Bits256, int> block_create_count_;
  unsigned block_create_total_{0};

  std::unique_ptr<vm::AugmentedDictionary> account_blocks_dict_;
  block::ValueFlow value_flow_;
  block::CurrencyCollection import_created_, transaction_fees_, fees_burned_{0};
  td::RefInt256 import_fees_;

  ton::LogicalTime proc_lt_{0}, claimed_proc_lt_{0}, min_enq_lt_{~0ULL};
  ton::Bits256 proc_hash_ = ton::Bits256::zero(), claimed_proc_hash_, min_enq_hash_;

  std::vector<std::tuple<Bits256, LogicalTime, LogicalTime>> msg_emitted_lt_;

  std::map<std::pair<StdSmcAddress, td::uint64>, Ref<vm::Cell>> removed_dispatch_queue_messages_;
  std::map<std::pair<StdSmcAddress, td::uint64>, Ref<vm::Cell>> new_dispatch_queue_messages_;
  td::uint64 old_out_msg_queue_size_ = 0;

  std::vector<td::actor::ActorId<AccountCheckActor<ContestValidator>>> account_check_actors_;


  // KEK:: Account Check fields

  LogicalTime start_lt_;
  std::vector<block::StoragePrices> storage_prices_;
  block::StoragePhaseConfig storage_phase_cfg_{&storage_prices_};

  std::set<StdSmcAddress> account_expected_defer_all_messages_;
  std::unique_ptr<vm::AugmentedDictionary> in_msg_dict_, out_msg_dict_;

  Ref<vm::Cell> recover_create_msg_, mint_msg_;  // from McBlockExtra (UNCHECKED)
  
  std::vector<std::tuple<Bits256, LogicalTime, LogicalTime>> msg_proc_lt_;


  block::ShardState ps_;
  block::ShardState ns_;


  block::CurrencyCollection total_burned_{0};

  td::uint64 total_gas_used_{0}, total_special_gas_used_{0};

  td::BufferSlice result_state_update_;
};

}