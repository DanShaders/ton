#pragma once

#include <atomic>
#include <future>

using namespace ton;
using td::Ref;

class TransactionChecker {

public:

  explicit TransactionChecker();

  ~TransactionChecker() {
      if (worker.joinable()) {
          worker.join();
      }
  }

  bool is_special_in_msg(const vm::CellSlice& in_msg) const;
  void stop_and_wait();
  void stop();
  void init(UnixTime tm, 
            LogicalTime start_lt, 
            int verbosity_lvl, 
            bool after_merge, 
            bool before_split,
            bool msg_metadata_enabled, 
            bool deferring_messages_enabled,
            td::uint32 _block_limits_gas_hard,
            ShardIdFull shard,
            block::StoragePhaseConfig storage_phase_cfg,
            block::ComputePhaseConfig compute_phase_cfg,
            std::unique_ptr<block::BlockLimitStatus> block_limit_status,
            std::vector<std::tuple<Bits256, LogicalTime, LogicalTime>> msg_proc_lt,
            block::ActionPhaseConfig action_phase_cfg,
            std::set<StdSmcAddress> account_expected_defer_all_messages,
            std::unique_ptr<vm::AugmentedDictionary> ns_account_dict,
            std::unique_ptr<vm::AugmentedDictionary> ps_account_dict,
            block::CurrencyCollection total_burned
            );
  bool prepare(std::unique_ptr<vm::AugmentedDictionary> in_msg_dict, 
      std::unique_ptr<vm::AugmentedDictionary> out_msg_dict, 
      std::unique_ptr<vm::AugmentedDictionary> account_blocks_dict);
  bool wait();
  bool run();

  std::unique_ptr<vm::AugmentedDictionary> in_msg_dict_, out_msg_dict_, account_blocks_dict_;
  std::unique_ptr<vm::AugmentedDictionary> ps_account_dict_;
  std::unique_ptr<vm::AugmentedDictionary> ns_account_dict_;
  td::uint32 block_limits_gas_hard;
  LogicalTime start_lt_;
  bool msg_metadata_enabled_{false};
  bool deferring_messages_enabled_{false};
  bool after_merge_{false};
  bool before_split_{false};
  std::vector<std::tuple<Bits256, LogicalTime, LogicalTime>> msg_proc_lt_;
  block::StoragePhaseConfig storage_phase_cfg_;
  block::ComputePhaseConfig compute_phase_cfg_;
  std::unique_ptr<block::BlockLimitStatus> block_limit_status_;
  ShardIdFull shard_;
  block::ActionPhaseConfig action_phase_cfg_;
  std::set<StdSmcAddress> account_expected_defer_all_messages_;
  block::CurrencyCollection total_burned_{0};
  int verbosity{0};
  UnixTime now_{~0u};
private:
  std::thread worker;
  std::promise<bool> result_promise;
  std::future<bool> result_future;
  std::atomic<bool> stop_check_transactions{false};
  bool running{false};
  bool initialized{false};
  bool prepared{false};
  Ref<vm::Cell> recover_create_msg_, mint_msg_;  // from McBlockExtra (UNCHECKED)
  td::uint64 total_gas_used_{0}, total_special_gas_used_{0};
  bool check_transactions();
  bool reject_query(std::string error, td::BufferSlice reason = {});
  bool reject_query(std::string err_msg, td::Status error, td::BufferSlice reason = {});
  bool fatal_error(td::Status error);
  bool fatal_error(int err_code, std::string err_msg, td::Status error);
  bool fatal_error(std::string err_msg, int err_code = -666);
  bool fatal_error(int err_code, std::string err_msg);
  std::unique_ptr<block::Account> make_account_from(td::ConstBitPtr addr, Ref<vm::CellSlice> account);
  std::unique_ptr<block::Account> unpack_account(td::ConstBitPtr addr);
  bool check_account_transactions(const StdSmcAddress& acc_addr, Ref<vm::CellSlice> acc_tr);
  bool check_one_transaction(block::Account& account, ton::LogicalTime lt, Ref<vm::Cell> trans_root,
                                                 bool is_first, bool is_last);
  WorkchainId workchain() const {
    return shard_.workchain;
  }
};