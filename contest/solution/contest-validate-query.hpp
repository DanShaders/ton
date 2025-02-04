#pragma once

#include "common/global-version.h"

#include "block-auto.h"
#include "shard.hpp"
#include "transaction.h"

#include "ton/ton-types.h"

#include "vm/dict.h"

#include "absl/container/inlined_vector.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"

namespace solution {

using namespace ton;

class ErrorCtxAdd;
struct ErrorCtx {
 protected:
  friend class ErrorCtxAdd;
  absl::InlinedVector<std::string, 8> entries_;

 public:
  ErrorCtx() = default;

  explicit ErrorCtx(absl::InlinedVector<std::string, 8> str_list) : entries_(std::move(str_list)) {
  }

  explicit ErrorCtx(std::string str) : entries_{std::move(str)} {
  }

  std::string as_string() const;
  ErrorCtxAdd add_guard(std::string str_add);
} __attribute__((aligned(128)));

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

class ContestValidateQuery final : public td::actor::Actor {
  static constexpr int supported_version() {
    return SUPPORTED_VERSION;
  }
  static constexpr long long supported_capabilities() {
    return ton::capCreateStatsEnabled | ton::capBounceMsgBody | ton::capReportVersion | ton::capShortDequeue |
           ton::capStoreOutMsgQueueSize | ton::capMsgMetadata | ton::capDeferMessages | ton::capFullCollatedData;
  }

 public:
  ContestValidateQuery(BlockIdExt block_id, td::BufferSlice block_data, td::BufferSlice collated_data,
                       td::Promise<td::BufferSlice> promise);

 private:
  void start_up() final;

  const ShardIdFull shard_;
  const BlockIdExt id_;
  td::BufferSlice block_data_;
  td::BufferSlice collated_data_;
  td::Promise<td::BufferSlice> main_promise_;
  td::Ref<vm::Cell> block_root_;
  td::Ref<vm::Cell> old_mparams_;

  ErrorCtx error_ctx_;

  ton::BlockIdExt mc_blkid_;
  ton::BlockSeqno mc_seqno_{0};
  std::vector<BlockIdExt> prev_blocks_;
  bool after_merge_{false};
  bool after_split_{false};
  bool before_split_{false};
  int global_id_{0};
  ton::BlockSeqno vert_seqno_{~0U};
  LogicalTime start_lt_;
  LogicalTime end_lt_;
  UnixTime now_{~0u};
  bool want_split_{false};
  bool want_merge_{false};
  bool is_key_block_{false};
  BlockSeqno prev_key_seqno_{~0u};
  ton::Bits256 rand_seed_;
  td::Bits256 created_by_;

  std::vector<td::Ref<ton::validator::ShardState>> prev_states_;

  int pending_{0};

  td::Ref<ton::validator::MasterchainStateQ> mc_state_;
  td::Ref<vm::Cell> mc_state_root_;

  absl::btree_map<RootHash, td::Ref<vm::Cell>> virt_roots_;

  std::unique_ptr<vm::Dictionary> top_shard_descr_dict_;

  absl::btree_map<BlockSeqno, td::Ref<ton::validator::MasterchainStateQ>> aux_mc_states_;

  block::gen::ExtraCollatedData::Record extra_collated_data_;

  std::unique_ptr<block::ConfigInfo> config_;
  std::unique_ptr<block::ShardConfig> new_shard_conf_;
  std::unique_ptr<block::BlockLimits> block_limits_;
  std::unique_ptr<block::BlockLimitStatus> block_limit_status_;

  bool msg_metadata_enabled_ = false;
  bool deferring_messages_enabled_ = false;
  bool store_out_msg_queue_size_ = false;

  block::ComputePhaseConfig compute_phase_cfg_;
  std::vector<block::StoragePrices> storage_prices_;
  block::StoragePhaseConfig storage_phase_cfg_{&storage_prices_};
  block::ActionPhaseConfig action_phase_cfg_;
  td::RefInt256 masterchain_create_fee_;
  td::RefInt256 basechain_create_fee_;
  td::Ref<block::WorkchainInfo> wc_info_;
  bool accept_msgs_{true};
  int stage_{0};
  int verbosity{0};

  td::Ref<vm::Cell> prev_state_root_;
  std::shared_ptr<vm::CellUsageTree> state_usage_tree_;  // used to construct Merkle update

  absl::InlinedVector<block::McShardDescr, 8> neighbors_;

