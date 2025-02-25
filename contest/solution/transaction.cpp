#include "transaction.hpp"

#include <openssl/sha.h>

#include "crypto/block/block-auto.h"
#include "crypto/block/block-parse.h"
#include "crypto/vm/vm.h"
#include "tdutils/td/utils/ThreadSafeCounter.h"
#include "profile.hpp"

static int my_try_action_set_code(vm::CellSlice& cs, block::ActionPhase& ap, const block::ActionPhaseConfig&) {
	block::gen::OutAction::Record_action_set_code rec;
	if(!tlb::unpack_exact(cs, rec)) return -1;
	ap.new_code = std::move(rec.new_code);
	ap.code_changed = true;
	ap.spec_actions++;
	return 0;
}

bool MyTransaction::check_replace_src_addr(td::Ref<vm::CellSlice>& src_addr) const {
  int t = (int)src_addr->prefetch_ulong(2);
  if (!t && src_addr->size_ext() == 2) {
	// addr_none$00  --> replace with the address of current smart contract
	src_addr = my_addr;
	return true;
  }
  if (t != 2) {
	// invalid address (addr_extern and addr_var cannot be source addresses)
	return false;
  }
  if (src_addr->contents_equal(*my_addr) || src_addr->contents_equal(*my_addr_exact)) {
	// source address matches that of the current account
	return true;
  }
  // only one valid case remaining: rewritten source address used, replace with the complete one
  // (are we sure we want to allow this?)
  return false;
}

bool MyTransaction::check_rewrite_dest_addr(td::Ref<vm::CellSlice>& dest_addr, const block::ActionPhaseConfig& cfg, bool* is_mc) const {
	if(!dest_addr->prefetch_ulong(1)) {
		// all external addresses allowed
		if(is_mc) *is_mc = false;
		return true;
	}
	bool repack = false;
	int tag = block::gen::t_MsgAddressInt.get_tag(*dest_addr);
	block::gen::MsgAddressInt::Record_addr_var rec;
  if(tag == block::gen::MsgAddressInt::addr_var) {
	if(!tlb::csr_unpack(dest_addr, rec)) return false; // cannot unpack addr_var
	if(rec.addr_len == 256 && rec.workchain_id >= -128 && rec.workchain_id < 128) repack = true;
  } else if (tag == block::gen::MsgAddressInt::addr_std) {
	block::gen::MsgAddressInt::Record_addr_std recs;
	if (!tlb::csr_unpack(dest_addr, recs)) {
	  // cannot unpack addr_std
	  LOG(DEBUG) << "cannot unpack addr_std in a destination address";
	  return false;
	}
	rec.anycast = std::move(recs.anycast);
	rec.addr_len = 256;
	rec.workchain_id = recs.workchain_id;
	rec.address = td::make_bitstring_ref(recs.address);
  } else {
	// unknown address format (not a MsgAddressInt)
	LOG(DEBUG) << "destination address does not have a MsgAddressInt tag";
	return false;
  }
  if (rec.workchain_id != ton::masterchainId) {
	// recover destination workchain info from configuration
	auto it = cfg.workchains->find(rec.workchain_id);
	if (it == cfg.workchains->end()) {
	  // undefined destination workchain
	  LOG(DEBUG) << "destination address contains unknown workchain_id " << rec.workchain_id;
	  return false;
	}
	if (!it->second->accept_msgs) {
	  // workchain does not accept new messages
	  LOG(DEBUG) << "destination address belongs to workchain " << rec.workchain_id << " not accepting new messages";
	  return false;
	}
	if (!it->second->is_valid_addr_len(rec.addr_len)) {
	  // invalid address length for specified workchain
	  LOG(DEBUG) << "destination address has length " << rec.addr_len << " invalid for destination workchain "
				 << rec.workchain_id;
	  return false;
	}
  }
  if (rec.anycast->size() > 1) {
	// destination address is an anycast
	vm::CellSlice cs{*rec.anycast};
	int d = (int)cs.fetch_ulong(6) - 32;
	if (d <= 0 || d > 30) {
	  // invalid anycast prefix length
	  return false;
	}
	unsigned pfx = (unsigned)cs.fetch_ulong(d);
	unsigned my_pfx = (unsigned)account.addr.cbits().get_uint(d);
	if(pfx != my_pfx) {
		// rewrite destination address
		vm::CellBuilder cb;
		cb.store_long_bool(32 + d, 6);
		cb.store_long_bool(my_pfx, d);
		rec.anycast = load_cell_slice_ref(cb.finalize());
		repack = true;
	}
  }
  if(is_mc) *is_mc = (rec.workchain_id == ton::masterchainId);
  if(!repack) return true;
  if(rec.addr_len == 256 && rec.workchain_id >= -128 && rec.workchain_id < 128) {
	// repack as an addr_std
	vm::CellBuilder cb;
	cb.store_long_bool(2, 2);
	cb.append_cellslice_bool(std::move(rec.anycast));
	cb.store_long_bool(rec.workchain_id, 8);
	cb.append_bitstring(std::move(rec.address));
	dest_addr = load_cell_slice_ref(cb.finalize());
  } else {
	// repack as an addr_var
	tlb::csr_pack(dest_addr, std::move(rec));
  }
  return true;
}

