#include "contest-validate-query.hpp"
#include "transaction-checker.hpp"


using td::Ref;
using namespace std::literals::string_literals;

/**
 * Initializes the TransactionChecker object.
 * Creates a promise and future for asynchronous operations.
 *
 */
TransactionChecker::TransactionChecker()
    : result_promise(), result_future(result_promise.get_future()) {}

/**
 * Stops the transaction check and waits for the operation to complete.
 * 
 */
void TransactionChecker::stop_and_wait() {
  stop();
  wait();
}

/**
 * Stops the transaction checking process.
 * Sets a flag to stop the transaction checking operation.
 */
void TransactionChecker::stop() {
  stop_check_transactions.store(true);
}

/**
 * Initializes the TransactionChecker object with the given parameters.
 *
 * @param tm Block creation time.
 * @param start_lt Block start logical time.
 * @param verbosity_lvl Verbosity level for logging.
 * @param after_merge Flag indicating that the block is after the merge.
 * @param before_split Flag indicating that the block is before split.
 * @param msg_metadata_enabled Whether message metadata processing is enabled.
 * @param deferring_messages_enabled Whether message deferring is allowed.
 * @param _block_limits_gas_hard Hard gas limit for the block.
 * @param shard Shard identifier.
 * @param storage_phase_cfg Configuration for the block's storage phase.
 * @param compute_phase_cfg Configuration for the block's compute phase.
 * @param block_limit_status Status of block limits.
 * @param msg_proc_lt List of message processing logical times.
 * @param action_phase_cfg Configuration for the action phase.
 * @param account_expected_defer_all_messages Set of accounts expected to defer all messages.
 * @param ns_account_dict New state accounts.
 * @param ps_account_dict Post state accounts.
 * @param total_burned Total burned.
 */

void TransactionChecker::init(
    UnixTime tm,
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
) {
    now_ = tm;
    start_lt_ = start_lt;
    verbosity = verbosity_lvl;
    after_merge_ = after_merge;
    before_split_ = before_split;
    msg_metadata_enabled_ = msg_metadata_enabled;
    deferring_messages_enabled_ = deferring_messages_enabled;

    block_limits_gas_hard = _block_limits_gas_hard;
    shard_ = shard;

    storage_phase_cfg_ = std::move(storage_phase_cfg);
    compute_phase_cfg_ = std::move(compute_phase_cfg);

    block_limit_status_ = std::move(block_limit_status);
    msg_proc_lt_ = std::move(msg_proc_lt);
    
    action_phase_cfg_ = std::move(action_phase_cfg);
    account_expected_defer_all_messages_ = std::move(account_expected_defer_all_messages);

    ns_account_dict_ = std::move(ns_account_dict);
    ps_account_dict_ = std::move(ps_account_dict);
    total_burned_ = std::move(total_burned);
    // reset flags
    stop_check_transactions.store(false);
    running = false;
    initialized = true;
}

/**
 * Waits for the transaction checking process to complete.
 * If the transaction checking is running, it waits for the operation to complete and returns the result.
 *
 * @returns True if the checking completed successfully.
 */
bool TransactionChecker::wait() {
  if (running) {
    running = false;
    auto res = result_future.get();
    initialized = false;
    return res;
  }
  return false;
}

/**
 * Starts the transaction checking process in a separate thread.
 *
 */
bool TransactionChecker::run() {
  if (!initialized) {
    return reject_query("TransactionChecker should be initialized before run");
  }
  if (!prepared) {
    return reject_query("Call prepare() before run");
  }
  if (running) {
    return reject_query("TransactionChecker worker already running");
  }
  running = true;
  worker = std::thread([this]() {
    result_promise.set_value(check_transactions());
  });
  return true;
}

bool TransactionChecker::prepare(std::unique_ptr<vm::AugmentedDictionary> in_msg_dict, 
                      std::unique_ptr<vm::AugmentedDictionary> out_msg_dict, 
                      std::unique_ptr<vm::AugmentedDictionary> account_blocks_dict) {
    if(!in_msg_dict || !out_msg_dict || !account_blocks_dict){
      return reject_query("in_msg_dict or out_msg_dict or account_blocks_dict is invalid");
    }
    in_msg_dict_ = std::move(in_msg_dict);
    out_msg_dict_ = std::move(out_msg_dict);
    account_blocks_dict_ = std::move(account_blocks_dict);
    prepared = true;
    return true;
}

/**
 * Rejects the validation and logs an error message.
 *
 * @param error The error message to be logged.
 * @param reason The reason for rejecting the validation.
 *
 * @returns False indicating that the validation failed.
 */
bool TransactionChecker::reject_query(std::string error, td::BufferSlice reason) {
  LOG(WARNING) << "REJECT: aborting validation of block candidate for " << shard_.to_str() << " : " << error;
  return false;
}

