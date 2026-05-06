/*
 * Copyright (c) 2025-2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "ton/ton-io.hpp"

#include "bus.h"

namespace ton::validator::consensus {

std::string Start::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string FinalizeBlock::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

static void format_field(td::StringBuilder& sb, const OurLeaderWindowStarted& v,
                         td::actor::detail::field_tag<&OurLeaderWindowStarted::start_time>) {
  sb << v.start_time.at_unix();
}

std::string OurLeaderWindowStarted::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string CandidateGenerated::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string CandidateReceived::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string ValidationRequest::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string ValidationRequest::response_to_string(const ReturnType& result) {
  std::string str;
  auto accept_fn = [&](const CandidateAccept& accept) { str = PSTRING() << "CandidateAccept{}"; };
  auto reject_fn = [&](const CandidateReject& reject) {
    str = PSTRING() << "CandidateReject{reason=" << reject.reason << "}";
  };
  result.visit(td::overloaded(accept_fn, reject_fn));
  return str;
}

std::string IncomingProtocolMessage::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string OutgoingProtocolMessage::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string IncomingOverlayRequest::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string IncomingOverlayRequest::response_to_string(const ReturnType& response) {
  return PSTRING() << response;
}

static void format_field(td::StringBuilder& sb, const OutgoingOverlayRequest& v,
                         td::actor::detail::field_tag<&OutgoingOverlayRequest::timeout>) {
  sb << v.timeout.in() << " remaining";
}

std::string OutgoingOverlayRequest::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string OutgoingOverlayRequest::response_to_string(const ReturnType& response) {
  return PSTRING() << response;
}

std::string BlockFinalizedInMasterchain::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string MisbehaviorReport::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string TraceEvent::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string NoncriticalParamsUpdated::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

std::string PrecheckCandidateBroadcast::contents_to_string() const {
  return td::actor::stringify_event(*this);
}

}  // namespace ton::validator::consensus