int MyTransaction::try_action_reserve_currency(vm::CellSlice& cs, block::ActionPhase& ap, const block::ActionPhaseConfig& cfg) {
  block::gen::OutAction::Record_action_reserve_currency rec;
  if(!tlb::unpack_exact(cs, rec)) return -1;
  if((rec.mode & 16) && cfg.bounce_on_fail_enabled) {
	rec.mode &= ~16;
	ap.need_bounce_on_fail = true;
  }
  if(rec.mode & ~15) return -1;
  int mode = rec.mode;
  block::CurrencyCollection reserve, newc;
  if(!reserve.validate_unpack(std::move(rec.currency))) return -1;
  if(mode & 4) {
	if(mode & 8) reserve = original_balance - reserve;
	else reserve += original_balance;
  } else if (mode & 8) return -1;
  if(!reserve.is_valid() || td::sgn(reserve.grams) < 0) return -1;
  if(reserve.grams > ap.remaining_balance.grams) {
	if(mode & 2) reserve.grams = ap.remaining_balance.grams;
	else return 37;  // not enough grams
  }
  if(!block::sub_extra_currency(ap.remaining_balance.extra, reserve.extra, newc.extra)) return 38;  // not enough (extra) funds
  newc.grams = ap.remaining_balance.grams - reserve.grams;
  if (mode & 1) {
	// leave only res_grams, reserve everything else
	std::swap(newc, reserve);
  }
  // set remaining_balance to new_grams and new_extra
  ap.remaining_balance = std::move(newc);
  // increase reserved_balance by res_grams and res_extra
  ap.reserved_balance += std::move(reserve);
  ap.spec_actions++;
  return 0;
}