/**
 * Rejects the validation and logs an error message.
 *
 * @param err_msg The error message to be displayed.
 * @param error The error status.
 * @param reason The reason for rejecting the query.
 *
 * @returns False indicating that the validation failed.
 */
bool TransactionChecker::reject_query(std::string err_msg, td::Status error, td::BufferSlice reason) {
  error.ensure_error();
  return reject_query(err_msg + " : " + error.to_string(), std::move(reason));
}

/**
 * Handles a fatal error during validation.
 *
 * @param error The error status.
 *
 * @returns False indicating that the validation failed.
 */
bool TransactionChecker::fatal_error(td::Status error) {
  error.ensure_error();
  LOG(WARNING) << "aborting validation of block candidate for " << shard_.to_str() << " : " << error.to_string();
  return false;
}

/**
 * Handles a fatal error during validation.
 *
 * @param err_code Error code.
 * @param err_msg Error message.
 *
 * @returns False indicating that the validation failed.
 */
bool TransactionChecker::fatal_error(int err_code, std::string err_msg) {
  return fatal_error(td::Status::Error(err_code, err_msg));
}

/**
 * Handles a fatal error during validation.
 *
 * @param err_code Error code.
 * @param err_msg Error message.
 * @param error Error status.
 *
 * @returns False indicating that the validation failed.
 */
bool TransactionChecker::fatal_error(int err_code, std::string err_msg, td::Status error) {
  error.ensure_error();
  return fatal_error(err_code, err_msg + " : " + error.to_string());
}

/**
 * Handles a fatal error during validation.
 *
 * @param err_msg Error message.
 * @param err_code Error code.
 *
 * @returns False indicating that the validation failed.
 */
bool TransactionChecker::fatal_error(std::string err_msg, int err_code) {
  return fatal_error(td::Status::Error(err_code, err_msg));
}

/**
 * Checks all transactions in the account blocks.
 *
 * @returns True if all transactions pass the check, False otherwise.
 */
bool TransactionChecker::check_transactions() {
  try {
    LOG(INFO) << "checking all transactions";
    bool ok = account_blocks_dict_->check_for_each_extra(
        [this](Ref<vm::CellSlice> value, Ref<vm::CellSlice> extra, td::ConstBitPtr key, int key_len) {
          CHECK(key_len == 256);
          if (stop_check_transactions.load()) {
            return false;
          }
          return check_account_transactions(key, std::move(value));
        });
    return ok;
  } catch (const std::exception& e) {
    LOG(ERROR) << "Exception occurred during transactions check: " << e.what();
    return false;
  }
  return false;
}

/**
 * Checks the validity of transactions for a given account block.
 * NB: may be run in parallel for different accounts
 *
 * @param acc_addr The address of the account.
 * @param acc_blk_root The root of the AccountBlock.
 *
 * @returns True if the account transactions are valid, false otherwise.
 */
