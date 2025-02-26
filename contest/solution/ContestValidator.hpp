#pragma once

#include "interfaces/validator-manager.h"
#include "vm/cells.h"
#include "vm/dict.h"
#include "td/utils/CompileMacro.h"
#include "block/mc-config.h"
#include "block/transaction.h"
#include "shard.hpp"
#include "signature-set.hpp"
#include <vector>
#include <string>
#include <map>
#include "common/global-version.h"
#include "tonlib/tonlib/ExtClient.h"
#include "AccountCheckActor.hpp"
#include "WrappingMutex.hpp"
#include "State.hpp"

namespace solution {

using namespace ton;
using namespace ton::validator;

using td::Ref;

class ContestValidator : public td::actor::Actor {
  static constexpr int supported_version() {
    return SUPPORTED_VERSION;
  }
  static constexpr long long supported_capabilities() {
    return ton::capCreateStatsEnabled | ton::capBounceMsgBody | ton::capReportVersion | ton::capShortDequeue |
           ton::capStoreOutMsgQueueSize | ton::capMsgMetadata | ton::capDeferMessages | ton::capFullCollatedData;
  }

 public:
  ContestValidator(std::unique_ptr<State> state);

 public:
  std::unique_ptr<State> state_;

  WorkchainId workchain() const {
    return state_->shard_.workchain;
  }

  TD_FORCE_INLINE void finish_query();
  TD_FORCE_INLINE void abort_query(td::Status error);
  TD_FORCE_INLINE bool reject_query(std::string error, td::BufferSlice reason = {});
  TD_FORCE_INLINE bool reject_query(std::string err_msg, td::Status error, td::BufferSlice reason = {});
  TD_FORCE_INLINE bool soft_reject_query(std::string error, td::BufferSlice reason = {});
  TD_FORCE_INLINE void start_up() override;

  TD_FORCE_INLINE bool fatal_error(td::Status error);
  TD_FORCE_INLINE bool fatal_error(int err_code, std::string err_msg);
  TD_FORCE_INLINE bool fatal_error(int err_code, std::string err_msg, td::Status error);
  TD_FORCE_INLINE bool fatal_error(std::string err_msg, int err_code = -666);

  TD_FORCE_INLINE std::string error_ctx() const {
    return state_->error_ctx_.as_string();
  }

  TD_FORCE_INLINE ErrorCtxAdd error_ctx_add_guard(std::string str) {
    return state_->error_ctx_.add_guard(std::move(str));
  }
  TD_FORCE_INLINE ErrorCtxSet error_ctx_set_guard(std::string str) {
    return state_->error_ctx_.set_guard(std::move(str));
  }

  TD_FORCE_INLINE td::actor::ActorId<ContestValidator> get_self() {
    return actor_id(this);
  }

  TD_FORCE_INLINE td::Result<Ref<ShardState>> fetch_block_state(BlockIdExt block_id) {
    Ref<vm::Cell> state_root = get_virt_state_root(block_id.root_hash);
    if (state_root.is_null()) {
      return td::Status::Error(PSTRING() << "cannot get hash of state root: " << block_id.to_str());
    }
    td::Bits256 state_root_hash = state_root->get_hash().bits();
    auto it = state_->virt_roots_.find(state_root_hash);
    if (it == state_->virt_roots_.end()) {
      return td::Status::Error(PSTRING() << "cannot get state root from collated data: " << block_id.to_str());
    }
    TRY_RESULT(res, ShardStateQ::fetch(block_id, {}, it->second));
    return Ref<ShardState>(res);
  }

  TD_FORCE_INLINE void after_get_mc_state(td::Result<Ref<ShardState>> res);
  TD_FORCE_INLINE void after_get_shard_state(int idx, td::Result<Ref<ShardState>> res);
  TD_FORCE_INLINE bool process_mc_state(Ref<MasterchainState> mc_state);
  TD_FORCE_INLINE bool try_unpack_mc_state();
  TD_FORCE_INLINE bool fetch_config_params();
  TD_FORCE_INLINE bool check_prev_block(const BlockIdExt& listed, const BlockIdExt& prev, bool chk_chain_len = true);
  TD_FORCE_INLINE bool check_prev_block_exact(const BlockIdExt& listed, const BlockIdExt& prev);
  TD_FORCE_INLINE bool check_this_shard_mc_info();
  TD_FORCE_INLINE bool init_parse();
  TD_FORCE_INLINE bool unpack_block_candidate();
  TD_FORCE_INLINE bool extract_collated_data_from(Ref<vm::Cell> croot, int idx);
  TD_FORCE_INLINE bool extract_collated_data();
  TD_FORCE_INLINE bool try_validate();
  TD_FORCE_INLINE bool try_validate_after_account_check();
  TD_FORCE_INLINE bool compute_prev_state();
  TD_FORCE_INLINE bool unpack_merge_prev_state();
  TD_FORCE_INLINE bool unpack_prev_state();
  TD_FORCE_INLINE bool init_next_state();
  TD_FORCE_INLINE bool unpack_one_prev_state(block::ShardState& ss, BlockIdExt blkid, Ref<vm::Cell> prev_state_root);
  TD_FORCE_INLINE bool split_prev_state(block::ShardState& ss);
  TD_FORCE_INLINE bool request_neighbor_queues();
  TD_FORCE_INLINE void got_neighbor_out_queue(int i, td::Result<Ref<MessageQueue>> res);

