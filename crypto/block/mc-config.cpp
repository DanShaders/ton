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

    In addition, as a special exception, the copyright holders give permission 
    to link the code of portions of this program with the OpenSSL library. 
    You must obey the GNU General Public License in all respects for all 
    of the code used other than OpenSSL. If you modify file(s) with this 
    exception, you may extend this exception to your version of the file(s), 
    but you are not obligated to do so. If you do not wish to do so, delete this 
    exception statement from your version. If you delete this exception statement 
    from all source files in the program, then also delete it here.

    Copyright 2017-2020 Telegram Systems LLP
*/
#include "mc-config.h"


#include <stack>
#include <algorithm>
#include <compare>
#include <iostream>
#include <string>

#include "block/block.h"
#include "block/block-parse.h"
#include "block/block-auto.h"
#include "common/bitstring.h"
#include "vm/dict.h"
#include "utils/bits.h"
#include "utils/uint128.h"
#include "ton/ton-types.h"
#include "ton/ton-shard.h"
#include "openssl/digest.hpp"
#include "utils/Slice-decl.h"
#include "utils/Slice.h"
#include "utils/check.h"
#include "utils/logging.h"
#include "utils/port/platform.h"
#include "tl/tlblib.hpp"
#include "vm/cells/CellBuilder.h"
#include "vm/excno.hpp"