int MyTransaction::try_action_send_msg(const vm::CellSlice& cs0, block::ActionPhase& ap, const block::ActionPhaseConfig& cfg, int redoing) {
  block::gen::OutAction::Record_action_send_msg act_rec;
  vm::CellSlice cs{cs0};
  if(!tlb::unpack_exact(cs, act_rec)) return -1;
  if ((act_rec.mode & 16) && cfg.bounce_on_fail_enabled) {
	act_rec.mode &= ~16;
	ap.need_bounce_on_fail = true;
  }
  if ((act_rec.mode & ~0xe3) || (act_rec.mode & 0xc0) == 0xc0) {
	return -1;
  }
  bool skip_invalid = (act_rec.mode & 2);
  auto check_skip_invalid = [&](unsigned error_code) -> unsigned int {
	if(skip_invalid) {
	  if(cfg.message_skip_enabled) ap.skipped_actions++;
	  return 0;
	}
	return error_code;
  };
  td::RefInt256 fwd_fee, ihr_fee;
  block::gen::MessageRelaxed::Record msg;
  if(!tlb::type_unpack_cell(act_rec.out_msg, block::gen::t_MessageRelaxed_Any, msg)) return -1;
  if(!block::tlb::validate_message_relaxed_libs(act_rec.out_msg)) return -1;
  if(redoing >= 1) {
	if(msg.init->size_refs() >= 2) {
	  msg.init.write().skip_first(2);
	  vm::CellBuilder cb;
	  td::Ref<vm::Cell> cell;
	  CHECK(cb.append_cellslice_bool(std::move(msg.init))  // StateInit
			&& cb.finalize_to(cell)                        // -> ^StateInit
			&& cb.store_long_bool(3, 2)                    // (just (right ... ))
			&& cb.store_ref_bool(std::move(cell))          // z:^StateInit
			&& cb.finalize_to(cell));
	  msg.init = vm::load_cell_slice_ref(cell);
	} else {
	  redoing = 2;
	}
  }
  if (redoing >= 2 && msg.body->size_ext() > 1 && msg.body->prefetch_ulong(1) == 0) {
	// body:(Either X ^X)
	// transform (left x:X) into (right x:^X)
	msg.body.write().skip_first(1);
	vm::CellBuilder cb;
	td::Ref<vm::Cell> cell;
	CHECK(cb.append_cellslice_bool(std::move(msg.body))  // X
		  && cb.finalize_to(cell)                        // -> ^X
		  && cb.store_long_bool(1, 1)                    // (right ... )
		  && cb.store_ref_bool(std::move(cell))          // x:^X
		  && cb.finalize_to(cell));
	msg.body = vm::load_cell_slice_ref(cell);
  }

  block::gen::CommonMsgInfoRelaxed::Record_int_msg_info info;
  bool ext_msg = msg.info->prefetch_ulong(1);
  if (ext_msg) {
	// ext_out_msg_info$11 constructor of CommonMsgInfoRelaxed
	block::gen::CommonMsgInfoRelaxed::Record_ext_out_msg_info erec;
	if (!tlb::csr_unpack(msg.info, erec)) {
	  return -1;
	}
	if (act_rec.mode & ~3) {
	  return -1;  // invalid mode for an external message
	}
	info.src = std::move(erec.src);
	info.dest = std::move(erec.dest);
	// created_lt and created_at are ignored
	info.ihr_disabled = true;
	info.bounce = false;
	info.bounced = false;
	fwd_fee = ihr_fee = td::zero_refint();
  } else {
	// int_msg_info$0 constructor
	if (!tlb::csr_unpack(msg.info, info) || !block::tlb::t_CurrencyCollection.validate_csr(info.value)) {
	  return -1;
	}
	if (cfg.disable_custom_fess) {
	  fwd_fee = ihr_fee = td::zero_refint();
	} else {
	  fwd_fee = block::tlb::t_Grams.as_integer(info.fwd_fee);
	  ihr_fee = block::tlb::t_Grams.as_integer(info.ihr_fee);
	}
  }
  // set created_at and created_lt to correct values
  info.created_at = now;
  info.created_lt = ap.end_lt;
  // always clear bounced flag
  info.bounced = false;
  // have to check source address
  // it must be either our source address, or empty
  if(!check_replace_src_addr(info.src)) return 35;  // invalid source address
  bool to_mc = false;
  if(!check_rewrite_dest_addr(info.dest, cfg, &to_mc)) return check_skip_invalid(36);  // invalid destination address

  // fetch message pricing info
  const block::MsgPrices& msg_prices = cfg.fetch_msg_prices(to_mc || account.is_masterchain());
  // If action fails, account is required to pay fine_per_cell for every visited cell
  // Number of visited cells is limited depending on available funds
  unsigned max_cells = cfg.size_limits.max_msg_cells;
  td::uint64 fine_per_cell = 0;
  if(cfg.action_fine_enabled && !account.is_special) {
	fine_per_cell = (msg_prices.cell_price >> 16) / 4;
	td::RefInt256 funds = ap.remaining_balance.grams;
	if(!ext_msg && !(act_rec.mode & 0x80) && !(act_rec.mode & 1)) {
	  if(!block::tlb::t_CurrencyCollection.validate_csr(info.value)) return check_skip_invalid(37);
	  block::CurrencyCollection value;
	  value.unpack(info.value);
	  td::RefInt256 new_funds = value.grams;
	  if(act_rec.mode & 0x40) {
		if(msg_balance_remaining.is_valid()) new_funds += msg_balance_remaining.grams;
		if(compute_phase) new_funds -= compute_phase->gas_fees;
		new_funds -= ap.action_fine;
		if(new_funds->sgn() < 0) return check_skip_invalid(37);
	  }
	  funds = std::min(funds, new_funds);
	}
	if(funds->cmp(max_cells * fine_per_cell) < 0) max_cells = static_cast<unsigned>((funds / td::make_refint(fine_per_cell))->to_long());
  }
  // compute size of message
  vm::CellStorageStat sstat(max_cells);  // for message size
  // preliminary storage estimation of the resulting message
  unsigned max_merkle_depth = 0;
  auto add_used_storage = [&](const auto& x, unsigned skip_root_count) -> td::Status {
	if(x.not_null()) {
	  TRY_RESULT(res, sstat.add_used_storage(x, true, skip_root_count));
	  max_merkle_depth = std::max(max_merkle_depth, res.max_merkle_depth);
	}
	return td::Status::OK();
  };
  add_used_storage(msg.init, 3);  // message init
  add_used_storage(msg.body, 3);  // message body (the root cell itself is not counted)
  if (!ext_msg) {
	add_used_storage(info.value->prefetch_ref(), 0);
  }
  auto collect_fine = [&] {
	if (cfg.action_fine_enabled && !account.is_special) {
	  td::uint64 fine = fine_per_cell * std::min<td::uint64>(max_cells, sstat.cells);
	  if (ap.remaining_balance.grams->cmp(fine) < 0) {
		fine = ap.remaining_balance.grams->to_long();
	  }
	  ap.action_fine += fine;
	  ap.remaining_balance.grams -= fine;
	}
  };
  if (sstat.cells > max_cells && max_cells < cfg.size_limits.max_msg_cells) {
	collect_fine();
	return check_skip_invalid(40);
  }
  if (sstat.bits > cfg.size_limits.max_msg_bits || sstat.cells > max_cells) {
	collect_fine();
	return check_skip_invalid(40);
  }
  if (max_merkle_depth > max_allowed_merkle_depth) {
	collect_fine();
	return check_skip_invalid(40);
  }

  // compute forwarding fees
  auto fees_c = msg_prices.compute_fwd_ihr_fees(sstat.cells, sstat.bits, info.ihr_disabled);

  if (account.is_special) {
	fees_c.first = fees_c.second = 0;
  }

  // set fees to computed values
  if (fwd_fee->unsigned_fits_bits(63) && fwd_fee->to_long() < (long long)fees_c.first) {
	fwd_fee = td::make_refint(fees_c.first);
  }
  if (fees_c.second && ihr_fee->unsigned_fits_bits(63) && ihr_fee->to_long() < (long long)fees_c.second) {
	ihr_fee = td::make_refint(fees_c.second);
  }

  td::Ref<vm::Cell> new_msg;
  td::RefInt256 fees_collected, fees_total;
  unsigned new_msg_bits;

  if (!ext_msg) {
	// Process outbound internal message
	// check value, check/compute ihr_fees, fwd_fees
	// ...
	if (!block::tlb::t_CurrencyCollection.validate_csr(info.value)) {
	  collect_fine();
	  return check_skip_invalid(37);
	}
	if (info.ihr_disabled) ihr_fee = td::zero_refint(); // if IHR is disabled, IHR fees will be always zero
	// extract value to be carried by the message
	block::CurrencyCollection req;
	req.unpack(info.value);
	if (act_rec.mode & 0x80) {
	  // attach all remaining balance to this message
	  req = ap.remaining_balance;
	  act_rec.mode &= ~1;  // pay fees from attached value
	} else if (act_rec.mode & 0x40) {
	  // attach all remaining balance of the inbound message (in addition to the original value)
	  req += msg_balance_remaining;
	  if (!(act_rec.mode & 1)) {
		req -= ap.action_fine;
		if (compute_phase) {
		  req -= compute_phase->gas_fees;
		}
		if (!req.is_valid()) {
		  collect_fine();
		  return check_skip_invalid(37);
		}
	  }
	}

	// compute req_grams + fees
	td::RefInt256 req_grams_brutto = req.grams;
	fees_total = fwd_fee + ihr_fee;
	if (act_rec.mode & 1) {
	  // we are going to pay the fees
	  req_grams_brutto += fees_total;
	} else if (req.grams < fees_total) {
	  // receiver pays the fees (but cannot)
	  collect_fine();
	  return check_skip_invalid(37);  // not enough grams
	} else {
	  // decrease message value
	  req.grams -= fees_total;
	}

	// check that we have at least the required value
	if (ap.remaining_balance.grams < req_grams_brutto) {
	  collect_fine();
	  return check_skip_invalid(37);  // not enough grams
	}

	td::Ref<vm::Cell> new_extra;

	if (!block::sub_extra_currency(ap.remaining_balance.extra, req.extra, new_extra)) {
	  collect_fine();
	  return check_skip_invalid(38);  // not enough (extra) funds
	}
	auto fwd_fee_mine = msg_prices.get_first_part(fwd_fee);
	auto fwd_fee_remain = fwd_fee - fwd_fee_mine;

	// re-pack message value
	req.pack_to(info.value);
	block::tlb::t_Grams.pack_integer(info.fwd_fee, fwd_fee_remain);
	block::tlb::t_Grams.pack_integer(info.ihr_fee, ihr_fee);

	// serialize message
	tlb::csr_pack(msg.info, info);
	vm::CellBuilder cb;
	if(!tlb::type_pack(cb, block::gen::t_MessageRelaxed_Any, msg)) {
	  if(redoing == 2) {
		collect_fine();
		return check_skip_invalid(39);
	  }
	  return -2;
	}

	new_msg_bits = cb.size();
	new_msg = cb.finalize();

	// clear msg_balance_remaining if it has been used
	if (act_rec.mode & 0xc0) {
	  msg_balance_remaining.set_zero();
	}

	// update balance
	ap.remaining_balance -= req_grams_brutto;
	ap.remaining_balance.extra = std::move(new_extra);
	fees_total = fwd_fee + ihr_fee;
	fees_collected = fwd_fee_mine;
  } else {
	// external messages also have forwarding fees
	if (ap.remaining_balance.grams < fwd_fee) {
	  collect_fine();
	  return check_skip_invalid(37);  // not enough grams
	}
	// repack message
	// ext_out_msg_info$11 constructor of CommonMsgInfo
	block::gen::CommonMsgInfo::Record_ext_out_msg_info erec;
	erec.src = info.src;
	erec.dest = info.dest;
	erec.created_at = info.created_at;
	erec.created_lt = info.created_lt;
	tlb::csr_pack(msg.info, erec);
	vm::CellBuilder cb;
	if(!tlb::type_pack(cb, block::gen::t_MessageRelaxed_Any, msg)) {
	  if(redoing == 2) {
		collect_fine();
		return check_skip_invalid(39);
	  }
	  return -2;
	}

	new_msg_bits = cb.size();
	new_msg = cb.finalize();

	// update balance
	ap.remaining_balance -= fwd_fee;
	fees_collected = fees_total = fwd_fee;
  }

  if(!block::tlb::t_Message.validate_ref(new_msg)) {
	collect_fine();
	return -1;
  }
  if(!block::gen::t_Message_Any.validate_ref(new_msg)) {
	block::gen::t_Message_Any.print_ref(std::cerr, new_msg);
	vm::load_cell_slice(new_msg).print_rec(std::cerr);
	collect_fine();
	return -1;
  }
  ap.msgs_created++;
  ap.end_lt++;
  ap.out_msgs.push_back(std::move(new_msg));
  ap.total_action_fees += fees_collected;
  ap.total_fwd_fees += fees_total;

  if ((act_rec.mode & 0xa0) == 0xa0) {
	// ap.remaining_balance.is_zero()
	ap.acc_delete_req = ap.reserved_balance.is_zero();
  }

  ap.tot_msg_bits += sstat.bits + new_msg_bits;
  ap.tot_msg_cells += sstat.cells + 1;

  return 0;
}

