#pragma once

#include "td/actor/actor.h"
#include "td/actor/ActorId.h"
#include "td/actor/core/Scheduler.h"
#include "td/utils/OptionParser.h"
#include "td/utils/Time.h"
#include "td/utils/logging.h"
#include "td/utils/SpinLock.h"
#include "td/utils/port/signals.h"
#include "td/utils/format.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/MultiPromise.h"
#include "td/utils/Status.h"

#include <vector>
#include <memory>
#include <iostream>

#include "perf/perf.hpp"
#include "top-shard-descr.hpp"
#include "validator-set.hpp"
#include "adnl/utils.hpp"
#include "ton/ton-tl.hpp"
#include "ton/ton-io.hpp"
#include "vm/boc.h"
#include "block/block-db.h"
#include "block/block.h"
#include "block/block-parse.h"
#include "block/block-auto.h"
#include "block/output-queue-merger.h"
#include "vm/cells/MerkleProof.h"
#include "vm/cells/MerkleUpdate.h"
#include "common/errorlog.h"
#include "fabric.h"
#include "perf/perf.hpp"
#include "third-party/rocksdb/port/likely.h"
#include <ctime>

DEFINE_PERF_EVENT(CHECK_ACCOUNT_TRANSACTIONS);

DEFINE_PERF_EVENT(XX_ACCOUNT_UNPACK);
DEFINE_PERF_EVENT(XX_ACCOUNT_CHECK_TRANSACTIONS);
DEFINE_PERF_EVENT(XX_ACCOUNT_CHECK_INVARIANTS);
DEFINE_PERF_EVENT(XX_ACCOUNT_HASH_CHECK);

DEFINE_PERF_EVENT(XX_TRANSACTION_CHECK_IN_MSG);
DEFINE_PERF_EVENT(XX_TRANSACTION_CHECK_OUT_MSGS);
DEFINE_PERF_EVENT(XX_TRANSACTION_CHECK_GENERAL_DATA);
DEFINE_PERF_EVENT(XX_TRANSACTION_CHECK_ACCOUNT_STATE);
DEFINE_PERF_EVENT(XX_TRANSACTION_CHECK_TYPE_SPECIFIC_DATA);
DEFINE_PERF_EVENT(XX_TRANSACTION_CHECK_TRANSACTION_COMPUTATION);
DEFINE_PERF_EVENT(XX_TRANSACTION_COMMIT_TRANSACTION);
DEFINE_PERF_EVENT(XX_TRANSACTION_COMPARE_TRANSACTIONS);

namespace solution {

using namespace ton;
using namespace ton::validator;

using td::Ref;

template<class ContestValidatorType>
class AccountCheckActor : public td::actor::Actor {
 public:
  AccountCheckActor(ContestValidatorType* state, const StdSmcAddress& acc_addr, Ref<vm::CellSlice> acc_blk_root,
                    td::Promise<bool> promise)
      : validator_(state), acc_addr_(acc_addr), acc_blk_root_(std::move(acc_blk_root)), promise_(std::move(promise)) {
  }

  ~AccountCheckActor() override = default;

  void start_up() override {
    promise_.set_value(check_account_transactions());
    stop();
  }