bool TransactionChecker::check_account_transactions(const StdSmcAddress& acc_addr, Ref<vm::CellSlice> acc_blk_root) {
  block::gen::AccountBlock::Record acc_blk;
  CHECK(tlb::csr_unpack(std::move(acc_blk_root), acc_blk) && acc_blk.account_addr == acc_addr);
  auto account_p = unpack_account(acc_addr.cbits());
  if (!account_p) {
    return reject_query("cannot unpack old state of account "s + acc_addr.to_hex());
  }
  auto& account = *account_p;
  CHECK(account.addr == acc_addr);
  vm::AugmentedDictionary trans_dict{vm::DictNonEmpty(), std::move(acc_blk.transactions), 64,
                                     block::tlb::aug_AccountTransactions};
  td::BitArray<64> min_trans, max_trans;
  CHECK(trans_dict.get_minmax_key(min_trans).not_null() && trans_dict.get_minmax_key(max_trans, true).not_null());
  ton::LogicalTime min_trans_lt = min_trans.to_ulong(), max_trans_lt = max_trans.to_ulong();
  if (!trans_dict.check_for_each_extra([this, &account, min_trans_lt, max_trans_lt](Ref<vm::CellSlice> value,
                                                                                    Ref<vm::CellSlice> extra,
                                                                                    td::ConstBitPtr key, int key_len) {
        if (stop_check_transactions.load()) {
          return false;
        }
        CHECK(key_len == 64);
        ton::LogicalTime lt = key.get_uint(64);
        extra.clear();
        return check_one_transaction(account, lt, value->prefetch_ref(), lt == min_trans_lt, lt == max_trans_lt);
      })) {
    return reject_query("at least one Transaction of account "s + acc_addr.to_hex() + " is invalid");
  }

  // See Collator::combine_account_trabsactions
  if (account.total_state->get_hash() != account.orig_total_state->get_hash()) {
    // account changed
    if (account.orig_status == block::Account::acc_nonexist) {
      // account created
      CHECK(account.status != block::Account::acc_nonexist);
      vm::CellBuilder cb;
      if (!(cb.store_ref_bool(account.total_state)             // account_descr$_ account:^Account
            && cb.store_bits_bool(account.last_trans_hash_)    // last_trans_hash:bits256
            && cb.store_long_bool(account.last_trans_lt_, 64)  // last_trans_lt:uint64
            && ns_account_dict_->set_builder(account.addr, cb, vm::Dictionary::SetMode::Add))) {
        return fatal_error(std::string{"cannot add newly-created account "} + account.addr.to_hex() +
                           " into ShardAccounts");
      }
    } else if (account.status == block::Account::acc_nonexist) {
      // account deleted
      if (verbosity > 2) {
        std::cerr << "deleting account " << account.addr.to_hex() << " with empty new value ";
        block::gen::t_Account.print_ref(std::cerr, account.total_state);
      }
      if (ns_account_dict_->lookup_delete(account.addr).is_null()) {
        return fatal_error(std::string{"cannot delete account "} + account.addr.to_hex() + " from ShardAccounts");
      }
    } else {
      // existing account modified
      if (verbosity > 4) {
        std::cerr << "modifying account " << account.addr.to_hex() << " to ";
        block::gen::t_Account.print_ref(std::cerr, account.total_state);
      }
      vm::CellBuilder cb;
      if (!(cb.store_ref_bool(account.total_state)             // account_descr$_ account:^Account
            && cb.store_bits_bool(account.last_trans_hash_)    // last_trans_hash:bits256
            && cb.store_long_bool(account.last_trans_lt_, 64)  // last_trans_lt:uint64
            && ns_account_dict_->set_builder(account.addr, cb, vm::Dictionary::SetMode::Replace))) {
        return fatal_error(std::string{"cannot modify existing account "} + account.addr.to_hex() +
                           " in ShardAccounts");
      }
    }
  }

  block::gen::HASH_UPDATE::Record hash_upd;
  if (!tlb::type_unpack_cell(std::move(acc_blk.state_update), block::gen::t_HASH_UPDATE_Account, hash_upd)) {
    return reject_query("cannot extract (HASH_UPDATE Account) from the AccountBlock of "s + account.addr.to_hex());
  }
  block::tlb::ShardAccount::Record old_state, new_state;
  if (!(old_state.unpack(ps_account_dict_->lookup(account.addr)) &&
        new_state.unpack(ns_account_dict_->lookup(account.addr)))) {
    return reject_query("cannot extract Account from the ShardAccount of "s + account.addr.to_hex());
  }
  if (hash_upd.old_hash != old_state.account->get_hash().bits()) {
    return reject_query("(HASH_UPDATE Account) from the AccountBlock of "s + account.addr.to_hex() +
                        " has incorrect old hash");
  }
  if (hash_upd.new_hash != new_state.account->get_hash().bits()) {
    return reject_query("(HASH_UPDATE Account) from the AccountBlock of "s + account.addr.to_hex() +
                        " has incorrect new hash");
  }

  return true;
}

/**
 * Checks the validity of a single transaction for a given account.
 * Performs transaction execution.
 *
 * @param account The account of the transaction.
 * @param lt The logical time of the transaction.
 * @param trans_root The root of the transaction.
 * @param is_first Flag indicating if this is the first transaction of the account.
 * @param is_last Flag indicating if this is the last transaction of the account.
 *
 * @returns True if the transaction is valid, false otherwise.
 */