int MyTransaction::try_action_change_library(vm::CellSlice& cs, block::ActionPhase& ap, const block::ActionPhaseConfig& cfg) {
  block::gen::OutAction::Record_action_change_library rec;
  if(!tlb::unpack_exact(cs, rec)) return -1;
  // mode: +0 = remove library, +1 = add private library, +2 = add public library, +16 - bounce on fail
  if(rec.mode & 16) {
	if(!cfg.bounce_on_fail_enabled) return -1;
	ap.need_bounce_on_fail = true;
	rec.mode &= ~16;
  }
  if(rec.mode > 2) return -1;
  td::Ref<vm::Cell> lib_ref = rec.libref->prefetch_ref();
  ton::Bits256 hash;
  if(lib_ref.not_null()) hash = lib_ref->get_hash().bits();
  else {
	rec.libref.write().skip_first(1);
	rec.libref.write().fetch_bits_to(hash);
  }
  try {
	vm::Dictionary dict{new_library, 256};
	if (!rec.mode) {
	  // remove library
	  dict.lookup_delete(hash);
	} else {
	  auto val = dict.lookup(hash);
	  if (val.not_null()) {
		bool is_public = val->prefetch_ulong(1);
		auto ref = val->prefetch_ref();
		if (hash == ref->get_hash().bits()) {
		  lib_ref = ref;
		  if (is_public == (rec.mode >> 1)) {
			// library already in required state
			ap.spec_actions++;
			return 0;
		  }
		}
	  }
	  if (lib_ref.is_null()) return 41; // library code not found
	  vm::CellStorageStat sstat;
	  auto cell_info = sstat.compute_used_storage(lib_ref).move_as_ok();
	  if(sstat.cells > cfg.size_limits.max_library_cells || cell_info.max_merkle_depth > max_allowed_merkle_depth) return 43;
	  vm::CellBuilder cb;
	  cb.store_bool_bool(rec.mode >> 1);
	  cb.store_ref_bool(std::move(lib_ref));
	  dict.set_builder(hash, cb);
	}
	new_library = std::move(dict).extract_root_cell();
  } catch (vm::VmError& vme) {
	return 42;
  }
  ap.spec_actions++;
  return 0;
}

static td::uint32 get_public_libraries_count(const td::Ref<vm::Cell>& libraries) {
	td::uint32 count = 0;
	vm::Dictionary dict{libraries, 256};
	dict.check_for_each([&](td::Ref<vm::CellSlice> value, td::ConstBitPtr key, int) {
		if(block::is_public_library(key, std::move(value))) ++count;
		return true;
	});
	return count;
}