  bool check_account_transactions() {
    // std::cout << "start check_account_transactions for address: " << acc_addr_.to_hex() << std::endl;

    auto unpack_timer = CREATE_PERF_TIMER(XX_ACCOUNT_UNPACK);
    block::gen::AccountBlock::Record acc_blk;
    CHECK(tlb::csr_unpack(std::move(acc_blk_root_), acc_blk) && acc_blk.account_addr == acc_addr_);

    std::unique_ptr<block::Account> account_p;
    {
        account_p = unpack_account(acc_addr_.cbits());
    }



    if (!account_p) {
      return false;
    }

    auto& account = *account_p;
    CHECK(account.addr == acc_addr_);
    unpack_timer.stop();


    auto check_transactions_timer = CREATE_PERF_TIMER(XX_ACCOUNT_CHECK_TRANSACTIONS);
    vm::AugmentedDictionary trans_dict{vm::DictNonEmpty(), std::move(acc_blk.transactions), 64,
                                       block::tlb::aug_AccountTransactions};


    td::BitArray<64> min_trans, max_trans;
    CHECK(trans_dict.get_minmax_key(min_trans).not_null() && trans_dict.get_minmax_key(max_trans, true).not_null());
    ton::LogicalTime min_trans_lt = min_trans.to_ulong(), max_trans_lt = max_trans.to_ulong();
    if (!trans_dict.check_for_each_extra(
            [this, &account, min_trans_lt, max_trans_lt](Ref<vm::CellSlice> value, Ref<vm::CellSlice> extra,
                                                         td::ConstBitPtr key, int key_len) {
              CHECK(key_len == 64);
              ton::LogicalTime lt = key.get_uint(64);
              extra.clear();
              return check_one_transaction(account, lt, value->prefetch_ref(), lt == min_trans_lt, lt == max_trans_lt);
            })) {
      return false;
    }
    check_transactions_timer.stop();

    auto check_account_invariants_timer = CREATE_PERF_TIMER(XX_ACCOUNT_CHECK_INVARIANTS);
    // See Collator::combine_account_trabsactions
    if (account.total_state->get_hash() != account.orig_total_state->get_hash()) {
      // account changed

      auto lock = account_dict_mutex_.lock();
      if (account.orig_status == block::Account::acc_nonexist) {
        // account created
        CHECK(account.status != block::Account::acc_nonexist);
        vm::CellBuilder cb;


        if (!(cb.store_ref_bool(account.total_state)             // account_descr$_ account:^Account
              && cb.store_bits_bool(account.last_trans_hash_)    // last_trans_hash:bits256
              && cb.store_long_bool(account.last_trans_lt_, 64)  // last_trans_lt:uint64
              && validator_->state_->ns_.account_dict_->set_builder(account.addr, cb, vm::Dictionary::SetMode::Add))) {
          return false;
        }
      } else if (account.status == block::Account::acc_nonexist) {
        // account deleted
        if (validator_->state_->ns_.account_dict_->lookup_delete(account.addr).is_null()) {
          return false;
        }
      } else {
        // existing account modified
        vm::CellBuilder cb;
        if (!(cb.store_ref_bool(account.total_state)             // account_descr$_ account:^Account
              && cb.store_bits_bool(account.last_trans_hash_)    // last_trans_hash:bits256
              && cb.store_long_bool(account.last_trans_lt_, 64)  // last_trans_lt:uint64
              && validator_->state_->ns_.account_dict_->set_builder(account.addr, cb, vm::Dictionary::SetMode::Replace))) {
          return false;
        }
      }
    }
    check_account_invariants_timer.stop();

    auto hash_check_timer = CREATE_PERF_TIMER(XX_ACCOUNT_HASH_CHECK);
    block::gen::HASH_UPDATE::Record hash_upd;
    if (!tlb::type_unpack_cell(std::move(acc_blk.state_update), block::gen::t_HASH_UPDATE_Account, hash_upd)) {
      return false;
    }
    block::tlb::ShardAccount::Record old_state, new_state;
    {
      auto lock = account_dict_mutex_.lock();
      if (!(old_state.unpack(validator_->state_->ps_.account_dict_->lookup(account.addr)) &&
            new_state.unpack(validator_->state_->ns_.account_dict_->lookup(account.addr)))) {
        return false;
      }
    }
    if (hash_upd.old_hash != old_state.account->get_hash().bits()) {
      return false;
    }
    if (hash_upd.new_hash != new_state.account->get_hash().bits()) {
      return false;
    }
    hash_check_timer.stop();

    // std::cout << "finish check_account_transactions for address: " << acc_addr_.to_hex() << std::endl;
    return true;
  }