  td::BufferSlice result_state_update_;

  block::ShardState ps_;
  std::unique_ptr<vm::AugmentedDictionary> sibling_out_msg_queue_;
  std::shared_ptr<block::MsgProcessedUptoCollection> sibling_processed_upto_;

  std::unique_ptr<vm::AugmentedDictionary> in_msg_dict_;
  std::unique_ptr<vm::AugmentedDictionary> out_msg_dict_;
  std::unique_ptr<vm::AugmentedDictionary> account_blocks_dict_;

  block::ValueFlow value_flow_;

  td::Ref<vm::Cell> recover_create_msg_;
  td::Ref<vm::Cell> mint_msg_;  // from McBlockExtra (UNCHECKED)
  td::RefInt256 import_fees_;
  block::CurrencyCollection import_created_;
  block::CurrencyCollection transaction_fees_;
  block::CurrencyCollection total_burned_{0};
  block::CurrencyCollection fees_burned_{0};
  td::BitArray<64> shard_pfx_;
  int shard_pfx_len_;

  absl::btree_map<std::pair<StdSmcAddress, td::uint64>, td::Ref<vm::Cell>> removed_dispatch_queue_messages_;
  absl::btree_map<std::pair<StdSmcAddress, td::uint64>, td::Ref<vm::Cell>> new_dispatch_queue_messages_;
  absl::btree_set<StdSmcAddress> account_expected_defer_all_messages_;
  td::uint64 processed_account_dispatch_queues_ = 0;
  td::uint64 old_out_msg_queue_size_ = 0;
  bool have_out_msg_queue_size_in_state_ = false;
  bool out_msg_queue_size_known_ = false;
  bool have_unprocessed_account_dispatch_queue_ = false;
  ton::LogicalTime proc_lt_{0}, claimed_proc_lt_{0}, min_enq_lt_{~0ULL};
  ton::Bits256 proc_hash_ = ton::Bits256::zero(), claimed_proc_hash_, min_enq_hash_;
  absl::InlinedVector<std::tuple<Bits256, LogicalTime, LogicalTime>, 64> msg_emitted_lt_;
  bool processed_upto_updated_{false};
  ton::LogicalTime max_shard_lt_{0};
  absl::InlinedVector<std::tuple<Bits256, LogicalTime, LogicalTime>, 64> msg_proc_lt_;
  td::uint64 total_gas_used_{0};
  td::uint64 total_special_gas_used_{0};
  ton::BlockSeqno min_shard_ref_mc_seqno_{~0U};

  block::ShardState ns_;

  std::string error_ctx() const;
  ErrorCtxAdd error_ctx_add_guard(std::string str);

  bool reject_query(std::string error);
  bool soft_reject_query(std::string error);

  bool fatal_error(td::Status error);
  bool fatal_error(int err_code, std::string err_msg);
  bool fatal_error(std::string err_msg, int err_code = -666);

  void finish_query();

  bool unpack_block_candidate();
  bool init_parse();
  bool extract_collated_data(std::vector<td::Ref<vm::Cell>>&& collated_roots);
  bool extract_collated_data_from(bool& have_extra_collated_data, td::Ref<vm::Cell> croot, int idx);

  td::Result<td::Ref<ton::validator::ShardState>> fetch_block_state(BlockIdExt block_id);
  td::Ref<vm::Cell> get_virt_state_root(td::Bits256 block_root_hash);

  void after_get_shard_state(int idx, td::Result<td::Ref<ton::validator::ShardState>> res);
  void after_get_mc_state(td::Result<td::Ref<ton::validator::ShardState>> res);
  bool process_mc_state(td::Ref<ton::validator::MasterchainState> mc_state);
  bool try_unpack_mc_state();
  bool fetch_config_params();
  bool register_mc_state(td::Ref<ton::validator::MasterchainStateQ> other_mc_state);
  bool check_this_shard_mc_info();
  bool try_validate();
  bool check_prev_block(const BlockIdExt& listed, const BlockIdExt& prev, bool chk_chain_len = true);
  bool check_prev_block_exact(const BlockIdExt& listed, const BlockIdExt& prev);
  bool compute_prev_state();
  bool request_neighbor_queues();
  void got_neighbor_out_queue(int i, td::Result<td::Ref<ton::validator::MessageQueue>> res);
  bool request_aux_mc_state(BlockSeqno seqno, td::Ref<ton::validator::MasterchainStateQ>& state);
  void after_get_aux_shard_state(ton::BlockIdExt blkid, td::Result<td::Ref<ton::validator::ShardState>> res);