struct CellStorageStat {
	using CellInfo = vm::CellStorageStat::CellInfo;
	unsigned long long bits = 0;
	vm::HashSet seen;

	void clear() {
		bits = 0;
		seen.clear();
	}

	bool add_used_storage(td::Ref<vm::Cell> cell) {
		std::vector<td::Ref<vm::Cell>> cells {std::move(cell)};
		while(!cells.empty()) {
			cell = std::move(cells.back());
			cells.pop_back();
			if(!seen.emplace(cell->get_hash())) continue;

			const vm::DataCell *dc;
			vm::Cell::LoadedCell lc;
			if(cell->is_datacell()) dc = (const vm::DataCell*) cell.get();
			else {
				auto rlc = cell->load_cell();
				lc = rlc.is_ok() ? rlc.move_as_ok() : vm::Cell::LoadedCell{};
				dc = lc.data_cell.get();
			}

			uint32_t nrefs = dc->get_refs_cnt();
			bits += dc->get_bits();
			if(!nrefs) continue;

			if(lc.virt.get_level() != vm::Cell::VirtualizationParameters::max_level()) {
				const vm::Cell::SpecialType type = dc->special_type();
				if(type == vm::CellTraits::SpecialType::MerkleProof || type == vm::CellTraits::SpecialType::MerkleUpdate)
					lc.virt = vm::Cell::VirtualizationParameters(lc.virt.get_level()+1, lc.virt.get_virtualization());
			}
			vm::Cell* const* refs = dc->get_refs();
			do {
				--nrefs;
				td::Ref<vm::Cell> cr(refs[nrefs]->virtualize(lc.virt));
				if(cr.is_null()) return false;
				cells.emplace_back(std::move(cr));
			} while(nrefs);
		}
		return true;
	}
};

td::Status MyTransaction::check_state_limits(const block::SizeLimitsConfig& size_limits, bool update_storage_stat) {
	auto cell_equal = [](const td::Ref<vm::Cell>& a, const td::Ref<vm::Cell>& b) -> bool {
		if(a.is_null()) return b.is_null();
		if(b.is_null()) return false;
		return a->get_hash() == b->get_hash();
  	};
	if(cell_equal(account.code, new_code) && cell_equal(account.data, new_data) && cell_equal(account.library, new_library))
		return td::Status::OK();
	CellStorageStat storage_stat;
	auto add_used_storage = [&](const td::Ref<vm::Cell>& cell)->td::Status {
		if(cell.not_null()) {
			if(!storage_stat.add_used_storage(cell)) return td::Status::Error("cell is null");
			if(storage_stat.bits > size_limits.max_acc_state_bits) return td::Status::Error("too many bits");
			if(storage_stat.seen.size() > size_limits.max_acc_state_cells) return td::Status::Error("too many cells");
		}
		return td::Status::OK();
	};
	TRY_STATUS(add_used_storage(new_code));
	TRY_STATUS(add_used_storage(new_data));
	TRY_STATUS(add_used_storage(new_library));
	if(acc_status != block::Account::acc_active) storage_stat.clear();
	td::Status res;
	if(account.is_masterchain() && !cell_equal(account.library, new_library) && get_public_libraries_count(new_library) > size_limits.max_acc_public_libraries)
		res = td::Status::Error("too many public libraries");
	else res = td::Status::OK();
	if(update_storage_stat) {
		new_storage_stat.cells = storage_stat.seen.size();
		new_storage_stat.bits = storage_stat.bits;
		new_storage_stat.public_cells = 0;
		new_storage_stat.seen = std::move(storage_stat.seen);
		new_storage_stat.clear_limit();
	}
  return res;
}