  bool check_one_transaction(block::Account& account, ton::LogicalTime lt, Ref<vm::Cell> trans_root, bool is_first,
                             bool is_last) {
    LOG(DEBUG) << "checking transaction " << lt << " of account " << account.addr.to_hex();

    const StdSmcAddress& addr = account.addr;
    block::gen::Transaction::Record trans;
    block::gen::HASH_UPDATE::Record hash_upd;
    CHECK(tlb::unpack_cell(trans_root, trans) &&
          tlb::type_unpack_cell(std::move(trans.state_update), block::gen::t_HASH_UPDATE_Account, hash_upd));
    auto in_msg_root = trans.r1.in_msg->prefetch_ref();
    bool external{false}, ihr_delivered{false}, need_credit_phase{false};
    // check input message
    block::CurrencyCollection money_imported(0), money_exported(0);
    bool is_special_tx = false;  // recover/mint transaction
    auto td_cs = vm::load_cell_slice(trans.description);
    int tag = block::gen::t_TransactionDescr.get_tag(td_cs);
    CHECK(tag >= 0);  // we have already validated the serialization of all Transactions
    td::optional<block::MsgMetadata> in_msg_metadata;

    if (in_msg_root.not_null()) {
      auto check_in_msg_timer = CREATE_PERF_TIMER(XX_TRANSACTION_CHECK_IN_MSG);
      auto in_descr_cs = validator_->state_->in_msg_dict_->lookup(in_msg_root->get_hash().as_bitslice());
      if (in_descr_cs.is_null()) {
        return false;
      }
      auto in_msg_tag = block::gen::t_InMsg.get_tag(*in_descr_cs);
      if (in_msg_tag != block::gen::InMsg::msg_import_ext && in_msg_tag != block::gen::InMsg::msg_import_fin &&
          in_msg_tag != block::gen::InMsg::msg_import_imm && in_msg_tag != block::gen::InMsg::msg_import_ihr &&
          in_msg_tag != block::gen::InMsg::msg_import_deferred_fin) {
        return false;
      }
      is_special_tx = validator_->is_special_in_msg(*in_descr_cs);
      // once we know there is a InMsg with correct hash, we already know that it contains a message with this hash (by the verification of InMsg), so it is our message
      // have still to check its destination address and imported value
      // and that it refers to this transaction
      Ref<vm::CellSlice> dest;
      if (in_msg_tag == block::gen::InMsg::msg_import_ext) {
        block::gen::CommonMsgInfo::Record_ext_in_msg_info info;
        CHECK(tlb::unpack_cell_inexact(in_msg_root, info));
        dest = std::move(info.dest);
        external = true;
      } else {
        block::gen::CommonMsgInfo::Record_int_msg_info info;
        CHECK(tlb::unpack_cell_inexact(in_msg_root, info));
        if (info.created_lt >= lt) {
          return false;
        }
        LogicalTime emitted_lt = info.created_lt;  // See ContestValidator::check_message_processing_order
        if (in_msg_tag == block::gen::InMsg::msg_import_imm || in_msg_tag == block::gen::InMsg::msg_import_fin ||
            in_msg_tag == block::gen::InMsg::msg_import_deferred_fin) {
          block::tlb::MsgEnvelope::Record_std msg_env;
          if (!block::tlb::unpack_cell(in_descr_cs->prefetch_ref(), msg_env)) {
            return false;
          }
          in_msg_metadata = std::move(msg_env.metadata);
          if (msg_env.emitted_lt) {
            emitted_lt = msg_env.emitted_lt.value();
          }
        }
        if (info.created_lt != validator_->state_->start_lt_ || !is_special_tx) {
          static td::SpinLock msg_proc_lt_mutex_;
          auto msg_proc_lt_lock = msg_proc_lt_mutex_.lock();
          validator_->state_->msg_proc_lt_.emplace_back(addr, lt, emitted_lt);
        }
        dest = std::move(info.dest);
        CHECK(money_imported.validate_unpack(info.value));
        ihr_delivered = (in_msg_tag == block::gen::InMsg::msg_import_ihr);
        if (!ihr_delivered) {
          money_imported += block::tlb::t_Grams.as_integer(info.ihr_fee);
        }
        CHECK(money_imported.is_valid());
      }
      WorkchainId d_wc;
      StdSmcAddress d_addr;
      CHECK(block::tlb::t_MsgAddressInt.extract_std_address(dest, d_wc, d_addr));
      if (d_wc != validator_->state_->shard_.workchain || d_addr != addr) {
        return false;
      }
      auto in_msg_trans = in_descr_cs->prefetch_ref(1);  // trans:^Transaction
      CHECK(in_msg_trans.not_null());
      if (in_msg_trans->get_hash() != trans_root->get_hash()) {
        return false;
      }
    }
    // check output messages
    {
      auto check_out_msgs_timer = CREATE_PERF_TIMER(XX_TRANSACTION_CHECK_OUT_MSGS);
      td::optional<block::MsgMetadata> new_msg_metadata;
      if (validator_->state_->msg_metadata_enabled_) {
        if (external || is_special_tx || tag != block::gen::TransactionDescr::trans_ord) {
          new_msg_metadata = block::MsgMetadata{0, account.workchain, account.addr, (LogicalTime)trans.lt};
        } else if (in_msg_metadata) {
          new_msg_metadata = std::move(in_msg_metadata);
          ++new_msg_metadata.value().depth;
        }
      }
      vm::Dictionary out_dict{trans.r1.out_msgs, 15};
      for (int i = 0; i < trans.outmsg_cnt; i++) {
        auto out_msg_root = out_dict.lookup_ref(td::BitArray<15>{i});
        CHECK(out_msg_root.not_null());  // we have pre-checked this
        auto out_descr_cs = validator_->state_->out_msg_dict_->lookup(out_msg_root->get_hash().as_bitslice());
        if (out_descr_cs.is_null()) {
          return false;
        }
        auto tag = block::gen::t_OutMsg.get_tag(*out_descr_cs);
        if (tag != block::gen::OutMsg::msg_export_ext && tag != block::gen::OutMsg::msg_export_new &&
            tag != block::gen::OutMsg::msg_export_imm && tag != block::gen::OutMsg::msg_export_new_defer) {
          return false;
        }
        // once we know there is an OutMsg with correct hash, we already know that it contains a message with this hash
        // (by the verification of OutMsg), so it is our message
        // have still to check its source address, lt and imported value
        // and that it refers to this transaction as its origin
        Ref<vm::CellSlice> src;
        LogicalTime message_lt;
        if (tag == block::gen::OutMsg::msg_export_ext) {
          block::gen::CommonMsgInfo::Record_ext_out_msg_info info;
          CHECK(tlb::unpack_cell_inexact(out_msg_root, info));
          src = std::move(info.src);
          message_lt = info.created_lt;
        } else {
          block::gen::CommonMsgInfo::Record_int_msg_info info;
          CHECK(tlb::unpack_cell_inexact(out_msg_root, info));
          src = std::move(info.src);
          message_lt = info.created_lt;
          block::tlb::MsgEnvelope::Record_std msg_env;
          CHECK(tlb::unpack_cell(out_descr_cs->prefetch_ref(), msg_env));
          // unpack exported message value (from this transaction)
          block::CurrencyCollection msg_export_value;
          CHECK(msg_export_value.unpack(info.value));
          msg_export_value += block::tlb::t_Grams.as_integer(info.ihr_fee);
          msg_export_value += msg_env.fwd_fee_remaining;
          CHECK(msg_export_value.is_valid());
          money_exported += msg_export_value;
          if (msg_env.metadata != new_msg_metadata) {
            return false;
          }
        }
        WorkchainId s_wc;
        StdSmcAddress ss_addr;  // s_addr is some macros in Windows
        CHECK(block::tlb::t_MsgAddressInt.extract_std_address(src, s_wc, ss_addr));
        if (s_wc != validator_->state_->shard_.workchain || ss_addr != addr) {
          return false;
        }
        auto out_msg_trans = out_descr_cs->prefetch_ref(1);  // trans:^Transaction
        CHECK(out_msg_trans.not_null());
        if (out_msg_trans->get_hash() != trans_root->get_hash()) {
          return false;
        }
        if (tag != block::gen::OutMsg::msg_export_ext) {
          static td::SpinLock deferred_messages_mutex_;
          auto deferred_lock = deferred_messages_mutex_.lock();

          bool is_deferred = tag == block::gen::OutMsg::msg_export_new_defer;
          if (validator_->state_->account_expected_defer_all_messages_.count(ss_addr) && !is_deferred) {
            return false;
          }
          if (is_deferred) {
            LOG(INFO) << "message from account " << validator_->state_->shard_.workchain << ":" << ss_addr.to_hex() << " with lt " << message_lt
                      << " was deferred";
            if (!validator_->state_->deferring_messages_enabled_ && !validator_->state_->account_expected_defer_all_messages_.count(ss_addr)) {
              return false;
            }
            if (i == 0 && !validator_->state_->account_expected_defer_all_messages_.count(ss_addr)) {
              return false;
            }
            validator_->state_->account_expected_defer_all_messages_.insert(ss_addr);
          }
        }
      }
    }

    CHECK(money_exported.is_valid());
    // check general transaction data
    block::CurrencyCollection old_balance{account.get_balance()};
    {
      auto check_general_data_timer = CREATE_PERF_TIMER(XX_TRANSACTION_CHECK_GENERAL_DATA);
      if (tag == block::gen::TransactionDescr::trans_merge_prepare ||
          tag == block::gen::TransactionDescr::trans_merge_install ||
          tag == block::gen::TransactionDescr::trans_split_prepare ||
          tag == block::gen::TransactionDescr::trans_split_install) {
        bool split = (tag == block::gen::TransactionDescr::trans_split_prepare ||
                      tag == block::gen::TransactionDescr::trans_split_install);
        if (split && !validator_->state_->before_split_) {
          return false;
        }
        if (split && !is_last) {
          return false;
        }
        if (!split && !validator_->state_->after_merge_) {
          return false;
        }
        if (!split && !is_first) {
          return false;
        }
        // check later a global configuration flag in config_.global_flags_
        // (for now, split/merge transactions are always globally disabled)
        return false;
      }
      if (tag == block::gen::TransactionDescr::trans_tick_tock) {
        return false;
      }
      if (tag == block::gen::TransactionDescr::trans_storage && !is_first) {
        return false;
      }
    }

    // check that the original account state has correct hash
    {
      auto check_account_validator_timer = CREATE_PERF_TIMER(XX_TRANSACTION_CHECK_ACCOUNT_STATE);
      CHECK(account.total_state.not_null());
      if (hash_upd.old_hash != account.total_state->get_hash().bits()) {
        return false;
      }
    }

    // some type-specific checks
    int trans_type = block::transaction::Transaction::tr_none;
    {
      auto check_type_specific_data_timer = CREATE_PERF_TIMER(XX_TRANSACTION_CHECK_TYPE_SPECIFIC_DATA);
      switch (tag) {
        case block::gen::TransactionDescr::trans_ord: {
          trans_type = block::transaction::Transaction::tr_ord;
          if (in_msg_root.is_null()) {
            return false;
          }
          need_credit_phase = !external;
          break;
        }
        case block::gen::TransactionDescr::trans_storage: {
          trans_type = block::transaction::Transaction::tr_storage;
          if (in_msg_root.not_null()) {
            return false;
          }
          if (trans.outmsg_cnt) {
            return false;
          }
          // FIXME
          return false;
          break;
        }
        case block::gen::TransactionDescr::trans_tick_tock: {
          bool is_tock = (td_cs.prefetch_ulong(4) & 1);
          trans_type = is_tock ? block::transaction::Transaction::tr_tock : block::transaction::Transaction::tr_tick;
          if (in_msg_root.not_null()) {
            return false;
          }
          break;
        }
        case block::gen::TransactionDescr::trans_merge_prepare: {
          trans_type = block::transaction::Transaction::tr_merge_prepare;
          if (in_msg_root.not_null()) {
            return false;
          }
          if (trans.outmsg_cnt != 1) {
            return false;
          }
          // FIXME
          return false;
          break;
        }
        case block::gen::TransactionDescr::trans_merge_install: {
          trans_type = block::transaction::Transaction::tr_merge_install;
          if (in_msg_root.is_null()) {
            return false;
          }
          need_credit_phase = true;
          // FIXME
          return false;
          break;
        }
        case block::gen::TransactionDescr::trans_split_prepare: {
          trans_type = block::transaction::Transaction::tr_split_prepare;
          if (in_msg_root.not_null()) {
            return false;
          }
          if (trans.outmsg_cnt > 1) {
            return false;
          }
          // FIXME
          return false;
          break;
        }
        case block::gen::TransactionDescr::trans_split_install: {
          trans_type = block::transaction::Transaction::tr_split_install;
          if (in_msg_root.is_null()) {
            return false;
          }
          // FIXME
          return false;
          break;
        }
      }
    }

    // ....
    // check transaction computation by re-doing it
    // similar to Collator::create_ordinary_transaction() and Collator::create_ticktock_transaction()
    // ....
    std::unique_ptr<block::transaction::Transaction> trs =
        std::make_unique<block::transaction::Transaction>(account, trans_type, lt, validator_->state_->now_, in_msg_root);
    {
      auto check_transaction_computation_timer = CREATE_PERF_TIMER(XX_TRANSACTION_CHECK_TRANSACTION_COMPUTATION);
      if (in_msg_root.not_null()) {
        if (!trs->unpack_input_msg(ihr_delivered, &validator_->state_->action_phase_cfg_)) {
          // inbound external message was not accepted
          return false;
        }
      }
      if (trs->bounce_enabled) {
        if (!trs->prepare_storage_phase(validator_->state_->storage_phase_cfg_, true)) {
          return false;
        }
        if (need_credit_phase && !trs->prepare_credit_phase()) {
          return false;
        }
      } else {
        if (need_credit_phase && !trs->prepare_credit_phase()) {
          return false;
        }
        if (!trs->prepare_storage_phase(validator_->state_->storage_phase_cfg_, true, need_credit_phase)) {
          return false;
        }
      }
      if (!trs->prepare_compute_phase(validator_->state_->compute_phase_cfg_)) {
        return false;
      }
      if (!trs->compute_phase->accepted) {
        if (external) {
          return false;
        } else if (trs->compute_phase->skip_reason == block::ComputePhase::sk_none) {
          return false;
        }
      }
      if (trs->compute_phase->success && !trs->prepare_action_phase(validator_->state_->action_phase_cfg_)) {
        return false;
      }
      if (trs->bounce_enabled &&
          (!trs->compute_phase->success || trs->action_phase->state_exceeds_limits || trs->action_phase->bounce) &&
          !trs->prepare_bounce_phase(validator_->state_->action_phase_cfg_)) {
        return false;
      }
      if (!trs->serialize()) {
        return false;
      }
      if (!trs->update_limits(*validator_->state_->block_limit_status_, /* with_gas = */ false, /* with_size = */ false)) {
        return false;
      }
    }

    // Collator should stop if total gas usage exceeds limits, including transactions on special accounts, but without
    // ticktocks and mint/recover.
    // Here Validator checks a weaker condition
    {
      static td::SpinLock gas_usage_mutex_;
      auto gas_lock = gas_usage_mutex_.lock();
      if (!is_special_tx && !trs->gas_limit_overridden && trans_type == block::transaction::Transaction::tr_ord) {
        (account.is_special ? validator_->state_->total_special_gas_used_ : validator_->state_->total_gas_used_) += trs->gas_used();
      }
      if (validator_->state_->total_gas_used_ > validator_->state_->block_limits_->gas.hard() + validator_->state_->compute_phase_cfg_.gas_limit) {
        return false;
      }
      if (validator_->state_->total_special_gas_used_ > validator_->state_->block_limits_->gas.hard() + validator_->state_->compute_phase_cfg_.special_gas_limit) {
        return false;
      }
    }

    auto commit_transaction_timer = CREATE_PERF_TIMER(XX_TRANSACTION_COMMIT_TRANSACTION);
    auto trans_root2 = trs->commit(account);
    if (trans_root2.is_null()) {
      return false;
    }
    commit_transaction_timer.stop();

    // now compare the re-created transaction with the one we have
    {
      auto compare_transactions_timer = CREATE_PERF_TIMER(XX_TRANSACTION_COMPARE_TRANSACTIONS);
      if (trans_root2->get_hash() != trans_root->get_hash()) {
        return false;
      }
      block::gen::Transaction::Record trans2;
      block::gen::HASH_UPDATE::Record hash_upd2;
      if (!(tlb::unpack_cell(trans_root2, trans2) &&
            tlb::type_unpack_cell(std::move(trans2.state_update), block::gen::t_HASH_UPDATE_Account, hash_upd2))) {
        return false;
      }
      if (hash_upd2.old_hash != hash_upd.old_hash) {
        return false;
      }
      if (hash_upd2.new_hash != account.total_state->get_hash().bits()) {
        return false;
      }
      if (hash_upd.new_hash != account.total_state->get_hash().bits()) {
        return false;
      }
      if (!trans.r1.out_msgs->contents_equal(*trans2.r1.out_msgs)) {
        return false;
      }
      {
        static td::SpinLock total_burned_mutex_;
        auto burned_lock = total_burned_mutex_.lock();
        validator_->state_->total_burned_ += trs->blackhole_burned;
      }
      // check new balance and value flow
      auto new_balance = account.get_balance();
      block::CurrencyCollection total_fees;
      if (!total_fees.validate_unpack(trans.total_fees)) {
        return false;
      }
      if (old_balance + money_imported != new_balance + money_exported + total_fees + trs->blackhole_burned) {
        return false;
      }
    }
    return true;
  }

  std::unique_ptr<block::Account> unpack_account(td::ConstBitPtr addr) {
    auto dict_entry = validator_->state_->ps_.account_dict_->lookup_extra(addr, 256);
    auto new_acc = make_account_from(addr, std::move(dict_entry.first));
    if (!new_acc) {
      return {};
    }
    if (!new_acc->belongs_to_shard(validator_->state_->shard_)) {
      return {};
    }
    return new_acc;
  }

  std::unique_ptr<block::Account> make_account_from(td::ConstBitPtr addr, Ref<vm::CellSlice> account) {
    auto ptr = std::make_unique<block::Account>(validator_->state_->shard_.workchain, addr);
    if (account.is_null()) {
      if (!ptr->init_new(validator_->state_->now_)) {
        return nullptr;
      }
    } else if (!ptr->unpack(std::move(account), validator_->state_->now_, false)) {
      return nullptr;
    }
    ptr->block_lt = validator_->state_->start_lt_;
    return ptr;
  }

 private:
  td::Promise<bool> promise_;
  ContestValidatorType* validator_;
  const StdSmcAddress acc_addr_;
  Ref<vm::CellSlice> acc_blk_root_;         

  static td::SpinLock account_dict_mutex_;
};

}  // namespace solution