  TD_FORCE_INLINE bool register_mc_state(Ref<MasterchainStateQ> other_mc_state);
  TD_FORCE_INLINE bool request_aux_mc_state(BlockSeqno seqno, Ref<MasterchainStateQ>& state);
  TD_FORCE_INLINE Ref<MasterchainStateQ> get_aux_mc_state(BlockSeqno seqno) const;
  TD_FORCE_INLINE void after_get_aux_shard_state(ton::BlockIdExt blkid, td::Result<Ref<ShardState>> res);

  TD_FORCE_INLINE bool check_utime_lt();
  TD_FORCE_INLINE bool prepare_out_msg_queue_size();
  TD_FORCE_INLINE void got_out_queue_size(size_t i, td::Result<td::uint64> res);

  TD_FORCE_INLINE bool fix_one_processed_upto(block::MsgProcessedUpto& proc, ton::ShardIdFull owner, bool allow_cur = false);
  TD_FORCE_INLINE bool fix_processed_upto(block::MsgProcessedUptoCollection& upto, bool allow_cur = false);
  TD_FORCE_INLINE bool fix_all_processed_upto();
  TD_FORCE_INLINE bool add_trivial_neighbor_after_merge();
  TD_FORCE_INLINE bool add_trivial_neighbor();
  TD_FORCE_INLINE bool unpack_block_data();
  TD_FORCE_INLINE bool unpack_precheck_value_flow(Ref<vm::Cell> value_flow_root);
  TD_FORCE_INLINE bool compute_minted_amount(block::CurrencyCollection& to_mint);
  TD_FORCE_INLINE bool postcheck_one_account_update(td::ConstBitPtr acc_id, Ref<vm::CellSlice> old_value, Ref<vm::CellSlice> new_value);
  TD_FORCE_INLINE bool postcheck_account_updates();
  TD_FORCE_INLINE bool precheck_one_transaction(td::ConstBitPtr acc_id, ton::LogicalTime trans_lt, Ref<vm::CellSlice> trans_csr,
                                ton::Bits256& prev_trans_hash, ton::LogicalTime& prev_trans_lt,
                                unsigned& prev_trans_lt_len, ton::Bits256& acc_state_hash);
  TD_FORCE_INLINE bool precheck_one_account_block(td::ConstBitPtr acc_id, Ref<vm::CellSlice> acc_blk);
  TD_FORCE_INLINE bool precheck_account_transactions();
  TD_FORCE_INLINE Ref<vm::Cell> lookup_transaction(const ton::StdSmcAddress& addr, ton::LogicalTime lt) const;
  TD_FORCE_INLINE bool is_valid_transaction_ref(Ref<vm::Cell> trans_ref) const;

  TD_FORCE_INLINE bool build_new_message_queue();
  TD_FORCE_INLINE bool precheck_one_message_queue_update(td::ConstBitPtr out_msg_id, Ref<vm::CellSlice> old_value,
                                         Ref<vm::CellSlice> new_value);
  TD_FORCE_INLINE bool precheck_message_queue_update();
  TD_FORCE_INLINE bool check_account_dispatch_queue_update(td::Bits256 addr, Ref<vm::CellSlice> old_queue_csr,
                                           Ref<vm::CellSlice> new_queue_csr);
  TD_FORCE_INLINE bool unpack_dispatch_queue_update();
  TD_FORCE_INLINE bool update_max_processed_lt_hash(ton::LogicalTime lt, const ton::Bits256& hash);
  TD_FORCE_INLINE bool update_min_enqueued_lt_hash(ton::LogicalTime lt, const ton::Bits256& hash);
  TD_FORCE_INLINE bool check_imported_message(Ref<vm::Cell> msg_env);
  TD_FORCE_INLINE bool is_special_in_msg(const vm::CellSlice& in_msg) const;
  TD_FORCE_INLINE bool check_in_msg(td::ConstBitPtr key, Ref<vm::CellSlice> in_msg);
  TD_FORCE_INLINE bool check_in_msg_descr();
  TD_FORCE_INLINE bool check_out_msg(td::ConstBitPtr key, Ref<vm::CellSlice> out_msg);
  TD_FORCE_INLINE bool check_out_msg_descr();
  TD_FORCE_INLINE bool check_dispatch_queue_update();
  TD_FORCE_INLINE bool check_processed_upto();
  TD_FORCE_INLINE bool check_neighbor_outbound_message(Ref<vm::CellSlice> enq_msg, ton::LogicalTime lt, td::ConstBitPtr key,
                                       const block::McShardDescr& src_nb, bool& unprocessed, bool& processed_here,
                                       td::Bits256& msg_hash);
  TD_FORCE_INLINE bool check_in_queue();
  TD_FORCE_INLINE bool check_transactions();
  TD_FORCE_INLINE bool check_message_processing_order();
  TD_FORCE_INLINE bool check_new_state();
  TD_FORCE_INLINE bool postcheck_value_flow();

  TD_FORCE_INLINE Ref<vm::Cell> get_virt_state_root(td::Bits256 block_root_hash);

  TD_FORCE_INLINE bool store_master_ref(vm::CellBuilder& cb);
  TD_FORCE_INLINE bool build_state_update();
};

}  // namespace solution

#include "ContestValidator.inc"