void MyTransaction::prepare_action_phase(const block::ActionPhaseConfig& cfg) {
	if(!compute_phase || !compute_phase->success) return;
	action_phase = std::make_unique<block::ActionPhase>();
	block::ActionPhase& ap = *(action_phase.get());
	ap.result_code = -1;
	ap.result_arg = 0;
	ap.tot_actions = ap.spec_actions = ap.skipped_actions = ap.msgs_created = 0;
	td::Ref<vm::Cell> list = compute_phase->actions;
	ap.action_list_hash = list->get_hash().bits();
	ap.remaining_balance = balance;
	ap.end_lt = end_lt;
	ap.total_fwd_fees = td::zero_refint();
	ap.total_action_fees = td::zero_refint();
	ap.reserved_balance.set_zero();
	ap.action_fine = td::zero_refint();
	td::Ref<vm::Cell> old_code = new_code, old_data = new_data, old_library = new_library;
	auto enforce_state_limits = [&]() {
		if(account.is_special) return true;
		td::Status S = check_state_limits(cfg.size_limits);
		if (S.is_error()) {
			// Rollback changes to state, fail action phase
			new_storage_stat.clear();
			new_code = old_code;
			new_data = old_data;
			new_library = old_library;
			ap.result_code = 50;
			ap.state_exceeds_limits = true;
			return false;
		}
		return true;
	};

  int n = 0;
  while (true) {
	ap.action_list.push_back(list);
	bool special = true;
	auto cs = load_cell_slice_special(std::move(list), special);
	if (special) {
	  ap.result_code = 32;  // action list invalid
	  ap.result_arg = n;
	  ap.action_list_invalid = true;
	  return;
	}
	if(!cs.size_ext()) break;
	if(!cs.have_refs()) {
	  ap.result_code = 32;  // action list invalid
	  ap.result_arg = n;
	  ap.action_list_invalid = true;
	  return;
	}
	list = cs.prefetch_ref();
	n++;
	if(n > cfg.max_actions) {
	  ap.result_code = 33;  // too many actions
	  ap.result_arg = n;
	  ap.action_list_invalid = true;
	  return;
	}
  }

  ap.tot_actions = n;
  ap.spec_actions = ap.skipped_actions = 0;
  for (int i = n - 1; i >= 0; --i) {
	ap.result_arg = n - 1 - i;
	if (!block::gen::t_OutListNode.validate_ref(ap.action_list[i])) {
	  if (cfg.message_skip_enabled) {
		// try to read mode from action_send_msg even if out_msg scheme is violated
		// action should at least contain 40 bits: 32bit tag and 8 bit mode
		// if (mode & 2), that is ignore error mode, skip action even for invalid message
		// if there is no (mode & 2) but (mode & 16) presents - enable bounce if possible
		bool special = true;
		auto cs = load_cell_slice_special(ap.action_list[i], special);
		if (!special) {
		  if ((cs.size() >= 40) && ((int)cs.fetch_ulong(32) == 0x0ec3c86d)) {
			int mode = (int)cs.fetch_ulong(8);
			if (mode & 2) {
			  ap.skipped_actions++;
			  ap.action_list[i] = {};
			  continue;
			} else if ((mode & 16) && cfg.bounce_on_fail_enabled) {
			  ap.bounce = true;
			}
		  }
		}
	  }
	  ap.result_code = 34;  // action #i invalid or unsupported
	  ap.action_list_invalid = true;
	  return;
	}
  }
  ap.valid = true;
  for (int i = n - 1; i >= 0; --i) {
	if(ap.action_list[i].is_null()) {
	  continue;
	}
	ap.result_arg = n - 1 - i;
	vm::CellSlice cs = load_cell_slice(ap.action_list[i]);
	cs.fetch_ref();
	int tag = block::gen::t_OutAction.get_tag(cs);
	int err_code = 34;
	ap.need_bounce_on_fail = false;
	switch (tag) {
	  case block::gen::OutAction::action_set_code:
		err_code = my_try_action_set_code(cs, ap, cfg);
		break;
	  case block::gen::OutAction::action_send_msg:
		err_code = try_action_send_msg(cs, ap, cfg);
		if (err_code == -2) {
		  err_code = try_action_send_msg(cs, ap, cfg, 1);
		  if (err_code == -2) {
			err_code = try_action_send_msg(cs, ap, cfg, 2);
		  }
		}
		break;
	  case block::gen::OutAction::action_reserve_currency:
		err_code = try_action_reserve_currency(cs, ap, cfg);
		break;
	  case block::gen::OutAction::action_change_library:
		err_code = try_action_change_library(cs, ap, cfg);
		break;
	}
	if(err_code) {
	  ap.result_code = (err_code == -1 ? 34 : err_code);
	  ap.end_lt = end_lt;
	  if (err_code == -1 || err_code == 34) {
		ap.action_list_invalid = true;
	  }
	  if (err_code == 37 || err_code == 38) {
		ap.no_funds = true;
	  }
	  // This is required here because changes to libraries are applied even if actipn phase fails
	  enforce_state_limits();
	  if(cfg.action_fine_enabled) {
		ap.action_fine = std::min(ap.action_fine, balance.grams);
		ap.total_action_fees = ap.action_fine;
		balance.grams -= ap.action_fine;
		total_fees += ap.action_fine;
	  }
	  if(ap.need_bounce_on_fail) ap.bounce = true;
	  return;
	}
  }

  if (cfg.action_fine_enabled) {
	ap.total_action_fees += ap.action_fine;
  }
  end_lt = ap.end_lt;
  if (ap.new_code.not_null()) {
	new_code = ap.new_code;
  }
  new_data = compute_phase->new_data;  // tentative persistent data update applied
  if(!enforce_state_limits()) return;

  ap.result_arg = 0;
  ap.result_code = 0;
  // ap.remaining_balance.grams->sgn() >= 0
  // ap.reserved_balance.grams->sgn() >= 0
  ap.remaining_balance += ap.reserved_balance;
  // ap.remaining_balance.is_valid()
  if(ap.acc_delete_req) {
	// ap.remaining_balance.is_zero()
	ap.acc_status_change = block::ActionPhase::acst_deleted;
	acc_status = block::Account::acc_deleted;
	was_deleted = true;
  }
  ap.success = true;
  out_msgs = std::move(ap.out_msgs);
  total_fees += ap.total_action_fees;  // NB: forwarding fees are not accounted here (they are not collected by the validators in this transaction)
  balance = ap.remaining_balance;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

bool MyTransaction::unpack_msg_state(const block::ComputePhaseConfig& cfg, bool lib_only, bool forbid_public_libs) {
	block::gen::StateInit::Record state;
	if(in_msg_state.is_null() || !tlb::unpack_cell(in_msg_state, state))
		return false;
	if(lib_only) {
		in_msg_library = state.library->prefetch_ref();
		return true;
	}
	new_split_depth = state.split_depth->size() == 6 ? (char)(state.split_depth->prefetch_ulong(6) - 32) : 0;
	if(state.special->size() > 1) {
		int z = (int) state.special->prefetch_ulong(3);
		if(z < 0) return false;
		new_tick = z & 2;
		new_tock = z & 1;
	}
	td::Ref<vm::Cell> old_code = new_code, old_data = new_data, old_library = new_library;
	new_code = state.code->prefetch_ref();
	new_data = state.data->prefetch_ref();
	new_library = state.library->prefetch_ref();
	auto size_limits = cfg.size_limits;
	if(forbid_public_libs) size_limits.max_acc_public_libraries = 0;
	td::Status S = check_state_limits(size_limits, false);
	if(S.is_error()) {
		new_code = old_code;
		new_data = old_data;
		new_library = old_library;
		return false;
	}
	return true;
}

td::Ref<vm::Tuple> MyTransaction::prepare_vm_c7(const block::ComputePhaseConfig& cfg) const {
	td::BitArray<256> rand_seed;
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	SHA256_CTX sha_ctx;
	SHA256_Init(&sha_ctx);
	SHA256_Update(&sha_ctx, cfg.block_rand_seed.data(), 32);
	SHA256_Update(&sha_ctx, cfg.global_version >= 8 ? account.addr.data() : account.addr_rewrite.data(), 32);
	SHA256_Final(rand_seed.data(), &sha_ctx);
	#pragma GCC diagnostic pop
	td::RefInt256 rand_seed_int{true};
	if(!rand_seed_int.unique_write().import_bits(rand_seed.cbits(), 256, false))
		throw std::runtime_error("cannot generate valid SmartContractInfo");
  std::vector<vm::StackEntry> tuple = {
      td::make_refint(0x076ef1ea),                // [ magic:0x076ef1ea
      td::zero_refint(),                          //   actions:Integer
      td::zero_refint(),                          //   msgs_sent:Integer
      td::make_refint(now),                       //   unixtime:Integer
      td::make_refint(account.block_lt),          //   block_lt:Integer
      td::make_refint(start_lt),                  //   trans_lt:Integer
      std::move(rand_seed_int),                   //   rand_seed:Integer
      balance.as_vm_tuple(),                      //   balance_remaining:[Integer (Maybe Cell)]
      my_addr,                                    //   myself:MsgAddressInt
      vm::StackEntry::maybe(cfg.global_config)    //   global_config:(Maybe Cell) ] = SmartContractInfo;
  };
  if(cfg.global_version >= 4) {
    tuple.push_back(vm::StackEntry::maybe(new_code));  // code:Cell
    if(msg_balance_remaining.is_valid()) tuple.push_back(msg_balance_remaining.as_vm_tuple());  // in_msg_value:[Integer (Maybe Cell)]
    else tuple.push_back(block::CurrencyCollection::zero().as_vm_tuple());
    tuple.push_back(storage_phase->fees_collected);       // storage_fees:Integer
    tuple.push_back(vm::StackEntry::maybe(cfg.prev_blocks_info));
	if(cfg.global_version >= 6) {
		tuple.push_back(vm::StackEntry::maybe(cfg.unpacked_config_tuple));          // unpacked_config_tuple:[...]
		tuple.push_back(due_payment.not_null() ? due_payment : td::zero_refint());  // due_payment:Integer
		tuple.push_back(compute_phase->precompiled_gas_usage
							? vm::StackEntry(td::make_refint(compute_phase->precompiled_gas_usage.value()))
							: vm::StackEntry());  // precompiled_gas_usage:Integer
	}
  }
  auto tuple_ref = td::make_cnt_ref<std::vector<vm::StackEntry>>(std::move(tuple));
  return vm::make_tuple_ref(std::move(tuple_ref));
}

int output_actions_count(td::Ref<vm::Cell> list) {
	int i = -1;
	do {
		++i;
		bool special;
		auto cs = vm::load_cell_slice_special(std::move(list), special);
		if(special) break;
		list = cs.prefetch_ref();
	} while(list.not_null());
	return i;
}

bool check_split_depth(const block::Account &account, int split_depth) {
	return account.split_depth_set_ ? (split_depth == account.split_depth_) : (split_depth >= 0 && split_depth <= 30);
}

class StringLoggerTail : public td::LogInterface {
public:
	explicit StringLoggerTail(size_t max_size = 256) : buf(max_size, '\0') {}
	void append(td::CSlice slice) override {
    	if(slice.size() > buf.size()) slice.remove_prefix(slice.size() - buf.size());
    	while(!slice.empty()) {
    		size_t s = std::min(buf.size() - pos, slice.size());
    		std::copy(slice.begin(), slice.begin() + s, buf.begin() + pos);
    		pos += s;
    		if(pos == buf.size()) {
    		  pos = 0;
    		  truncated = true;
    		}
    		slice.remove_prefix(s);
    	}
	}
	std::string get_log() const {
		if(truncated) {
      		std::string res = buf;
      		std::rotate(res.begin(), res.begin() + pos, res.end());
      		return res;
    	}
		return buf.substr(0, pos);
	}

private:
	std::string buf;
	size_t pos = 0;
	bool truncated = false;
};

bool MyTransaction::prepare_compute_phase(const block::ComputePhaseConfig& cfg) {
  compute_phase = std::make_unique<block::ComputePhase>();
  block::ComputePhase& cp = *(compute_phase.get());
  if(cfg.global_version >= 9) {
	original_balance = balance;
	if(msg_balance_remaining.is_valid()) original_balance -= msg_balance_remaining;
  } else original_balance -= total_fees;
  if(td::sgn(balance.grams) <= 0) {
	cp.skip_reason = block::ComputePhase::sk_no_gas;
	return true;
  }
  if(!compute_gas_limits(cp, cfg)) {
	compute_phase.reset();
	return false;
  }
  if(!cp.gas_limit && !cp.gas_credit) {
	cp.skip_reason = block::ComputePhase::sk_no_gas;
	return true;
  }
  if(in_msg_state.not_null() &&
	  (acc_status == block::Account::acc_uninit ||
	   (acc_status == block::Account::acc_frozen && account.state_hash == in_msg_state->get_hash().bits()))) {
	if(acc_status == block::Account::acc_uninit && cfg.is_address_suspended(account.workchain, account.addr)) {
	  cp.skip_reason = block::ComputePhase::sk_suspended;
	  return true;
	}
	use_msg_state = true;
	const bool forbid_public_libs = acc_status == block::Account::acc_uninit && account.is_masterchain();  // Forbid for deploying, allow for unfreezing
	if(!(unpack_msg_state(cfg, false, forbid_public_libs) && check_split_depth(account, new_split_depth))) {
	  cp.skip_reason = block::ComputePhase::sk_bad_state;
	  return true;
	}
	if(acc_status == block::Account::acc_uninit && !check_in_msg_state_hash()) {
	  cp.skip_reason = block::ComputePhase::sk_bad_state;
	  return true;
	}
  } else if(acc_status != block::Account::acc_active) {
	cp.skip_reason = in_msg_state.not_null() ? block::ComputePhase::sk_bad_state : block::ComputePhase::sk_no_state;
	return true;
  } else if (in_msg_state.not_null()) {
	if(cfg.allow_external_unfreeze && in_msg_extern && account.addr != in_msg_state->get_hash().bits()) {
		cp.skip_reason = block::ComputePhase::sk_bad_state;
		return true;
	}
	unpack_msg_state(cfg, true);  // use only libraries
  }
  if(!cfg.allow_external_unfreeze && in_msg_extern && in_msg_state.not_null() && account.addr != in_msg_state->get_hash().bits()) {
	  cp.skip_reason = block::ComputePhase::sk_bad_state;
	  return true;
  }

  td::optional<block::PrecompiledContractsConfig::Contract> precompiled;
  if(new_code.not_null() && trans_type == tr_ord)
	precompiled = cfg.precompiled_contracts.get_contract(new_code->get_hash().bits());

  vm::GasLimits gas{(long long)cp.gas_limit, (long long)cp.gas_max, (long long)cp.gas_credit};
  if(precompiled) {
	td::uint64 gas_usage = precompiled.value().gas_usage;
	cp.precompiled_gas_usage = gas_usage;
	if(gas_usage > cp.gas_limit) {
		cp.skip_reason = block::ComputePhase::sk_no_gas;
		return true;
	}
	auto impl = block::precompiled::get_implementation(new_code->get_hash().bits());
	if(impl != nullptr && !cfg.dont_run_precompiled_ && impl->required_version() <= cfg.global_version)
		return run_precompiled_contract(cfg, *impl);
	long long limit = account.is_special ? cfg.special_gas_limit : cfg.gas_limit;
	gas = vm::GasLimits{limit, limit, gas.gas_credit ? limit : 0};
  }

  td::Ref<vm::Stack> stack = prepare_vm_stack(cp);
  if(stack.is_null()) {
	compute_phase.reset();
	return false;
  }
  std::unique_ptr<StringLoggerTail> logger;
  auto vm_log = vm::VmLog();
  if(cfg.with_vm_log) {
	const size_t log_max_size = cfg.vm_log_verbosity > 4 ? (32 << 20) : (cfg.vm_log_verbosity > 0 ? (1 << 20) : 256);
	logger = std::make_unique<StringLoggerTail>(log_max_size);
	vm_log.log_interface = logger.get();
	vm_log.log_options = td::LogOptions(VERBOSITY_NAME(DEBUG), true, false);
	if (cfg.vm_log_verbosity > 1) {
	  vm_log.log_mask |= vm::VmLog::ExecLocation;
	  if (cfg.vm_log_verbosity > 2) {
		vm_log.log_mask |= vm::VmLog::GasRemaining;
		if (cfg.vm_log_verbosity > 3) {
		  vm_log.log_mask |= vm::VmLog::DumpStack;
		  if (cfg.vm_log_verbosity > 4) {
			vm_log.log_mask |= vm::VmLog::DumpStackVerbose;
			vm_log.log_mask |= vm::VmLog::DumpC5;
		  }
		}
	  }
	}
  }
  vm::VmState vm{new_code, std::move(stack), gas, 1, new_data, vm_log, compute_vm_libraries(cfg)};
  vm.set_max_data_depth(cfg.max_vm_data_depth);
  vm.set_global_version(cfg.global_version);
  vm.set_c7(prepare_vm_c7(cfg));  // tuple with SmartContractInfo
  vm.set_chksig_always_succeed(cfg.ignore_chksig);
  vm.set_stop_on_accept_message(cfg.stop_on_accept_message);

  cp.vm_init_state_hash = vm.get_state_hash();
  cp.exit_code = ~vm.run();
  cp.out_of_gas = (cp.exit_code == ~(int)vm::Excno::out_of_gas);
  cp.vm_final_state_hash = vm.get_final_state_hash(cp.exit_code);
  stack = vm.get_stack_ref();
  cp.vm_steps = (int)vm.get_steps_count();
  gas = vm.get_gas_limits();
  cp.gas_used = std::min<long long>(gas.gas_consumed(), gas.gas_limit);
  cp.accepted = (gas.gas_credit == 0);
  cp.success = (cp.accepted && vm.committed());
  if (cp.accepted & use_msg_state) {
	was_activated = true;
	acc_status = block::Account::acc_active;
  }
  if (precompiled) {
	cp.gas_used = precompiled.value().gas_usage;
	cp.vm_steps = 0;
	cp.vm_init_state_hash = cp.vm_final_state_hash = td::Bits256::zero();
	if (cp.out_of_gas) {
	  return false;
	}
  }
  if(logger != nullptr) cp.vm_log = logger->get_log();
  if(cp.success) {
	cp.new_data = vm.get_committed_state().c4;  // c4 -> persistent data
	cp.actions = vm.get_committed_state().c5;   // c5 -> action list
  }
  cp.mode = 0;
  cp.exit_arg = 0;
  if(!cp.success && stack->depth() > 0) {
	td::RefInt256 tos = stack->tos().as_int();
	if(tos.not_null() && tos->signed_fits_bits(32)) cp.exit_arg = (int)tos->to_long();
  }
  if(cp.accepted) {
	if(account.is_special) cp.gas_fees = td::zero_refint();
	else {
	  cp.gas_fees = cfg.compute_gas_price(cp.gas_used);
	  total_fees += cp.gas_fees;
	  balance -= cp.gas_fees;
	}
  }
  return true;
}