  bool unpack_prev_state();
  bool check_utime_lt();
  bool init_next_state();
  bool prepare_out_msg_queue_size();
  bool fix_all_processed_upto();
  bool fix_one_processed_upto(block::MsgProcessedUpto& proc, ton::ShardIdFull owner, bool allow_cur = false);
  bool add_trivial_neighbor();
  bool unpack_block_data();
  bool precheck_account_transactions();
  bool build_new_message_queue();
  bool precheck_message_queue_update();
  bool unpack_dispatch_queue_update();
  bool check_in_msg_descr();
  bool check_out_msg_descr();
  bool check_dispatch_queue_update();
  bool check_processed_upto();
  bool check_in_queue();
  bool check_transactions();
  bool postcheck_account_updates();
  bool check_message_processing_order();
  bool check_new_state();
  bool postcheck_value_flow();
  bool build_state_update();
  bool add_trivial_neighbor_after_merge();
  bool unpack_precheck_value_flow(td::Ref<vm::Cell> value_flow_root);
  bool precheck_one_account_block(td::ConstBitPtr acc_id, td::Ref<vm::CellSlice> acc_blk);
  bool precheck_one_transaction(td::ConstBitPtr acc_id, ton::LogicalTime trans_lt, td::Ref<vm::CellSlice> trans_csr,
                                ton::Bits256& prev_trans_hash, ton::LogicalTime& prev_trans_lt,
                                unsigned& prev_trans_lt_len, ton::Bits256& acc_state_hash);
  bool precheck_one_message_queue_update(td::ConstBitPtr out_msg_id, td::Ref<vm::CellSlice> old_value,
                                         td::Ref<vm::CellSlice> new_value);
  bool check_account_dispatch_queue_update(td::Bits256 addr, td::Ref<vm::CellSlice> old_queue_csr,
                                           td::Ref<vm::CellSlice> new_queue_csr);
  bool check_in_msg(td::ConstBitPtr key, td::Ref<vm::CellSlice> in_msg);
  bool is_special_in_msg(const vm::CellSlice& in_msg) const;
  bool update_max_processed_lt_hash(ton::LogicalTime lt, const ton::Bits256& hash);
  bool update_min_enqueued_lt_hash(ton::LogicalTime lt, const ton::Bits256& hash);
  bool is_valid_transaction_ref(td::Ref<vm::Cell> trans_ref) const;
  td::Ref<vm::Cell> lookup_transaction(const ton::StdSmcAddress& addr, ton::LogicalTime lt) const;
  bool check_imported_message(td::Ref<vm::Cell> msg_env);
  bool check_out_msg(td::ConstBitPtr key, td::Ref<vm::CellSlice> out_msg);
  bool check_neighbor_outbound_message(td::Ref<vm::CellSlice> enq_msg, ton::LogicalTime lt, td::ConstBitPtr key,
                                       const block::McShardDescr& src_nb, bool& unprocessed, bool& processed_here,
                                       td::Bits256& msg_hash);
  bool fix_processed_upto(block::MsgProcessedUptoCollection& upto, bool allow_cur = false);
  bool check_account_transactions(const StdSmcAddress& acc_addr, td::Ref<vm::CellSlice> acc_tr);
  std::unique_ptr<block::Account> make_account_from(td::ConstBitPtr addr, td::Ref<vm::CellSlice> account);
  std::unique_ptr<block::Account> unpack_account(td::ConstBitPtr addr);
  bool check_one_transaction(block::Account& account, LogicalTime lt, td::Ref<vm::Cell> trans_root, bool is_first,
                             bool is_last);
  bool postcheck_one_account_update(td::ConstBitPtr acc_id, td::Ref<vm::CellSlice> old_value,
                                    td::Ref<vm::CellSlice> new_value);
  bool store_master_ref(vm::CellBuilder& cb);
  bool unpack_merge_prev_state();
  bool unpack_one_prev_state(block::ShardState& ss, BlockIdExt blkid, td::Ref<vm::Cell> prev_state_root);
  bool split_prev_state(block::ShardState& ss);
  td::Ref<ton::validator::MasterchainStateQ> get_aux_mc_state(BlockSeqno seqno) const;
};

}  // namespace solution