namespace block {
using namespace std::literals::string_literals;
using td::Ref;

#define DBG(__n) dbg(__n)&&
#define DSTART int __dcnt = 0;
#define DEB DBG(++__dcnt)

static inline bool dbg(int c) TD_UNUSED;
static inline bool dbg(int c) {
  std::cerr << '[' << (char)('0' + c / 10) << (char)('0' + c % 10) << ']';
  return true;
}

Config::Config(Ref<vm::Cell> config_root, const td::Bits256& config_addr, int _mode)
    : mode(_mode), config_addr(config_addr), config_root(std::move(config_root)) {
}


td::Result<std::unique_ptr<ConfigInfo>> ConfigInfo::extract_config(std::shared_ptr<vm::StaticBagOfCellsDb> static_boc,
                                                                   int mode) {
  TRY_RESULT(rc, static_boc->get_root_count());
  if (rc != 1) {
    return td::Status::Error(-668, "Masterchain state BoC is invalid");
  }
  TRY_RESULT(root, static_boc->get_root_cell(0));
  return extract_config(std::move(root), mode);
}

td::Result<std::unique_ptr<ConfigInfo>> ConfigInfo::extract_config(Ref<vm::Cell> mc_state_root, int mode) {
  if (mc_state_root.is_null()) {
    return td::Status::Error("configuration state root cell is null");
  }
  auto config = std::unique_ptr<ConfigInfo>{new ConfigInfo(std::move(mc_state_root), mode)};
  TRY_STATUS(config->unpack_wrapped());
  return std::move(config);
}

ConfigInfo::ConfigInfo(Ref<vm::Cell> mc_state_root, int _mode) : Config(_mode), state_root(std::move(mc_state_root)) {
  block_id.root_hash.set_zero();
  block_id.file_hash.set_zero();
}

td::Status ConfigInfo::unpack_wrapped() {
  try {
    return unpack();
  } catch (vm::VmError& err) {
    return td::Status::Error(PSLICE() << "error unpacking block state header and configuration: " << err.get_msg());
  } catch (vm::VmVirtError& err) {
    return td::Status::Error(PSLICE() << "virtualization error while unpacking block state header and configuration: "
                                      << err.get_msg());
  }
}

td::Status ConfigInfo::unpack() {
  gen::ShardStateUnsplit::Record root_info;
  if (!tlb::unpack_cell(state_root, root_info) || !root_info.global_id) {
    return td::Status::Error("configuration state root cannot be deserialized");
  }
  global_id_ = root_info.global_id;
  block::ShardId shard_id{root_info.shard_id};
  block_id.id = ton::BlockId{ton::ShardIdFull(shard_id), (unsigned)root_info.seq_no};
  block_id.root_hash.set_zero();
  block_id.file_hash.set_zero();
  vert_seqno = root_info.vert_seq_no;
  utime = root_info.gen_utime;
  lt = root_info.gen_lt;
  min_ref_mc_seqno_ = root_info.min_ref_mc_seqno;
  if (!root_info.custom->size_refs()) {
    return td::Status::Error("state does not have a `custom` field with masterchain configuration");
  }
  if (mode & needLibraries) {
    lib_root_ = root_info.r1.libraries->prefetch_ref();
    libraries_dict_ = std::make_unique<vm::Dictionary>(lib_root_, 256);
  }
  if (mode & needAccountsRoot) {
    accounts_root = vm::load_cell_slice_ref(root_info.accounts);
    LOG(DEBUG) << "requested accounts dictionary";
    accounts_dict = std::make_unique<vm::AugmentedDictionary>(accounts_root, 256, block::tlb::aug_ShardAccounts);
    LOG(DEBUG) << "accounts dictionary created";
  }
  state_extra_root_ = root_info.custom->prefetch_ref();
  if (!is_masterchain()) {
    if (mode & (needShardHashes | needValidatorSet | needSpecialSmc | needPrevBlocks | needWorkchainInfo)) {
      return td::Status::Error("cannot extract masterchain-specific configuration data from a non-masterchain state");
    }
    cleanup();
    return td::Status::OK();
  }
  gen::McStateExtra::Record extra_info;
  if (!tlb::unpack_cell(state_extra_root_, extra_info)) {
    vm::load_cell_slice(state_extra_root_).print_rec(std::cerr);
    block::gen::t_McStateExtra.print_ref(std::cerr, state_extra_root_);
    return td::Status::Error("state extra information is invalid");
  }
  gen::ValidatorInfo::Record validator_info;
  if (!tlb::csr_unpack(extra_info.r1.validator_info, validator_info)) {
    return td::Status::Error("validator_info in state extra information is invalid");
  }
  cc_seqno_ = validator_info.catchain_seqno;
  nx_cc_updated = validator_info.nx_cc_updated;
  if ((mode & needShardHashes) && !ShardConfig::unpack(extra_info.shard_hashes)) {
    return td::Status::Error("cannot unpack Shard configuration");
  }
  is_key_state_ = extra_info.r1.after_key_block;
  if (extra_info.r1.last_key_block->size() > 1) {
    auto& cs = extra_info.r1.last_key_block.write();
    block::gen::ExtBlkRef::Record ext_ref;
    if (!(cs.advance(1) && tlb::unpack_exact(cs, ext_ref))) {
      return td::Status::Error("cannot unpack last_key_block from masterchain state");
    }
    last_key_block_.id = ton::BlockId{ton::masterchainId, ton::shardIdAll, ext_ref.seq_no};
    last_key_block_.root_hash = ext_ref.root_hash;
    last_key_block_.file_hash = ext_ref.file_hash;
    last_key_block_lt_ = ext_ref.end_lt;
  } else {
    last_key_block_.invalidate();
    last_key_block_.id.seqno = 0;
    last_key_block_lt_ = 0;
  }
  // unpack configuration
  TRY_STATUS(Config::unpack_wrapped(std::move(extra_info.config)));
  // unpack previous masterchain block collection
  std::unique_ptr<vm::AugmentedDictionary> prev_blocks_dict =
      std::make_unique<vm::AugmentedDictionary>(extra_info.r1.prev_blocks, 32, block::tlb::aug_OldMcBlocksInfo);
  if (block_id.id.seqno) {
    block::gen::ExtBlkRef::Record extref = {};
    auto ref = prev_blocks_dict->lookup(td::BitArray<32>::zero());
    if (!(ref.not_null() && ref.write().advance(1) && tlb::csr_unpack(ref, extref) && !extref.seq_no)) {
      return td::Status::Error("OldMcBlocks in masterchain state does not contain a valid zero state reference");
    }
    zerostate_id_.root_hash = extref.root_hash;
    zerostate_id_.file_hash = extref.file_hash;
  } else {
    zerostate_id_.root_hash.set_zero();
    zerostate_id_.file_hash.set_zero();
  }
  zerostate_id_.workchain = ton::masterchainId;
  if (mode & needPrevBlocks) {
    prev_blocks_dict_ = std::move(prev_blocks_dict);
  }
  // ...
  cleanup();
  return td::Status::OK();
}

td::Status Config::unpack_wrapped(Ref<vm::CellSlice> config_csr) {
  try {
    return unpack(std::move(config_csr));
  } catch (vm::VmError err) {
    return td::Status::Error(PSLICE() << "error unpacking masterchain configuration: " << err.get_msg());
  }
}

td::Status Config::unpack_wrapped() {
  try {
    return unpack();
  } catch (vm::VmError err) {
    return td::Status::Error(PSLICE() << "error unpacking masterchain configuration: " << err.get_msg());
  }
}

td::Status Config::unpack(Ref<vm::CellSlice> config_cs) {
  gen::ConfigParams::Record config_params;
  if (!tlb::csr_unpack(std::move(config_cs), config_params)) {
    return td::Status::Error("cannot unpack ConfigParams");
  }
  config_addr = config_params.config_addr;
  config_root = std::move(config_params.config);
  return unpack();
}

td::Status Config::unpack() {
  if (config_root.is_null()) {
    return td::Status::Error("configuration root not set");
  }
  config_dict = std::make_unique<vm::Dictionary>(config_root, 32);
  if (mode & needValidatorSet) {
    auto vset_res = unpack_validator_set(get_config_param(35, 34));
    if (vset_res.is_error()) {
      return vset_res.move_as_error();
    }
    cur_validators_ = vset_res.move_as_ok();
  }
  if (mode & needSpecialSmc) {
    LOG(DEBUG) << "needSpecialSmc flag set";
    auto param = get_config_param(31);
    if (param.is_null()) {
      special_smc_dict = std::make_unique<vm::Dictionary>(256);
    } else {
      special_smc_dict = std::make_unique<vm::Dictionary>(vm::load_cell_slice_ref(std::move(param)), 256);
      LOG(DEBUG) << "smc dictionary created";
    }
  }
  if (mode & needWorkchainInfo) {
    TRY_RESULT(pair, unpack_workchain_list_ext(get_config_param(12)));
    workchains_ = std::move(pair.first);
    workchains_dict_ = std::move(pair.second);
  }
  if (mode & needCapabilities) {
    auto cell = get_config_param(8);
    if (cell.is_null()) {
      version_ = 0;
      capabilities_ = 0;
    } else {
      block::gen::GlobalVersion::Record gv;
      if (!tlb::unpack_cell(std::move(cell), gv)) {
        return td::Status::Error(
            "cannot extract global blockchain version and capabilities from GlobalVersion in configuration parameter "
            "#8");
      }
      version_ = gv.version;
      capabilities_ = gv.capabilities;
    }
  }
  // ...
  return td::Status::OK();
}

bool Config::foreach_config_param(std::function<bool(int, Ref<vm::Cell>)> scan_func) const {
  if (!config_dict) {
    return false;
  }
  return config_dict->check_for_each([scan_func](Ref<vm::CellSlice> cs_ref, td::ConstBitPtr key, int n) {
    return n == 32 && cs_ref.not_null() && cs_ref->size_ext() == 0x10000 &&
           scan_func((int)key.get_int(n), cs_ref->prefetch_ref());
  });
}

std::unique_ptr<vm::AugmentedDictionary> ConfigInfo::create_accounts_dict() const {
  if (mode & needAccountsRoot) {
    return std::make_unique<vm::AugmentedDictionary>(accounts_root, 256, block::tlb::aug_ShardAccounts);
  } else {
    return nullptr;
  }
}

const vm::AugmentedDictionary& ConfigInfo::get_accounts_dict() const {
  return *accounts_dict;
}

bool ConfigInfo::get_last_key_block(ton::BlockIdExt& blkid, ton::LogicalTime& blklt, bool strict) const {
  if (strict || !is_key_state_) {
    blkid = last_key_block_;
    blklt = last_key_block_lt_;
  } else {
    blkid = block_id;
    blklt = lt;
  }
  return blkid.is_valid();
}

td::Result<std::pair<WorkchainSet, std::unique_ptr<vm::Dictionary>>> Config::unpack_workchain_list_ext(
    Ref<vm::Cell> root) {
  if (root.is_null()) {
    LOG(DEBUG) << "workchain description dictionary is empty (no configuration parameter #12)";
    return std::make_pair(WorkchainSet{}, std::make_unique<vm::Dictionary>(32));
  } else {
    auto wc_dict = std::make_unique<vm::Dictionary>(vm::load_cell_slice_ref(std::move(root)), 32);
    WorkchainSet wc_list;
    LOG(DEBUG) << "workchain description dictionary created";
    if (!(wc_dict->check_for_each([&wc_list](Ref<vm::CellSlice> cs_ref, td::ConstBitPtr key, int n) -> bool {
          ton::WorkchainId wc = ton::WorkchainId(key.get_int(32));
          Ref<WorkchainInfo> wc_info{true};
          return wc_info.unique_write().unpack(wc, cs_ref.write()) && wc_list.emplace(wc, std::move(wc_info)).second;
        }))) {
      return td::Status::Error("cannot unpack WorkchainDescr from masterchain configuration");
    }
    return std::make_pair(std::move(wc_list), std::move(wc_dict));
  }
}

td::Result<std::unique_ptr<ValidatorSet>> Config::unpack_validator_set(Ref<vm::Cell> vset_root) {
  if (vset_root.is_null()) {
    return td::Status::Error("validator set is absent");
  }
  gen::ValidatorSet::Record_validators_ext rec;
  Ref<vm::Cell> dict_root;
  if (!tlb::unpack_cell(vset_root, rec)) {
    gen::ValidatorSet::Record_validators rec0;
    if (!tlb::unpack_cell(std::move(vset_root), rec0)) {
      return td::Status::Error("validator set is invalid");
    }
    rec.utime_since = rec0.utime_since;
    rec.utime_until = rec0.utime_until;
    rec.total = rec0.total;
    rec.main = rec0.main;
    dict_root = vm::Dictionary::construct_root_from(*rec0.list);
    rec.total_weight = 0;
  } else if (rec.total_weight) {
    dict_root = rec.list->prefetch_ref();
  } else {
    return td::Status::Error("validator set cannot have zero total weight");
  }
  vm::Dictionary dict{std::move(dict_root), 16};
  td::BitArray<16> key_buffer;
  auto last = dict.get_minmax_key(key_buffer.bits(), 16, true);
  if (last.is_null() || (int)key_buffer.to_ulong() != rec.total - 1) {
    return td::Status::Error(
        "maximal index in a validator set dictionary must be one less than the total number of validators");
  }
  auto ptr = std::make_unique<ValidatorSet>(rec.utime_since, rec.utime_until, rec.total, rec.main);
  for (int i = 0; i < rec.total; i++) {
    key_buffer.store_ulong(i);
    auto descr_cs = dict.lookup(key_buffer.bits(), 16);
    if (descr_cs.is_null()) {
      return td::Status::Error("indices in a validator set dictionary must be integers 0..total-1");
    }
    gen::ValidatorDescr::Record_validator_addr descr;
    if (!tlb::csr_unpack(descr_cs, descr)) {
      descr.adnl_addr.set_zero();
      if (!(gen::t_ValidatorDescr.unpack_validator(descr_cs.write(), descr.public_key, descr.weight) &&
            descr_cs->empty_ext())) {
        return td::Status::Error(PSLICE() << "validator #" << i
                                          << " has an invalid ValidatorDescr record in the validator set dictionary");
      }
    }
    gen::SigPubKey::Record sig_pubkey;
    if (!tlb::csr_unpack(std::move(descr.public_key), sig_pubkey)) {
      return td::Status::Error(PSLICE() << "validator #" << i
                                        << " has no public key or its public key is in unsupported format");
    }
    if (!descr.weight) {
      return td::Status::Error(PSLICE() << "validator #" << i << " has zero weight");
    }
    if (descr.weight > ~(ptr->total_weight)) {
      return td::Status::Error("total weight of all validators in validator set exceeds 2^64");
    }
    ptr->list.emplace_back(sig_pubkey.pubkey, descr.weight, ptr->total_weight, descr.adnl_addr);
    ptr->total_weight += descr.weight;
  }
  if (rec.total_weight && rec.total_weight != ptr->total_weight) {
    return td::Status::Error("validator set declares incorrect total weight");
  }
  return std::move(ptr);
}

bool Config::set_block_id_ext(const ton::BlockIdExt& block_id_ext) {
  if (block_id.id == block_id_ext.id) {
    block_id = block_id_ext;
    return true;
  } else {
    return false;
  }
}

bool ConfigInfo::set_block_id_ext(const ton::BlockIdExt& block_id_ext) {
  if (!Config::set_block_id_ext(block_id_ext)) {
    return false;
  }
  if (!block_id.seqno()) {
    zerostate_id_.workchain = ton::masterchainId;
    zerostate_id_.root_hash = block_id_ext.root_hash;
    zerostate_id_.file_hash = block_id_ext.file_hash;
  }
  reset_mc_hash();
  return true;
}

void ConfigInfo::cleanup() {
  if (!(mode & needStateRoot)) {
    state_root.clear();
  }
  if (!(mode & needStateExtraRoot)) {
    state_extra_root_.clear();
  }
}

Ref<vm::Cell> Config::get_config_param(int idx) const {
  if (!config_dict) {
    return {};
  }
  return config_dict->lookup_ref(td::BitArray<32>{idx});
}

Ref<vm::Cell> Config::get_config_param(int idx, int idx2) const {
  if (!config_dict) {
    return {};
  }
  auto res = config_dict->lookup_ref(td::BitArray<32>{idx});
  if (res.not_null()) {
    return res;
  } else {
    return config_dict->lookup_ref(td::BitArray<32>{idx2});
  }
}

td::Result<std::unique_ptr<BlockLimits>> Config::get_block_limits(bool is_masterchain) const {
  int param = (is_masterchain ? 22 : 23);
  auto cell = get_config_param(param);
  if (cell.is_null()) {
    return td::Status::Error(PSTRING() << "configuration parameter " << param << " with block limits is absent");
  }
  auto cs = vm::load_cell_slice(std::move(cell));
  auto ptr = std::make_unique<BlockLimits>();
  if (!ptr->deserialize(cs) || cs.size_ext()) {
    return td::Status::Error(PSTRING() << "cannot deserialize BlockLimits obtained from configuration parameter "
                                       << param);
  }
  return std::move(ptr);
}

td::Result<std::vector<StoragePrices>> Config::get_storage_prices() const {
  auto cell = get_config_param(18);
  std::vector<StoragePrices> res;
  if (cell.is_null()) {
    return td::Status::Error("configuration parameter 18 with storage prices dictionary is absent");
  }
  vm::Dictionary dict{std::move(cell), 32};
  if (!dict.check_for_each([&res](Ref<vm::CellSlice> cs_ref, td::ConstBitPtr key, int n) -> bool {
        auto r_prices = do_get_one_storage_prices(*cs_ref);
        if (r_prices.is_error()) {
          return false;
        }
        res.push_back(r_prices.move_as_ok());
        if (res.back().valid_since != key.get_uint(n)) {
          return false;
        }
        return true;
      })) {
    return td::Status::Error("invalid storage prices dictionary in configuration parameter 18");
  }
  return std::move(res);
}

td::Result<StoragePrices> Config::do_get_one_storage_prices(vm::CellSlice cs) {
  block::gen::StoragePrices::Record data;
  if (!tlb::unpack(cs, data)) {
    return td::Status::Error("invalid storage prices dictionary in configuration parameter 18");
  }
  return StoragePrices{data.utime_since, data.bit_price_ps, data.cell_price_ps, data.mc_bit_price_ps,
                       data.mc_cell_price_ps};
}

td::Result<GasLimitsPrices> Config::do_get_gas_limits_prices(vm::CellSlice cs, int id) {
  GasLimitsPrices res;
  vm::CellSlice cs0 = cs;
  block::gen::GasLimitsPrices::Record_gas_flat_pfx flat;
  if (tlb::unpack(cs, flat)) {
    cs = *flat.other;
    res.flat_gas_limit = flat.flat_gas_limit;
    res.flat_gas_price = flat.flat_gas_price;
  } else {
    cs = cs0;
  }
  auto f = [&](const auto& r, td::uint64 spec_limit) {
    res.gas_limit = r.gas_limit;
    res.special_gas_limit = spec_limit;
    res.gas_credit = r.gas_credit;
    res.gas_price = r.gas_price;
    res.freeze_due_limit = r.freeze_due_limit;
    res.delete_due_limit = r.delete_due_limit;
  };
  block::gen::GasLimitsPrices::Record_gas_prices_ext rec;
  if (tlb::unpack(cs, rec)) {
    f(rec, rec.special_gas_limit);
  } else {
    block::gen::GasLimitsPrices::Record_gas_prices rec0;
    if (tlb::unpack(cs = cs0, rec0)) {
      f(rec0, rec0.gas_limit);
    } else {
      return td::Status::Error(PSLICE() << "configuration parameter " << id
                                        << " with gas prices is invalid - can't parse");
    }
  }
  return res;
}

td::Result<GasLimitsPrices> Config::get_gas_limits_prices(bool is_masterchain) const {
  auto id = is_masterchain ? 20 : 21;
  auto cell = get_config_param(id);
  if (cell.is_null()) {
    return td::Status::Error(PSLICE() << "configuration parameter " << id << " with gas prices is absent");
  }
  return do_get_gas_limits_prices(vm::load_cell_slice(cell), id);
}

td::Result<MsgPrices> Config::get_msg_prices(bool is_masterchain) const {
  auto id = is_masterchain ? 24 : 25;
  auto cell = get_config_param(id);
  if (cell.is_null()) {
    return td::Status::Error(PSLICE() << "configuration parameter " << id << " with msg prices is absent");
  }
  return do_get_msg_prices(vm::load_cell_slice(cell), id);
}

td::Result<MsgPrices> Config::do_get_msg_prices(vm::CellSlice cs, int id) {
  block::gen::MsgForwardPrices::Record rec;
  if (!tlb::unpack(cs, rec)) {
    return td::Status::Error(PSLICE() << "configuration parameter " << id
                                      << " with msg prices is invalid - can't parse");
  }
  return MsgPrices(rec.lump_price, rec.bit_price, rec.cell_price, rec.ihr_price_factor, rec.first_frac, rec.next_frac);
}

void McShardHash::set_fsm(FsmState fsm, ton::UnixTime fsm_utime, ton::UnixTime fsm_interval) {
  fsm_ = fsm;
  fsm_utime_ = fsm_utime;
  fsm_interval_ = fsm_interval;
}

Ref<McShardHash> McShardHash::unpack(vm::CellSlice& cs, ton::ShardIdFull id) {
  int tag = gen::t_ShardDescr.get_tag(cs);
  if (tag < 0) {
    return {};
  }
  auto create = [&id](auto& descr, Ref<vm::CellSlice> fees, Ref<vm::CellSlice> funds) {
    CurrencyCollection fees_collected, funds_created;
    if (!(fees_collected.unpack(std::move(fees)) && funds_created.unpack(std::move(funds)))) {
      return Ref<McShardHash>{};
    }
    return td::make_ref<McShardHash>(ton::BlockId{id, (unsigned)descr.seq_no}, descr.start_lt, descr.end_lt,
                                     descr.gen_utime, descr.root_hash, descr.file_hash, fees_collected, funds_created,
                                     descr.reg_mc_seqno, descr.min_ref_mc_seqno, descr.next_catchain_seqno,
                                     descr.next_validator_shard, /* descr.nx_cc_updated */ false, descr.before_split,
                                     descr.before_merge, descr.want_split, descr.want_merge);
  };
  Ref<McShardHash> res;
  Ref<vm::CellSlice> fsm_cs;
  if (tag == gen::ShardDescr::shard_descr) {
    gen::ShardDescr::Record_shard_descr descr;
    if (tlb::unpack_exact(cs, descr)) {
      fsm_cs = std::move(descr.split_merge_at);
      res = create(descr, std::move(descr.fees_collected), std::move(descr.funds_created));
    }
  } else {
    gen::ShardDescr::Record_shard_descr_new descr;
    if (tlb::unpack_exact(cs, descr)) {
      fsm_cs = std::move(descr.split_merge_at);
      res = create(descr, std::move(descr.r1.fees_collected), std::move(descr.r1.funds_created));
    }
  }
  if (res.is_null()) {
    return res;
  }
  McShardHash& sh = res.unique_write();
  switch (gen::t_FutureSplitMerge.get_tag(*fsm_cs)) {
    case gen::FutureSplitMerge::fsm_none:
      return res;
    case gen::FutureSplitMerge::fsm_split:
      if (gen::t_FutureSplitMerge.unpack_fsm_split(fsm_cs.write(), sh.fsm_utime_, sh.fsm_interval_)) {
        sh.fsm_ = FsmState::fsm_split;
        return res;
      }
      break;
    case gen::FutureSplitMerge::fsm_merge:
      if (gen::t_FutureSplitMerge.unpack_fsm_merge(fsm_cs.write(), sh.fsm_utime_, sh.fsm_interval_)) {
        sh.fsm_ = FsmState::fsm_merge;
        return res;
      }
      break;
    default:
      break;
  }
  return {};
}

bool McShardHash::pack(vm::CellBuilder& cb) const {
  if (!(is_valid()                                        // (validate)
        && cb.store_long_bool(10, 4)                      // shard_descr_new#a
        && cb.store_long_bool(blk_.id.seqno, 32)          // seq_no:uint32
        && cb.store_long_bool(reg_mc_seqno_, 32)          // reg_mc_seqno:uint32
        && cb.store_long_bool(start_lt_, 64)              // start_lt:uint64
        && cb.store_long_bool(end_lt_, 64)                // end_lt:uint64
        && cb.store_bits_bool(blk_.root_hash)             // root_hash:bits256
        && cb.store_bits_bool(blk_.file_hash)             // file_hash:bits256
        && cb.store_bool_bool(before_split_)              // before_split:Bool
        && cb.store_bool_bool(before_merge_)              // before_merge:Bool
        && cb.store_bool_bool(want_split_)                // want_split:Bool
        && cb.store_bool_bool(want_merge_)                // want_merge:Bool
        && cb.store_bool_bool(false)                      // nx_cc_updated:Bool
        && cb.store_long_bool(0, 3)                       // flags:(## 3) { flags = 0 }
        && cb.store_long_bool(next_catchain_seqno_, 32)   // next_catchain_seqno:uint32
        && cb.store_long_bool(next_validator_shard_, 64)  // next_validator_shard:uint64
        && cb.store_long_bool(min_ref_mc_seqno_, 32)      // min_ref_mc_seqno:uint32
        && cb.store_long_bool(gen_utime_, 32)             // gen_utime:uint32
        )) {
    return false;
  }
  bool ok;
  switch (fsm_) {  // split_merge_at:FutureSplitMerge
    case FsmState::fsm_none:
      ok = gen::t_FutureSplitMerge.pack_fsm_none(cb);
      break;
    case FsmState::fsm_split:
      ok = gen::t_FutureSplitMerge.pack_fsm_split(cb, fsm_utime_, fsm_interval_);
      break;
    case FsmState::fsm_merge:
      ok = gen::t_FutureSplitMerge.pack_fsm_merge(cb, fsm_utime_, fsm_interval_);
      break;
    default:
      return false;
  }
  vm::CellBuilder cb2;
  return ok                                             // split_merge_at:FutureSplitMerge
         && fees_collected_.store_or_zero(cb2)          // ^[ fees_collected:CurrencyCollection
         && funds_created_.store_or_zero(cb2)           //    funds_created:CurrencyCollection ]
         && cb.store_builder_ref_bool(std::move(cb2));  // = ShardDescr;
}


McShardDescr::McShardDescr(const McShardDescr& other)
    : McShardHash(other)
    , block_root(other.block_root)
    , state_root(other.state_root)
    , processed_upto(other.processed_upto) {
  set_queue_root(other.outmsg_root);
}

McShardDescr& McShardDescr::operator=(const McShardDescr& other) {
  McShardHash::operator=(other);
  block_root = other.block_root;
  outmsg_root = other.outmsg_root;
  processed_upto = other.processed_upto;
  set_queue_root(other.outmsg_root);
  return *this;
}

Ref<McShardDescr> McShardDescr::from_state(ton::BlockIdExt blkid, Ref<vm::Cell> state_root) {
  if (state_root.is_null()) {
    return {};
  }
  block::gen::ShardStateUnsplit::Record info;
  block::gen::OutMsgQueueInfo::Record qinfo;
  block::ShardId shard;
  if (!(tlb::unpack_cell(state_root, info) && shard.deserialize(info.shard_id.write()) &&
        tlb::unpack_cell(info.out_msg_queue_info, qinfo))) {
    LOG(DEBUG) << "cannot create McShardDescr from a shardchain state";
    return {};
  }
  if (ton::ShardIdFull(shard) != ton::ShardIdFull(blkid) || info.seq_no != blkid.seqno()) {
    LOG(DEBUG) << "shard id mismatch, cannot construct McShardDescr";
    return {};
  }
  auto res = Ref<McShardDescr>(true, blkid.id, info.gen_lt, info.gen_lt, info.gen_utime, blkid.root_hash,
                               blkid.file_hash, CurrencyCollection{}, CurrencyCollection{}, ~0U, info.min_ref_mc_seqno,
                               0, shard.shard_pfx, false, info.before_split);
  res.unique_write().state_root = state_root;
  res.unique_write().set_queue_root(qinfo.out_queue->prefetch_ref(0));
  return res;
}

bool McShardDescr::set_queue_root(Ref<vm::Cell> queue_root) {
  outmsg_root = std::move(queue_root);
  out_msg_queue = std::make_unique<vm::AugmentedDictionary>(outmsg_root, 352, block::tlb::aug_OutMsgQueue);
  return true;
}

void McShardDescr::disable() {
  block_root.clear();
  state_root.clear();
  outmsg_root.clear();
  out_msg_queue.reset();
  processed_upto.reset();
  McShardHash::disable();
}

void ConfigInfo::reset_mc_hash() {
  if (block_id.is_masterchain() && !block_id.root_hash.is_zero()) {
    // TODO: use block_start_lt instead of lt if available
    set_mc_hash(Ref<McShardHash>(true, block_id.id, lt, lt, utime, block_id.root_hash, block_id.file_hash));
  } else {
    set_mc_hash(Ref<McShardHash>{});
  }
}

Ref<vm::CellSlice> ShardConfig::get_root_csr() const {
  if (!shard_hashes_dict_) {
    return {};
  }
  return shard_hashes_dict_->get_root();
}

bool ShardConfig::unpack(Ref<vm::Cell> shard_hashes, Ref<McShardHash> mc_shard_hash) {
  shard_hashes_ = std::move(shard_hashes);
  mc_shard_hash_ = std::move(mc_shard_hash);
  return init();
}

bool ShardConfig::unpack(Ref<vm::CellSlice> shard_hashes, Ref<McShardHash> mc_shard_hash) {
  shard_hashes_ = shard_hashes->prefetch_ref();
  mc_shard_hash_ = std::move(mc_shard_hash);
  return init();
}

bool ShardConfig::init() {
  shard_hashes_dict_ = std::make_unique<vm::Dictionary>(shard_hashes_, 32);
  valid_ = true;
  return true;
}

ShardConfig::ShardConfig(const ShardConfig& other)
    : shard_hashes_(other.shard_hashes_), mc_shard_hash_(other.mc_shard_hash_) {
  init();
}

bool ShardConfig::get_shard_hash_raw_from(vm::Dictionary& dict, vm::CellSlice& cs, ton::ShardIdFull id,
                                          ton::ShardIdFull& true_id, bool exact, Ref<vm::Cell>* leaf) {
  if (id.is_masterchain() || !id.is_valid()) {
    return false;
  }
  auto root = dict.lookup_ref(td::BitArray<32>{id.workchain});
  if (root.is_null()) {
    return false;
  }
  unsigned long long z = id.shard, m = std::numeric_limits<unsigned long long>::max();
  int len = id.pfx_len();
  while (true) {
    cs.load(vm::NoVmOrd(), leaf ? root : std::move(root));
    int t = (int)cs.fetch_ulong(1);
    if (t < 0) {
      return false;  // throw DictError ?
    } else if (!t) {
      if (len && exact) {
        return false;
      }
      true_id = ton::ShardIdFull{id.workchain, (id.shard | m) - (m >> 1)};
      if (leaf) {
        *leaf = std::move(root);
      }
      return true;
    }
    if (!len || cs.size_ext() != 0x20000) {
      return false;  // throw DictError in the second case?
    }
    root = cs.prefetch_ref((unsigned)(z >> 63));
    z <<= 1;
    --len;
    m >>= 1;
  }
}

bool ShardConfig::get_shard_hash_raw(vm::CellSlice& cs, ton::ShardIdFull id, ton::ShardIdFull& true_id,
                                     bool exact) const {
  return shard_hashes_dict_ && get_shard_hash_raw_from(*shard_hashes_dict_, cs, id, true_id, exact);
}

Ref<McShardHash> ShardConfig::get_shard_hash(ton::ShardIdFull id, bool exact) const {
  if (id.is_masterchain()) {
    return (!exact || id.shard == ton::shardIdAll) ? get_mc_hash() : Ref<McShardHash>{};
  }
  ton::ShardIdFull true_id;
  vm::CellSlice cs;
  if (get_shard_hash_raw(cs, id, true_id, exact)) {
    // block::gen::t_ShardDescr.print(std::cerr, vm::CellSlice{cs});
    return McShardHash::unpack(cs, true_id);
  } else {
    return {};
  }
}

ton::LogicalTime ShardConfig::get_shard_end_lt_ext(ton::AccountIdPrefixFull acc, ton::ShardIdFull& actual_shard) const {
  if (!acc.is_valid()) {
    actual_shard.workchain = ton::workchainInvalid;
    return 0;
  }
  if (acc.is_masterchain()) {
    actual_shard = ton::ShardIdFull(ton::masterchainId);
    CHECK(mc_shard_hash_.not_null());
    return mc_shard_hash_->end_lt_;
  }
  vm::CellSlice cs;
  unsigned long long end_lt;
  return get_shard_hash_raw(cs, acc.as_leaf_shard(), actual_shard, false)  // lookup ShardDescr containing acc
                 && cs.advance(4 + 128)              // shard_descr#b seq_no:uint32 reg_mc_seqno:uint32 start_lt:uint64
                 && cs.fetch_ulong_bool(64, end_lt)  // end_lt:uint64
             ? end_lt
             : 0;
}

ton::LogicalTime ShardConfig::get_shard_end_lt(ton::AccountIdPrefixFull acc) const {
  ton::ShardIdFull tmp;
  return get_shard_end_lt_ext(acc, tmp);
}

bool ShardConfig::contains(ton::BlockIdExt blkid) const {
  auto entry = get_shard_hash(blkid.shard_full());
  return entry.not_null() && entry->blk_ == blkid;
}

std::vector<ton::BlockId> ShardConfig::get_shard_hash_ids(
    const std::function<bool(ton::ShardIdFull, bool)>& filter) const {
  if (!shard_hashes_dict_) {
    return {};
  }
  std::vector<ton::BlockId> res;
  bool mcout = mc_shard_hash_.is_null() || !mc_shard_hash_->seqno();  // include masterchain as a shard if seqno > 0
  bool ok = shard_hashes_dict_->check_for_each(
      [&res, &mcout, mc_shard_hash_ = mc_shard_hash_, &filter](Ref<vm::CellSlice> cs_ref, td::ConstBitPtr key,
                                                               int n) -> bool {
        int workchain = (int)key.get_int(n);
        if (workchain >= 0 && !mcout) {
          if (filter(ton::ShardIdFull{ton::masterchainId}, true)) {
            res.emplace_back(mc_shard_hash_->blk_.id);
          }
          mcout = true;
        }
        if (!cs_ref->have_refs()) {
          return false;
        }
        std::stack<std::pair<Ref<vm::Cell>, unsigned long long>> stack;
        stack.emplace(cs_ref->prefetch_ref(), ton::shardIdAll);
        while (!stack.empty()) {
          vm::CellSlice cs{vm::NoVmOrd(), std::move(stack.top().first)};
          unsigned long long shard = stack.top().second;
          stack.pop();
          int t = (int)cs.fetch_ulong(1);
          if (t < 0) {
            return false;
          }
          if (!filter(ton::ShardIdFull{workchain, shard}, !t)) {
            continue;
          }
          if (!t) {
            if (!(cs.advance(4) && cs.have(32))) {
              return false;
            }
            res.emplace_back(workchain, shard, (int)cs.prefetch_ulong(32));
            continue;
          }
          unsigned long long delta = (td::lower_bit64(shard) >> 1);
          if (!delta || cs.size_ext() != 0x20000) {
            return false;
          }
          stack.emplace(cs.prefetch_ref(1), shard + delta);
          stack.emplace(cs.prefetch_ref(0), shard - delta);
        }
        return true;
      },
      true);
  if (!ok) {
    return {};
  }
  if (!mcout && filter(ton::ShardIdFull{ton::masterchainId}, true)) {
    res.emplace_back(mc_shard_hash_->blk_.id);
  }
  return res;
}

std::vector<ton::BlockId> ShardConfig::get_shard_hash_ids(bool skip_mc) const {
  return get_shard_hash_ids(
      [skip_mc](ton::ShardIdFull shard, bool) -> bool { return !(skip_mc && shard.is_masterchain()); });
}

std::vector<ton::BlockId> ShardConfig::get_intersecting_shard_hash_ids(ton::ShardIdFull myself) const {
  return get_shard_hash_ids(
      [myself](ton::ShardIdFull shard, bool) -> bool { return ton::shard_intersects(myself, shard); });
}

std::vector<ton::BlockId> ShardConfig::get_neighbor_shard_hash_ids(ton::ShardIdFull myself) const {
  return get_shard_hash_ids([myself](ton::ShardIdFull shard, bool) -> bool { return is_neighbor(myself, shard); });
}

std::vector<ton::BlockId> ShardConfig::get_proper_neighbor_shard_hash_ids(ton::ShardIdFull myself) const {
  return get_shard_hash_ids([myself](ton::ShardIdFull shard, bool leaf) -> bool {
    return is_neighbor(myself, shard) && !(leaf && ton::shard_intersects(myself, shard));
  });
}

bool ShardConfig::is_neighbor(ton::ShardIdFull x, ton::ShardIdFull y) {
  if (x.is_masterchain() || y.is_masterchain()) {
    return true;
  }
  unsigned long long xs = x.shard, ys = y.shard;
  unsigned long long xl = td::lower_bit64(xs), yl = td::lower_bit64(ys);
  unsigned long long z = (xs ^ ys) & td::bits_negate64(std::max(xl, yl) << 1);
  if (!z) {
    return true;
  }
  if (x.workchain != y.workchain) {
    return false;
  }
  int c1 = (td::count_leading_zeroes_non_zero64(z) >> 2);
  int c2 = (td::count_trailing_zeroes_non_zero64(z) >> 2);
  return c1 + c2 == 15;
}

bool ShardConfig::has_workchain(ton::WorkchainId workchain) const {
  return shard_hashes_dict_ && shard_hashes_dict_->key_exists(td::BitArray<32>{workchain});
}

std::vector<ton::WorkchainId> ShardConfig::get_workchains() const {
  if (!shard_hashes_dict_) {
    return {};
  }
  std::vector<ton::WorkchainId> res;
  if (!shard_hashes_dict_->check_for_each([&res](Ref<vm::CellSlice> val, td::ConstBitPtr key, int n) {
        CHECK(n == 32);
        ton::WorkchainId w = (int)key.get_int(32);
        res.push_back(w);
        return true;
      })) {
    return {};
  }
  return res;
}


static bool btree_set(Ref<vm::Cell>& root, ton::ShardId shard, Ref<vm::Cell> value) {
  if (!shard) {
    return false;
  }
  if (shard == ton::shardIdAll) {
    root = value;
    return true;
  }
  auto cs = vm::load_cell_slice(std::move(root));
  if (cs.size_ext() != 0x20001 || cs.prefetch_ulong(1) != 1) {
    return false;  // branch does not exist
  }
  Ref<vm::Cell> left = cs.prefetch_ref(0), right = cs.prefetch_ref(1);
  if (!(btree_set(shard & (1ULL << 63) ? right : left, shard << 1, std::move(value)))) {
    return false;
  }
  vm::CellBuilder cb;
  return cb.store_bool_bool(true)                // bt_node$1
         && cb.store_ref_bool(std::move(left))   // left:^(BinTree ShardDescr)
         && cb.store_ref_bool(std::move(right))  // right:^(BinTree ShardDescr)
         && cb.finalize_to(root);                // = BinTree ShardDescr
}

bool ShardConfig::set_shard_info(ton::ShardIdFull shard, Ref<vm::Cell> value) {
  if (!gen::t_BinTree_ShardDescr.validate_ref(1024, value)) {
    LOG(ERROR) << "attempting to store an invalid (BinTree ShardDescr) at shard configuration position "
               << shard.to_str();
    gen::t_BinTree_ShardDescr.print_ref(std::cerr, value);
    vm::load_cell_slice(value).print_rec(std::cerr);
    return false;
  }
  auto root = shard_hashes_dict_->lookup_ref(td::BitArray<32>{shard.workchain});
  if (root.is_null()) {
    LOG(ERROR) << "attempting to store a new ShardDescr for shard " << shard.to_str() << " in an undefined workchain";
    return false;
  }
  if (!btree_set(root, shard.shard, value)) {
    LOG(ERROR) << "error while storing a new ShardDescr for shard " << shard.to_str() << " into shard configuration";
    return false;
  }
  if (!shard_hashes_dict_->set_ref(td::BitArray<32>{shard.workchain}, std::move(root),
                                   vm::Dictionary::SetMode::Replace)) {
    return false;
  }
  auto ins = shards_updated_.insert(shard);
  CHECK(ins.second);
  return true;
}

bool Config::is_special_smartcontract(const ton::StdSmcAddress& addr) const {
  CHECK(special_smc_dict);
  return special_smc_dict->lookup(addr).not_null() || addr == config_addr;
}

td::Result<std::vector<std::pair<ton::StdSmcAddress, int>>> ConfigInfo::get_special_ticktock_smartcontracts(
    int tick_tock) const {
  if (!special_smc_dict) {
    return td::Status::Error(-666, "configuration loaded without fundamental smart contract list");
  }
  if (!accounts_dict) {
    return td::Status::Error(-666, "state loaded without accounts information");
  }
  std::vector<std::pair<ton::StdSmcAddress, int>> res;
  if (!special_smc_dict->check_for_each(
          [this, &res, tick_tock](Ref<vm::CellSlice> cs_ref, td::ConstBitPtr key, int n) -> bool {
            if (cs_ref->size_ext() || n != 256) {
              return false;
            }
            int tt = get_smc_tick_tock(key);
            if (tt < -1) {
              return false;
            }
            if (tt >= 0 && (tt & tick_tock) != 0) {
              res.emplace_back(key, tt);
            }
            return true;
          })) {
    return td::Status::Error(-666,
                             "invalid fundamental smart contract set in configuration parameter 31, or unable to "
                             "recover tick-tock value from one of them");
  }
  return std::move(res);
}

int ConfigInfo::get_smc_tick_tock(td::ConstBitPtr smc_addr) const {
  if (!accounts_dict) {
    return -2;
  }
  auto acc_csr = accounts_dict->lookup(smc_addr, 256);
  Ref<vm::Cell> acc_cell;
  if (acc_csr.is_null() || !acc_csr->prefetch_ref_to(acc_cell)) {
    return -1;
  }
  auto acc_cs = vm::load_cell_slice(std::move(acc_cell));
  if (block::gen::t_Account.get_tag(acc_cs) == block::gen::Account::account_none) {
    return 0;
  }
  block::gen::Account::Record_account acc;
  block::gen::AccountStorage::Record storage;
  int ticktock;
  return (tlb::unpack_exact(acc_cs, acc) && tlb::csr_unpack(acc.storage, storage) &&
          block::tlb::t_AccountState.get_ticktock(storage.state.write(), ticktock))
             ? ticktock
             : -2;
}

void validator_set_descr::incr_seed() {
  for (int i = 31; i >= 0 && !++(seed[i]); --i) {
  }
}

void validator_set_descr::hash_to(unsigned char hash_buffer[64]) const {
  digest::hash_str<digest::SHA512>(hash_buffer, (const void*)this, sizeof(*this));
}

inline bool operator<(td::uint64 pos, const ValidatorDescr& descr) {
  return pos < descr.cum_weight;
}

td::Result<SizeLimitsConfig> Config::get_size_limits_config() const {
  td::Ref<vm::Cell> param = get_config_param(43);
  if (param.is_null()) {
    return do_get_size_limits_config({});
  }
  return do_get_size_limits_config(vm::load_cell_slice_ref(param));
}

td::Result<SizeLimitsConfig> Config::do_get_size_limits_config(td::Ref<vm::CellSlice> cs) {
  SizeLimitsConfig limits;
  if (cs.is_null()) {
    return limits;  // default values
  }
  auto unpack_v1 = [&](auto& rec) {
    limits.max_msg_bits = rec.max_msg_bits;
    limits.max_msg_cells = rec.max_msg_cells;
    limits.max_library_cells = rec.max_library_cells;
    limits.max_vm_data_depth = static_cast<td::uint16>(rec.max_vm_data_depth);
    limits.ext_msg_limits.max_size = rec.max_ext_msg_size;
    limits.ext_msg_limits.max_depth = static_cast<td::uint16>(rec.max_ext_msg_depth);
  };

  auto unpack_v2 = [&](auto& rec) {
    unpack_v1(rec);
    limits.max_acc_state_bits = rec.max_acc_state_bits;
    limits.max_acc_state_cells = rec.max_acc_state_cells;
    limits.max_acc_public_libraries = rec.max_acc_public_libraries;
    limits.defer_out_queue_size_limit = rec.defer_out_queue_size_limit;
  };
  gen::SizeLimitsConfig::Record_size_limits_config rec_v1;
  gen::SizeLimitsConfig::Record_size_limits_config_v2 rec_v2;
  if (tlb::csr_unpack(cs, rec_v1)) {
    unpack_v1(rec_v1);
  } else if (tlb::csr_unpack(cs, rec_v2)) {
    unpack_v2(rec_v2);
  } else {
    return td::Status::Error("configuration parameter 43 is invalid");
  }
  return limits;
}

std::unique_ptr<vm::Dictionary> Config::get_suspended_addresses(ton::UnixTime now) const {
  td::Ref<vm::Cell> param = get_config_param(44);
  gen::SuspendedAddressList::Record rec;
  if (param.is_null() || !tlb::unpack_cell(param, rec) || rec.suspended_until <= now) {
    return {};
  }
  return std::make_unique<vm::Dictionary>(rec.addresses->prefetch_ref(), 288);
}

BurningConfig Config::get_burning_config() const {
  td::Ref<vm::Cell> param = get_config_param(5);
  gen::BurningConfig::Record rec;
  if (param.is_null() || !tlb::unpack_cell(param, rec)) {
    return {};
  }
  BurningConfig c;
  c.fee_burn_num = rec.fee_burn_num;
  c.fee_burn_denom = rec.fee_burn_denom;
  vm::CellSlice& addr = rec.blackhole_addr.write();
  if (addr.fetch_long(1)) {
    td::Bits256 x;
    addr.fetch_bits_to(x.bits(), 256);
    c.blackhole_addr = x;
  }
  return c;
}

td::Ref<vm::Tuple> Config::get_unpacked_config_tuple(ton::UnixTime now) const {
  auto get_param = [&](td::int32 idx) -> vm::StackEntry {
    auto cell = get_config_param(idx);
    if (cell.is_null()) {
      return {};
    }
    return vm::load_cell_slice_ref(cell);
  };
  auto get_current_storage_prices = [&]() -> vm::StackEntry {
    auto cell = get_config_param(18);
    if (cell.is_null()) {
      return {};
    }
    vm::StackEntry res;
    vm::Dictionary dict{std::move(cell), 32};
    dict.check_for_each([&](Ref<vm::CellSlice> cs_ref, td::ConstBitPtr key, int n) -> bool {
      auto utime_since = key.get_uint(n);
      if (now >= utime_since) {
        res = std::move(cs_ref);
        return true;
      }
      return false;
    });
    return res;
  };
  std::vector<vm::StackEntry> tuple;
  tuple.push_back(get_current_storage_prices());  // storage_prices
  tuple.push_back(get_param(19));                 // global_id
  tuple.push_back(get_param(20));                 // config_mc_gas_prices
  tuple.push_back(get_param(21));                 // config_gas_prices
  tuple.push_back(get_param(24));                 // config_mc_fwd_prices
  tuple.push_back(get_param(25));                 // config_fwd_prices
  tuple.push_back(get_param(43));                 // size_limits_config
  return td::make_cnt_ref<std::vector<vm::StackEntry>>(std::move(tuple));
}

PrecompiledContractsConfig Config::get_precompiled_contracts_config() const {
  PrecompiledContractsConfig c;
  td::Ref<vm::Cell> param = get_config_param(45);
  gen::PrecompiledContractsConfig::Record rec;
  if (param.is_null() || !tlb::unpack_cell(param, rec)) {
    return c;
  }
  c.list = vm::Dictionary{rec.list->prefetch_ref(), 256};
  return c;
}

bool WorkchainInfo::unpack(ton::WorkchainId wc, vm::CellSlice& cs) {
  workchain = ton::workchainInvalid;
  if (wc == ton::workchainInvalid) {
    return false;
  }
  auto unpack_v1 = [this](auto& info) {
    enabled_since = info.enabled_since;
    monitor_min_split = info.monitor_min_split;
    min_split = info.min_split;
    max_split = info.max_split;
    basic = info.basic;
    active = info.active;
    accept_msgs = info.accept_msgs;
    flags = info.flags;
    zerostate_root_hash = info.zerostate_root_hash;
    zerostate_file_hash = info.zerostate_file_hash;
    version = info.version;
    if (basic) {
      min_addr_len = max_addr_len = addr_len_step = 256;
    } else {
      block::gen::WorkchainFormat::Record_wfmt_ext ext;
      if (!tlb::csr_type_unpack(info.format, block::gen::WorkchainFormat{basic}, ext)) {
        return false;
      }
      min_addr_len = ext.min_addr_len;
      max_addr_len = ext.max_addr_len;
      addr_len_step = ext.addr_len_step;
    }
    return true;
  };
  auto unpack_v2 = [&, this](auto& info) {
    if (!unpack_v1(info)) {
      return false;
    }
    block::gen::WcSplitMergeTimings::Record rec;
    if (!tlb::csr_unpack(info.split_merge_timings, rec)) {
      return false;
    }
    split_merge_delay = rec.split_merge_delay;
    split_merge_interval = rec.split_merge_interval;
    min_split_merge_interval = rec.min_split_merge_interval;
    max_split_merge_delay = rec.max_split_merge_delay;
    return true;
  };
  block::gen::WorkchainDescr::Record_workchain info_v1;
  block::gen::WorkchainDescr::Record_workchain_v2 info_v2;
  vm::CellSlice cs0 = cs;
  if (tlb::unpack(cs, info_v1)) {
    if (!unpack_v1(info_v1)) {
      return false;
    }
  } else if (tlb::unpack(cs = cs0, info_v2)) {
    if (!unpack_v2(info_v2)) {
      return false;
    }
  } else {
    return false;
  }
  workchain = wc;
  LOG(DEBUG) << "unpacked info for workchain " << wc << ": basic=" << basic << ", active=" << active
             << ", accept_msgs=" << accept_msgs << ", min_split=" << min_split << ", max_split=" << max_split;
  return true;
}

Ref<WorkchainInfo> Config::get_workchain_info(ton::WorkchainId workchain_id) const {
  if (!workchains_dict_) {
    return {};
  }
  auto it = workchains_.find(workchain_id);
  if (it == workchains_.end()) {
    return {};
  } else {
    return it->second;
  }
}

bool ConfigInfo::get_old_mc_block_id(ton::BlockSeqno seqno, ton::BlockIdExt& blkid, ton::LogicalTime* end_lt) const {
  if (block_id.is_valid() && seqno == block_id.id.seqno) {
    blkid = block_id;
    if (end_lt) {
      *end_lt = lt;
    }
    return true;
  } else {
    return block::get_old_mc_block_id(prev_blocks_dict_.get(), seqno, blkid, end_lt);
  }
}

bool ConfigInfo::check_old_mc_block_id(const ton::BlockIdExt& blkid, bool strict) const {
  return (!strict && blkid.id.seqno == block_id.id.seqno && block_id.is_valid())
             ? blkid == block_id
             : block::check_old_mc_block_id(prev_blocks_dict_.get(), blkid);
}

// returns block with min block.seqno and req_lt <= block.end_lt
bool ConfigInfo::get_mc_block_by_lt(ton::LogicalTime req_lt, ton::BlockIdExt& blkid, ton::LogicalTime* end_lt) const {
  if (req_lt > lt) {
    return false;
  }
  td::BitArray<32> key;
  auto found = prev_blocks_dict_->traverse_extra(
      key.bits(), 32,
      [req_lt](td::ConstBitPtr key_prefix, int key_pfx_len, Ref<vm::CellSlice> extra, Ref<vm::CellSlice> value) {
        unsigned long long found_lt;
        if (!(extra.write().advance(1) && extra.write().fetch_ulong_bool(64, found_lt))) {
          return -1;
        }
        if (found_lt < req_lt) {
          return 0;  // all leaves in subtree have end_lt <= found_lt < req_lt, skip
        }
        return 6;  // visit left subtree, then right subtree; for leaf: accept and return to the top
      });
  if (found.first.not_null()) {
    CHECK(unpack_old_mc_block_id(std::move(found.first), (unsigned)key.to_ulong(), blkid, end_lt));
    return true;
  }
  if (block_id.is_valid()) {
    blkid = block_id;
    if (end_lt) {
      *end_lt = lt;
    }
    return true;
  } else {
    return false;
  }
}

// returns key block with max block.seqno and block.seqno <= req_seqno
bool ConfigInfo::get_prev_key_block(ton::BlockSeqno req_seqno, ton::BlockIdExt& blkid, ton::LogicalTime* end_lt) const {
  if (block_id.is_valid() && is_key_state_ && block_id.seqno() <= req_seqno) {
    blkid = block_id;
    if (end_lt) {
      *end_lt = lt;
    }
    return true;
  }
  td::BitArray<32> key;
  auto found =
      prev_blocks_dict_->traverse_extra(key.bits(), 32,
                                        [req_seqno](td::ConstBitPtr key_prefix, int key_pfx_len,
                                                    Ref<vm::CellSlice> extra, Ref<vm::CellSlice> value) -> int {
                                          if (extra->prefetch_ulong(1) != 1) {
                                            return 0;  // no key blocks in subtree, skip
                                          }
                                          unsigned x = (unsigned)key_prefix.get_uint(key_pfx_len);
                                          unsigned d = 32 - key_pfx_len;
                                          if (!d) {
                                            return x <= req_seqno;
                                          }
                                          unsigned y = req_seqno >> (d - 1);
                                          if (y < 2 * x) {
                                            // (x << d) > req_seqno <=> x > (req_seqno >> d) = (y >> 1) <=> 2 * x > y
                                            return 0;  // all nodes in subtree have block.seqno > req_seqno => skip
                                          }
                                          return y == 2 * x ? 1 /* visit only left */ : 5 /* visit right, then left */;
                                        });
  if (found.first.not_null()) {
    CHECK(unpack_old_mc_block_id(std::move(found.first), (unsigned)key.to_ulong(), blkid, end_lt));
    CHECK(blkid.seqno() <= req_seqno);
    return true;
  } else {
    blkid.invalidate();
    return false;
  }
}

// returns key block with min block.seqno and block.seqno >= req_seqno
bool ConfigInfo::get_next_key_block(ton::BlockSeqno req_seqno, ton::BlockIdExt& blkid, ton::LogicalTime* end_lt) const {
  td::BitArray<32> key;
  auto found = prev_blocks_dict_->traverse_extra(
      key.bits(), 32,
      [req_seqno](td::ConstBitPtr key_prefix, int key_pfx_len, Ref<vm::CellSlice> extra,
                  Ref<vm::CellSlice> value) -> int {
        if (extra->prefetch_ulong(1) != 1) {
          return 0;  // no key blocks in subtree, skip
        }
        unsigned x = (unsigned)key_prefix.get_uint(key_pfx_len);
        unsigned d = 32 - key_pfx_len;
        if (!d) {
          return x >= req_seqno;
        }
        unsigned y = req_seqno >> (d - 1);
        if (y > 2 * x + 1) {
          // ((x + 1) << d) <= req_seqno <=> (x+1) <= (req_seqno >> d) = (y >> 1) <=> 2*x+2 <= y <=> y > 2*x+1
          return 0;  // all nodes in subtree have block.seqno < req_seqno => skip
        }
        return y == 2 * x + 1 ? 2 /* visit only right */ : 6 /* visit left, then right */;
      });
  if (found.first.not_null()) {
    CHECK(unpack_old_mc_block_id(std::move(found.first), (unsigned)key.to_ulong(), blkid, end_lt));
    CHECK(blkid.seqno() >= req_seqno);
    return true;
  }
  if (block_id.is_valid() && is_key_state_ && block_id.seqno() >= req_seqno) {
    blkid = block_id;
    if (end_lt) {
      *end_lt = lt;
    }
    return true;
  } else {
    blkid.invalidate();
    return false;
  }
}

Ref<vm::Cell> ConfigInfo::lookup_library(td::ConstBitPtr root_hash) const {
  if (!libraries_dict_) {
    return {};
  }
  auto csr = libraries_dict_->lookup(root_hash, 256);
  if (csr.is_null() || csr->prefetch_ulong(2) != 0 || !csr->have_refs()) {  // shared_lib_descr$00 lib:^Cell
    return {};
  }
  auto lib = csr->prefetch_ref();
  if (lib->get_hash().bits().compare(root_hash, 256)) {
    LOG(ERROR) << "public library hash mismatch: expected " << root_hash.to_hex(256) << " , found "
               << lib->get_hash().bits().to_hex(256);
    return {};
  }
  return lib;
}

td::Result<Ref<vm::Tuple>> ConfigInfo::get_prev_blocks_info() const {
  // [ wc:Integer shard:Integer seqno:Integer root_hash:Integer file_hash:Integer] = BlockId;
  // [ last_mc_blocks:[BlockId...]
  //   prev_key_block:BlockId ] : PrevBlocksInfo
  auto block_id_to_tuple = [](const ton::BlockIdExt& block_id) -> vm::Ref<vm::Tuple> {
    td::RefInt256 shard = td::make_refint(block_id.id.shard);
    if (shard->sgn() < 0) {
      shard &= ((td::make_refint(1) << 64) - 1);
    }
    return vm::make_tuple_ref(td::make_refint(block_id.id.workchain), std::move(shard),
                              td::make_refint(block_id.id.seqno), td::bits_to_refint(block_id.root_hash.bits(), 256),
                              td::bits_to_refint(block_id.file_hash.bits(), 256));
  };
  std::vector<vm::StackEntry> last_mc_blocks;

  last_mc_blocks.push_back(block_id_to_tuple(block_id));
  for (ton::BlockSeqno seqno = block_id.id.seqno; seqno > 0 && last_mc_blocks.size() < 16;) {
    --seqno;
    ton::BlockIdExt block_id;
    if (!get_old_mc_block_id(seqno, block_id)) {
      return td::Status::Error("cannot fetch old mc block");
    }
    last_mc_blocks.push_back(block_id_to_tuple(block_id));
  }

  ton::BlockIdExt last_key_block;
  ton::LogicalTime last_key_block_lt;
  if (!get_last_key_block(last_key_block, last_key_block_lt)) {
    return td::Status::Error("cannot fetch last key block");
  }
  return vm::make_tuple_ref(td::make_cnt_ref<std::vector<vm::StackEntry>>(std::move(last_mc_blocks)),
                            block_id_to_tuple(last_key_block));
}

td::optional<PrecompiledContractsConfig::Contract> PrecompiledContractsConfig::get_contract(
    td::Bits256 code_hash) const {
  auto list_copy = list;
  auto cs = list_copy.lookup(code_hash);
  if (cs.is_null()) {
    return {};
  }
  gen::PrecompiledSmc::Record rec;
  if (!tlb::csr_unpack(cs, rec)) {
    return {};
  }
  Contract c;
  c.gas_usage = rec.gas_usage;
  return c;
}

}  // namespace block
