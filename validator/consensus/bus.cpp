/*
 * Copyright (c) 2025-2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "ton/ton-io.hpp"

#include "bus.h"

namespace ton::validator::consensus {

std::string Start::contents_to_string() const {
  return PSTRING() << "{state=" << state << "}";
}

std::string FinalizeBlock::contents_to_string() const {
  return PSTRING() << "{candidate=" << candidate << ", signatures=" << signatures << "}";
}

std::string OurLeaderWindowStarted::contents_to_string() const {
  return PSTRING() << "{base=" << base << ", state=" << state << ", start_slot=" << start_slot
                   << ", end_slot=" << end_slot << ", start_time=" << start_time.at_unix() << "}";
}

std::string CandidateGenerated::contents_to_string() const {
  return PSTRING() << "{candidate=" << candidate << ", collator_id=" << collator_id << "}";
}

std::string CandidateReceived::contents_to_string() const {
  return PSTRING() << "{candidate=" << candidate << "}";
}

std::string ValidationRequest::contents_to_string() const {
  return PSTRING() << "{state=" << state << ", candidate=" << candidate << "}";
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
  return PSTRING() << "{source=" << source << ", message=" << message << "}";
}

std::string OutgoingProtocolMessage::contents_to_string() const {
  return PSTRING() << "{recipient=" << recipient << ", message=" << message << "}";
}

std::string IncomingOverlayRequest::contents_to_string() const {
  return PSTRING() << "{source=" << source << ", request=" << request << "}";
}

std::string IncomingOverlayRequest::response_to_string(const ReturnType& response) {
  return PSTRING() << response;
}

std::string OutgoingOverlayRequest::contents_to_string() const {
  return PSTRING() << "{destination=" << destination << ", timeout=" << timeout.in()
                   << " remaining, request=" << request << "}";
}

std::string OutgoingOverlayRequest::response_to_string(const ReturnType& response) {
  return PSTRING() << response;
}

std::string BlockFinalizedInMasterchain::contents_to_string() const {
  return PSTRING() << "{block=" << block << "}";
}

std::string MisbehaviorReport::contents_to_string() const {
  return PSTRING() << "{id=" << id << "}";
}

std::string TraceEvent::contents_to_string() const {
  return PSTRING() << "{event=" << event << "}";
}

std::string NoncriticalParamsUpdated::contents_to_string() const {
  return PSTRING() << "{params=" << params << "}";
}

std::string PrecheckCandidateBroadcast::contents_to_string() const {
  return PSTRING() << "{slot=" << slot << ", broadcast_id=" << broadcast_id
                   << ", signature_checked=" << signature_checked << "}";
}

}  // namespace ton::validator::consensus