bool TransactionChecker::check_one_transaction(block::Account& account, ton::LogicalTime lt, Ref<vm::Cell> trans_root,
                                                 bool is_first, bool is_last) {
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
    auto in_descr_cs = in_msg_dict_->lookup(in_msg_root->get_hash().as_bitslice());
    if (in_descr_cs.is_null()) {
      return reject_query(PSTRING() << "inbound message with hash " << in_msg_root->get_hash().to_hex()
                                    << " of transaction " << lt << " of account " << addr.to_hex()
                                    << " does not have a corresponding InMsg record");
    }
    auto in_msg_tag = block::gen::t_InMsg.get_tag(*in_descr_cs);
    if (in_msg_tag != block::gen::InMsg::msg_import_ext && in_msg_tag != block::gen::InMsg::msg_import_fin &&
        in_msg_tag != block::gen::InMsg::msg_import_imm && in_msg_tag != block::gen::InMsg::msg_import_ihr &&
        in_msg_tag != block::gen::InMsg::msg_import_deferred_fin) {
      return reject_query(PSTRING() << "inbound message with hash " << in_msg_root->get_hash().to_hex()
                                    << " of transaction " << lt << " of account " << addr.to_hex()
                                    << " has an invalid InMsg record (not one of msg_import_ext, msg_import_fin, "
                                       "msg_import_imm, msg_import_ihr or msg_import_deferred_fin)");
    }
    is_special_tx = is_special_in_msg(*in_descr_cs);
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
        return reject_query(PSTRING() << "transaction " << lt << " of " << addr.to_hex()
                                      << " processed inbound message created later at logical time "
                                      << info.created_lt);
      }
      LogicalTime emitted_lt = info.created_lt;  // See ContestValidateQuery::check_message_processing_order
      if (in_msg_tag == block::gen::InMsg::msg_import_imm || in_msg_tag == block::gen::InMsg::msg_import_fin ||
          in_msg_tag == block::gen::InMsg::msg_import_deferred_fin) {
        block::tlb::MsgEnvelope::Record_std msg_env;
        if (!block::tlb::unpack_cell(in_descr_cs->prefetch_ref(), msg_env)) {
          return reject_query(PSTRING() << "InMsg record for inbound message with hash "
                                        << in_msg_root->get_hash().to_hex() << " of transaction " << lt
                                        << " of account " << addr.to_hex() << " does not have a valid MsgEnvelope");
        }
        in_msg_metadata = std::move(msg_env.metadata);
        if (msg_env.emitted_lt) {
          emitted_lt = msg_env.emitted_lt.value();
        }
      }
      if (info.created_lt != start_lt_ || !is_special_tx) {
        msg_proc_lt_.emplace_back(addr, lt, emitted_lt);
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
    if (d_wc != workchain() || d_addr != addr) {
      return reject_query(PSTRING() << "inbound message of transaction " << lt << " of account " << addr.to_hex()
                                    << " has a different destination address " << d_wc << ":" << d_addr.to_hex());
    }
    auto in_msg_trans = in_descr_cs->prefetch_ref(1);  // trans:^Transaction
    CHECK(in_msg_trans.not_null());
    if (in_msg_trans->get_hash() != trans_root->get_hash()) {
      return reject_query(PSTRING() << "InMsg record for inbound message with hash " << in_msg_root->get_hash().to_hex()
                                    << " of transaction " << lt << " of account " << addr.to_hex()
                                    << " refers to a different processing transaction");
    }
  }
  // check output messages
  td::optional<block::MsgMetadata> new_msg_metadata;
  if (msg_metadata_enabled_) {
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
    auto out_descr_cs = out_msg_dict_->lookup(out_msg_root->get_hash().as_bitslice());
    if (out_descr_cs.is_null()) {
      return reject_query(PSTRING() << "outbound message #" << i + 1 << " with hash "
                                    << out_msg_root->get_hash().to_hex() << " of transaction " << lt << " of account "
                                    << addr.to_hex() << " does not have a corresponding OutMsg record");
    }
    auto tag = block::gen::t_OutMsg.get_tag(*out_descr_cs);
    if (tag != block::gen::OutMsg::msg_export_ext && tag != block::gen::OutMsg::msg_export_new &&
        tag != block::gen::OutMsg::msg_export_imm && tag != block::gen::OutMsg::msg_export_new_defer) {
      return reject_query(PSTRING() << "outbound message #" << i + 1 << " with hash "
                                    << out_msg_root->get_hash().to_hex() << " of transaction " << lt << " of account "
                                    << addr.to_hex()
                                    << " has an invalid OutMsg record (not one of msg_export_ext, msg_export_new, "
                                       "msg_export_imm or msg_export_new_defer)");
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
        return reject_query(PSTRING() << "outbound message #" << i + 1 << " with hash "
                                      << out_msg_root->get_hash().to_hex() << " of transaction " << lt << " of account "
                                      << addr.to_hex() << " has invalid metadata in an OutMsg record: expected "
                                      << (new_msg_metadata ? new_msg_metadata.value().to_str() : "<none>") << ", found "
                                      << (msg_env.metadata ? msg_env.metadata.value().to_str() : "<none>"));
      }
    }
    WorkchainId s_wc;
    StdSmcAddress ss_addr;  // s_addr is some macros in Windows
    CHECK(block::tlb::t_MsgAddressInt.extract_std_address(src, s_wc, ss_addr));
    if (s_wc != workchain() || ss_addr != addr) {
      return reject_query(PSTRING() << "outbound message #" << i + 1 << " of transaction " << lt << " of account "
                                    << addr.to_hex() << " has a different source address " << s_wc << ":"
                                    << ss_addr.to_hex());
    }
    auto out_msg_trans = out_descr_cs->prefetch_ref(1);  // trans:^Transaction
    CHECK(out_msg_trans.not_null());
    if (out_msg_trans->get_hash() != trans_root->get_hash()) {
      return reject_query(PSTRING() << "OutMsg record for outbound message #" << i + 1 << " with hash "
                                    << out_msg_root->get_hash().to_hex() << " of transaction " << lt << " of account "
                                    << addr.to_hex() << " refers to a different processing transaction");
    }
    if (tag != block::gen::OutMsg::msg_export_ext) {
      bool is_deferred = tag == block::gen::OutMsg::msg_export_new_defer;
      if (account_expected_defer_all_messages_.count(ss_addr) && !is_deferred) {
        return reject_query(
            PSTRING() << "outbound message #" << i + 1 << " on account " << workchain() << ":" << ss_addr.to_hex()
                      << " must be deferred because this account has earlier messages in DispatchQueue");
      }
      if (is_deferred) {
        LOG(INFO) << "message from account " << workchain() << ":" << ss_addr.to_hex() << " with lt " << message_lt
                  << " was deferred";
        if (!deferring_messages_enabled_ && !account_expected_defer_all_messages_.count(ss_addr)) {
          return reject_query(PSTRING() << "outbound message #" << i + 1 << " on account " << workchain() << ":"
                                        << ss_addr.to_hex() << " is deferred, but deferring messages is disabled");
        }
        if (i == 0 && !account_expected_defer_all_messages_.count(ss_addr)) {
          return reject_query(PSTRING() << "outbound message #1 on account " << workchain() << ":" << ss_addr.to_hex()
                                        << " must not be deferred (the first message cannot be deferred unless some "
                                           "prevoius messages are deferred)");
        }
        account_expected_defer_all_messages_.insert(ss_addr);
      }
    }
  }
  CHECK(money_exported.is_valid());
  // check general transaction data
  block::CurrencyCollection old_balance{account.get_balance()};
  if (tag == block::gen::TransactionDescr::trans_merge_prepare ||
      tag == block::gen::TransactionDescr::trans_merge_install ||
      tag == block::gen::TransactionDescr::trans_split_prepare ||
      tag == block::gen::TransactionDescr::trans_split_install) {
    bool split = (tag == block::gen::TransactionDescr::trans_split_prepare ||
                  tag == block::gen::TransactionDescr::trans_split_install);
    if (split && !before_split_) {
      return reject_query(PSTRING() << "transaction " << lt << " of account " << addr.to_hex()
                                    << " is a split prepare/install transaction, but this block is not before a split");
    }
    if (split && !is_last) {
      return reject_query(PSTRING() << "transaction " << lt << " of account " << addr.to_hex()
                                    << " is a split prepare/install transaction, but it is not the last transaction "
                                       "for this account in this block");
    }
    if (!split && !after_merge_) {
      return reject_query(
          PSTRING() << "transaction " << lt << " of account " << addr.to_hex()
                    << " is a merge prepare/install transaction, but this block is not immediately after a merge");
    }
    if (!split && !is_first) {
      return reject_query(PSTRING() << "transaction " << lt << " of account " << addr.to_hex()
                                    << " is a merge prepare/install transaction, but it is not the first transaction "
                                       "for this account in this block");
    }
    // check later a global configuration flag in config_.global_flags_
    // (for now, split/merge transactions are always globally disabled)
    return reject_query(PSTRING() << "transaction " << lt << " of account " << addr.to_hex()
                                  << " is a split/merge prepare/install transaction, which are globally disabled");
  }
  if (tag == block::gen::TransactionDescr::trans_tick_tock) {
    return reject_query(PSTRING() << "transaction " << lt << " of account " << addr.to_hex()
                                  << " is a tick-tock transaction, which is impossible outside a masterchain block");
  }
  if (tag == block::gen::TransactionDescr::trans_storage && !is_first) {
    return reject_query(
        PSTRING() << "transaction " << lt << " of account " << addr.to_hex()
                  << " is a storage transaction, but it is not the first transaction for this account in this block");
  }
  // check that the original account state has correct hash
  CHECK(account.total_state.not_null());
  if (hash_upd.old_hash != account.total_state->get_hash().bits()) {
    return reject_query(PSTRING() << "transaction " << lt << " of account " << addr.to_hex()
                                  << " claims that the original account state hash must be "
                                  << hash_upd.old_hash.to_hex() << " but the actual value is "
                                  << account.total_state->get_hash().to_hex());
  }
  // some type-specific checks
  int trans_type = block::transaction::Transaction::tr_none;
  switch (tag) {
    case block::gen::TransactionDescr::trans_ord: {
      trans_type = block::transaction::Transaction::tr_ord;
      if (in_msg_root.is_null()) {
        return reject_query(PSTRING() << "ordinary transaction " << lt << " of account " << addr.to_hex()
                                      << " has no inbound message");
      }
      need_credit_phase = !external;
      break;
    }
    case block::gen::TransactionDescr::trans_storage: {
      trans_type = block::transaction::Transaction::tr_storage;
      if (in_msg_root.not_null()) {
        return reject_query(PSTRING() << "storage transaction " << lt << " of account " << addr.to_hex()
                                      << " has an inbound message");
      }
      if (trans.outmsg_cnt) {
        return reject_query(PSTRING() << "storage transaction " << lt << " of account " << addr.to_hex()
                                      << " has at least one outbound message");
      }
      // FIXME
      return reject_query(PSTRING() << "unable to verify storage transaction " << lt << " of account "
                                    << addr.to_hex());
      break;
    }
    case block::gen::TransactionDescr::trans_tick_tock: {
      bool is_tock = (td_cs.prefetch_ulong(4) & 1);
      trans_type = is_tock ? block::transaction::Transaction::tr_tock : block::transaction::Transaction::tr_tick;
      if (in_msg_root.not_null()) {
        return reject_query(PSTRING() << (is_tock ? "tock" : "tick") << " transaction " << lt << " of account "
                                      << addr.to_hex() << " has an inbound message");
      }
      break;
    }
    case block::gen::TransactionDescr::trans_merge_prepare: {
      trans_type = block::transaction::Transaction::tr_merge_prepare;
      if (in_msg_root.not_null()) {
        return reject_query(PSTRING() << "merge prepare transaction " << lt << " of account " << addr.to_hex()
                                      << " has an inbound message");
      }
      if (trans.outmsg_cnt != 1) {
        return reject_query(PSTRING() << "merge prepare transaction " << lt << " of account " << addr.to_hex()
                                      << " must have exactly one outbound message");
      }
      // FIXME
      return reject_query(PSTRING() << "unable to verify merge prepare transaction " << lt << " of account "
                                    << addr.to_hex());
      break;
    }
    case block::gen::TransactionDescr::trans_merge_install: {
      trans_type = block::transaction::Transaction::tr_merge_install;
      if (in_msg_root.is_null()) {
        return reject_query(PSTRING() << "merge install transaction " << lt << " of account " << addr.to_hex()
                                      << " has no inbound message");
      }
      need_credit_phase = true;
      // FIXME
      return reject_query(PSTRING() << "unable to verify merge install transaction " << lt << " of account "
                                    << addr.to_hex());
      break;
    }
    case block::gen::TransactionDescr::trans_split_prepare: {
      trans_type = block::transaction::Transaction::tr_split_prepare;
      if (in_msg_root.not_null()) {
        return reject_query(PSTRING() << "split prepare transaction " << lt << " of account " << addr.to_hex()
                                      << " has an inbound message");
      }
      if (trans.outmsg_cnt > 1) {
        return reject_query(PSTRING() << "split prepare transaction " << lt << " of account " << addr.to_hex()
                                      << " must have exactly one outbound message");
      }
      // FIXME
      return reject_query(PSTRING() << "unable to verify split prepare transaction " << lt << " of account "
                                    << addr.to_hex());
      break;
    }
    case block::gen::TransactionDescr::trans_split_install: {
      trans_type = block::transaction::Transaction::tr_split_install;
      if (in_msg_root.is_null()) {
        return reject_query(PSTRING() << "split install transaction " << lt << " of account " << addr.to_hex()
                                      << " has no inbound message");
      }
      // FIXME
      return reject_query(PSTRING() << "unable to verify split install transaction " << lt << " of account "
                                    << addr.to_hex());
      break;
    }
  }
  // ....
  // check transaction computation by re-doing it
  // similar to Collator::create_ordinary_transaction() and Collator::create_ticktock_transaction()
  // ....
  std::unique_ptr<block::transaction::Transaction> trs =
      std::make_unique<block::transaction::Transaction>(account, trans_type, lt, now_, in_msg_root);
  if (in_msg_root.not_null()) {
    if (!trs->unpack_input_msg(ihr_delivered, &action_phase_cfg_)) {
      // inbound external message was not accepted
      return reject_query(PSTRING() << "could not unpack inbound " << (external ? "external" : "internal")
                                    << " message processed by ordinary transaction " << lt << " of account "
                                    << addr.to_hex());
    }
  }
  if (trs->bounce_enabled) {
    if (!trs->prepare_storage_phase(storage_phase_cfg_, true)) {
      return reject_query(PSTRING() << "cannot re-create storage phase of transaction " << lt << " for smart contract "
                                    << addr.to_hex());
    }
    if (need_credit_phase && !trs->prepare_credit_phase()) {
      return reject_query(PSTRING() << "cannot create re-credit phase of transaction " << lt << " for smart contract "
                                    << addr.to_hex());
    }
  } else {
    if (need_credit_phase && !trs->prepare_credit_phase()) {
      return reject_query(PSTRING() << "cannot re-create credit phase of transaction " << lt << " for smart contract "
                                    << addr.to_hex());
    }
    if (!trs->prepare_storage_phase(storage_phase_cfg_, true, need_credit_phase)) {
      return reject_query(PSTRING() << "cannot re-create storage phase of transaction " << lt << " for smart contract "
                                    << addr.to_hex());
    }
  }
  if (!trs->prepare_compute_phase(compute_phase_cfg_)) {
    return reject_query(PSTRING() << "cannot re-create compute phase of transaction " << lt << " for smart contract "
                                  << addr.to_hex());
  }
  if (!trs->compute_phase->accepted) {
    if (external) {
      return reject_query(PSTRING() << "inbound external message claimed to be processed by ordinary transaction " << lt
                                    << " of account " << addr.to_hex()
                                    << " was in fact rejected (such transaction cannot appear in valid blocks)");
    } else if (trs->compute_phase->skip_reason == block::ComputePhase::sk_none) {
      return reject_query(PSTRING() << "inbound internal message processed by ordinary transaction " << lt
                                    << " of account " << addr.to_hex() << " was not processed without any reason");
    }
  }
  if (trs->compute_phase->success && !trs->prepare_action_phase(action_phase_cfg_)) {
    return reject_query(PSTRING() << "cannot re-create action phase of transaction " << lt << " for smart contract "
                                  << addr.to_hex());
  }
  if (trs->bounce_enabled &&
      (!trs->compute_phase->success || trs->action_phase->state_exceeds_limits || trs->action_phase->bounce) &&
      !trs->prepare_bounce_phase(action_phase_cfg_)) {
    return reject_query(PSTRING() << "cannot re-create bounce phase of  transaction " << lt << " for smart contract "
                                  << addr.to_hex());
  }
  if (!trs->serialize()) {
    return reject_query(PSTRING() << "cannot re-create the serialization of  transaction " << lt
                                  << " for smart contract " << addr.to_hex());
  }
  if (!trs->update_limits(*block_limit_status_, /* with_gas = */ false, /* with_size = */ false)) {
    return fatal_error(PSTRING() << "cannot update block limit status to include transaction " << lt << " of account "
                                 << addr.to_hex());
  }

  // Collator should stop if total gas usage exceeds limits, including transactions on special accounts, but without
  // ticktocks and mint/recover.
  // Here Validator checks a weaker condition
  if (!is_special_tx && !trs->gas_limit_overridden && trans_type == block::transaction::Transaction::tr_ord) {
    (account.is_special ? total_special_gas_used_ : total_gas_used_) += trs->gas_used();
  }
  if (total_gas_used_ > block_limits_gas_hard + compute_phase_cfg_.gas_limit) {
    return reject_query(PSTRING() << "gas block limits are exceeded: total_gas_used > gas_limit_hard + trx_gas_limit ("
                                  << "total_gas_used=" << total_gas_used_
                                  << ", gas_limit_hard=" << block_limits_gas_hard
                                  << ", trx_gas_limit=" << compute_phase_cfg_.gas_limit << ")");
  }
  if (total_special_gas_used_ > block_limits_gas_hard + compute_phase_cfg_.special_gas_limit) {
    return reject_query(
        PSTRING() << "gas block limits are exceeded: total_special_gas_used > gas_limit_hard + special_gas_limit ("
                  << "total_special_gas_used=" << total_special_gas_used_
                  << ", gas_limit_hard=" << block_limits_gas_hard
                  << ", special_gas_limit=" << compute_phase_cfg_.special_gas_limit << ")");
  }

  auto trans_root2 = trs->commit(account);
  if (trans_root2.is_null()) {
    return reject_query(PSTRING() << "the re-created transaction " << lt << " for smart contract " << addr.to_hex()
                                  << " could not be committed");
  }
  // now compare the re-created transaction with the one we have
  if (trans_root2->get_hash() != trans_root->get_hash()) {
    if (verbosity >= 3) {
      std::cerr << "original transaction " << lt << " of " << addr.to_hex() << ": ";
      block::gen::t_Transaction.print_ref(std::cerr, trans_root);
      std::cerr << "re-created transaction " << lt << " of " << addr.to_hex() << ": ";
      block::gen::t_Transaction.print_ref(std::cerr, trans_root2);
    }
    return reject_query(PSTRING() << "the transaction " << lt << " of " << addr.to_hex() << " has hash "
                                  << trans_root->get_hash().to_hex()
                                  << " different from that of the recreated transaction "
                                  << trans_root2->get_hash().to_hex());
  }
  block::gen::Transaction::Record trans2;
  block::gen::HASH_UPDATE::Record hash_upd2;
  if (!(tlb::unpack_cell(trans_root2, trans2) &&
        tlb::type_unpack_cell(std::move(trans2.state_update), block::gen::t_HASH_UPDATE_Account, hash_upd2))) {
    return fatal_error(PSTRING() << "cannot unpack the re-created transaction " << lt << " of " << addr.to_hex());
  }
  if (hash_upd2.old_hash != hash_upd.old_hash) {
    return fatal_error(PSTRING() << "the re-created transaction " << lt << " of " << addr.to_hex()
                                 << " is invalid: it starts from account state with different hash");
  }
  if (hash_upd2.new_hash != account.total_state->get_hash().bits()) {
    return fatal_error(
        PSTRING() << "the re-created transaction " << lt << " of " << addr.to_hex()
                  << " is invalid: its claimed new account hash differs from the actual new account state");
  }
  if (hash_upd.new_hash != account.total_state->get_hash().bits()) {
    return reject_query(PSTRING() << "transaction " << lt << " of " << addr.to_hex()
                                  << " is invalid: it claims that the new account state hash is "
                                  << hash_upd.new_hash.to_hex() << " but the re-computed value is "
                                  << hash_upd2.new_hash.to_hex());
  }
  if (!trans.r1.out_msgs->contents_equal(*trans2.r1.out_msgs)) {
    return reject_query(
        PSTRING()
        << "transaction " << lt << " of " << addr.to_hex()
        << " is invalid: it has produced a set of outbound messages different from that listed in the transaction");
  }
  total_burned_ += trs->blackhole_burned;
  // check new balance and value flow
  auto new_balance = account.get_balance();
  block::CurrencyCollection total_fees;
  if (!total_fees.validate_unpack(trans.total_fees)) {
    return reject_query(PSTRING() << "transaction " << lt << " of " << addr.to_hex()
                                  << " has an invalid total_fees value");
  }
  if (old_balance + money_imported != new_balance + money_exported + total_fees + trs->blackhole_burned) {
    return reject_query(
        PSTRING() << "transaction " << lt << " of " << addr.to_hex()
                  << " violates the currency flow condition: old balance=" << old_balance.to_str()
                  << " + imported=" << money_imported.to_str() << " does not equal new balance=" << new_balance.to_str()
                  << " + exported=" << money_exported.to_str() << " + total_fees=" << total_fees.to_str()
                  << (trs->blackhole_burned.is_zero() ? ""
                                                      : PSTRING() << " burned=" << trs->blackhole_burned.to_str()));
  }
  return true;
}

