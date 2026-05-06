/*
 * Copyright (c) 2025-2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "bus.h"

namespace ton::validator::consensus::simplex {

std::string BroadcastVote::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string NotarizationObserved::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string FinalizationObserved::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string LeaderWindowObserved::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

static void format_field(td::StringBuilder& sb, const WaitForParent& v,
                         td::actor::detail::field_tag<&WaitForParent::candidate>) {
  sb << "{id=" << v.candidate->id << ", parent_id=" << v.candidate->parent_id << "}";
}

std::string WaitForParent::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string ResolveCandidate::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

static void format_field(td::StringBuilder& sb, const StoreCandidate& v,
                         td::actor::detail::field_tag<&StoreCandidate::candidate>) {
  sb << "{id=" << v.candidate->id << "}";
}

std::string StoreCandidate::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string ResolveState::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string ResolveState::response_to_string(const ReturnType& result) {
  return PSTRING() << "ResolvedState{state=" << result.state << ", gen_utime_exact=" << result.gen_utime_exact << "}";
}

std::string SaveCertificate::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

namespace {

class SimplexCollatorSchedule : public CollatorSchedule {
 public:
  SimplexCollatorSchedule(td::uint32 slots_per_leader_window, td::uint32 leaders_count)
      : slots_per_leader_window_(slots_per_leader_window), leaders_count_(leaders_count) {
  }

  PeerValidatorId expected_collator_for(td::uint32 slot) const override {
    return PeerValidatorId{slot / slots_per_leader_window_ % leaders_count_};
  }

 private:
  td::uint32 slots_per_leader_window_;
  td::uint32 leaders_count_;
};

}  // namespace

void Bus::populate_collator_schedule() {
  auto validators = static_cast<td::uint32>(validator_set.size());
  collator_schedule = td::make_ref<SimplexCollatorSchedule>(config.slots_per_leader_window, validators);
}

}  // namespace ton::validator::consensus::simplex