/**
 * Checks if the given input message is a special message.
 * A message is considered special if it recovers fees or mints extra currencies.
 *
 * @param in_msg The input message to be checked.
 *
 * @returns True if the input message is special, False otherwise.
 */
bool TransactionChecker::is_special_in_msg(const vm::CellSlice& in_msg) const {
  return (recover_create_msg_.not_null() && vm::load_cell_slice(recover_create_msg_).contents_equal(in_msg)) ||
         (mint_msg_.not_null() && vm::load_cell_slice(mint_msg_).contents_equal(in_msg));
}

/**
 * Retreives an Account object from the data in the shard state.
 * Accounts are cached in the ValidatorQuery's map.
 * Similar to Collator::make_account()
 *
 * @param addr The 256-bit address of the account.
 *
 * @returns Pointer to the account if found or created successfully.
 *          Returns nullptr if an error occured.
 */
std::unique_ptr<block::Account> TransactionChecker::unpack_account(td::ConstBitPtr addr) {
  auto dict_entry = ps_account_dict_->lookup_extra(addr, 256);
  auto new_acc = make_account_from(addr, std::move(dict_entry.first));
  if (!new_acc) {
    reject_query("cannot load state of account "s + addr.to_hex(256) + " from previous shardchain state");
    return {};
  }
  if (!new_acc->belongs_to_shard(shard_)) {
    reject_query(PSTRING() << "old state of account " << addr.to_hex(256)
                           << " does not really belong to current shard");
    return {};
  }
  return new_acc;
}

/**
 * Creates a new Account object from the given address and serialized account data.
 * Creates a new Account if not found.
 * Similar to Collator::make_account_from()
 *
 * @param addr A pointer to the 256-bit address of the account.
 * @param account A cell slice with an account serialized using ShardAccount TLB-scheme.
 *
 * @returns A unique pointer to the created Account object, or nullptr if the creation failed.
 */
std::unique_ptr<block::Account> TransactionChecker::make_account_from(td::ConstBitPtr addr,
                                                                        Ref<vm::CellSlice> account) {
  auto ptr = std::make_unique<block::Account>(workchain(), addr);
  if (account.is_null()) {
    if (!ptr->init_new(now_)) {
      return nullptr;
    }
  } else if (!ptr->unpack(std::move(account), now_, false)) {
    return nullptr;
  }
  ptr->block_lt = start_lt_;
  return ptr;
}