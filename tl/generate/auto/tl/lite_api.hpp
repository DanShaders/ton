#pragma once

#include "lite_api.h"

namespace ton {
namespace lite_api {

/**
 * Calls specified function object with the specified object downcasted to the most-derived type.
 * \param[in] obj Object to pass as an argument to the function object.
 * \param[in] func Function object to which the object will be passed.
 * \returns whether function object call has happened. Should always return true for correct parameters.
 */
template <class T>
bool downcast_call(Object &obj, const T &func) {
  switch (obj.get_id()) {
    case adnl_message_query::ID:
      func(static_cast<adnl_message_query &>(obj));
      return true;
    case adnl_message_answer::ID:
      func(static_cast<adnl_message_answer &>(obj));
      return true;
    case liteServer_accountDispatchQueueInfo::ID:
      func(static_cast<liteServer_accountDispatchQueueInfo &>(obj));
      return true;
    case liteServer_accountId::ID:
      func(static_cast<liteServer_accountId &>(obj));
      return true;
    case liteServer_accountState::ID:
      func(static_cast<liteServer_accountState &>(obj));
      return true;
    case liteServer_allShardsInfo::ID:
      func(static_cast<liteServer_allShardsInfo &>(obj));
      return true;
    case liteServer_blockData::ID:
      func(static_cast<liteServer_blockData &>(obj));
      return true;
    case liteServer_blockHeader::ID:
      func(static_cast<liteServer_blockHeader &>(obj));
      return true;
    case liteServer_blockLinkBack::ID:
      func(static_cast<liteServer_blockLinkBack &>(obj));
      return true;
    case liteServer_blockLinkForward::ID:
      func(static_cast<liteServer_blockLinkForward &>(obj));
      return true;
    case liteServer_blockOutMsgQueueSize::ID:
      func(static_cast<liteServer_blockOutMsgQueueSize &>(obj));
      return true;
    case liteServer_blockState::ID:
      func(static_cast<liteServer_blockState &>(obj));
      return true;
    case liteServer_blockTransactions::ID:
      func(static_cast<liteServer_blockTransactions &>(obj));
      return true;
    case liteServer_blockTransactionsExt::ID:
      func(static_cast<liteServer_blockTransactionsExt &>(obj));
      return true;
    case liteServer_configInfo::ID:
      func(static_cast<liteServer_configInfo &>(obj));
      return true;
    case liteServer_currentTime::ID:
      func(static_cast<liteServer_currentTime &>(obj));
      return true;
    case liteServer_dispatchQueueInfo::ID:
      func(static_cast<liteServer_dispatchQueueInfo &>(obj));
      return true;
    case liteServer_dispatchQueueMessage::ID:
      func(static_cast<liteServer_dispatchQueueMessage &>(obj));
      return true;
    case liteServer_dispatchQueueMessages::ID:
      func(static_cast<liteServer_dispatchQueueMessages &>(obj));
      return true;
    case liteServer_error::ID:
      func(static_cast<liteServer_error &>(obj));
      return true;
    case liteServer_libraryEntry::ID:
      func(static_cast<liteServer_libraryEntry &>(obj));
      return true;
    case liteServer_libraryResult::ID:
      func(static_cast<liteServer_libraryResult &>(obj));
      return true;
    case liteServer_libraryResultWithProof::ID:
      func(static_cast<liteServer_libraryResultWithProof &>(obj));
      return true;
    case liteServer_lookupBlockResult::ID:
      func(static_cast<liteServer_lookupBlockResult &>(obj));
      return true;
    case liteServer_masterchainInfo::ID:
      func(static_cast<liteServer_masterchainInfo &>(obj));
      return true;
    case liteServer_masterchainInfoExt::ID:
      func(static_cast<liteServer_masterchainInfoExt &>(obj));
      return true;
    case liteServer_outMsgQueueSize::ID:
      func(static_cast<liteServer_outMsgQueueSize &>(obj));
      return true;
    case liteServer_outMsgQueueSizes::ID:
      func(static_cast<liteServer_outMsgQueueSizes &>(obj));
      return true;
    case liteServer_partialBlockProof::ID:
      func(static_cast<liteServer_partialBlockProof &>(obj));
      return true;
    case liteServer_runMethodResult::ID:
      func(static_cast<liteServer_runMethodResult &>(obj));
      return true;
    case liteServer_sendMsgStatus::ID:
      func(static_cast<liteServer_sendMsgStatus &>(obj));
      return true;
    case liteServer_shardBlockLink::ID:
      func(static_cast<liteServer_shardBlockLink &>(obj));
      return true;
    case liteServer_shardBlockProof::ID:
      func(static_cast<liteServer_shardBlockProof &>(obj));
      return true;
    case liteServer_shardInfo::ID:
      func(static_cast<liteServer_shardInfo &>(obj));
      return true;
    case liteServer_signature::ID:
      func(static_cast<liteServer_signature &>(obj));
      return true;
    case liteServer_signatureSet::ID:
      func(static_cast<liteServer_signatureSet &>(obj));
      return true;
    case liteServer_transactionId::ID:
      func(static_cast<liteServer_transactionId &>(obj));
      return true;
    case liteServer_transactionId3::ID:
      func(static_cast<liteServer_transactionId3 &>(obj));
      return true;
    case liteServer_transactionInfo::ID:
      func(static_cast<liteServer_transactionInfo &>(obj));
      return true;
    case liteServer_transactionList::ID:
      func(static_cast<liteServer_transactionList &>(obj));
      return true;
    case liteServer_transactionMetadata::ID:
      func(static_cast<liteServer_transactionMetadata &>(obj));
      return true;
    case liteServer_validatorStats::ID:
      func(static_cast<liteServer_validatorStats &>(obj));
      return true;
    case liteServer_version::ID:
      func(static_cast<liteServer_version &>(obj));
      return true;
    case liteServer_debug_verbosity::ID:
      func(static_cast<liteServer_debug_verbosity &>(obj));
      return true;
    case liteServer_nonfinal_candidate::ID:
      func(static_cast<liteServer_nonfinal_candidate &>(obj));
      return true;
    case liteServer_nonfinal_candidateId::ID:
      func(static_cast<liteServer_nonfinal_candidateId &>(obj));
      return true;
    case liteServer_nonfinal_candidateInfo::ID:
      func(static_cast<liteServer_nonfinal_candidateInfo &>(obj));
      return true;
    case liteServer_nonfinal_validatorGroupInfo::ID:
      func(static_cast<liteServer_nonfinal_validatorGroupInfo &>(obj));
      return true;
    case liteServer_nonfinal_validatorGroups::ID:
      func(static_cast<liteServer_nonfinal_validatorGroups &>(obj));
      return true;
    case tonNode_blockId::ID:
      func(static_cast<tonNode_blockId &>(obj));
      return true;
    case tonNode_blockIdExt::ID:
      func(static_cast<tonNode_blockIdExt &>(obj));
      return true;
    case tonNode_zeroStateIdExt::ID:
      func(static_cast<tonNode_zeroStateIdExt &>(obj));
      return true;
    default:
      return false;
  }
}

/**
* Constructs tl_object_ptr with the object of the same type as the specified object, calls the specified function.
 * \param[in] obj Object to get the type from.
 * \param[in] func Function object to which the new object will be passed.
 * \returns whether function object call has happened. Should always return true for correct parameters.
*/template <class T>
bool downcast_construct(Object &obj, const T &func) {
switch (obj.get_id()) {    case adnl_message_query::ID:
      func(create_tl_object<adnl_message_query>());
      return true;
    case adnl_message_answer::ID:
      func(create_tl_object<adnl_message_answer>());
      return true;
    case liteServer_accountDispatchQueueInfo::ID:
      func(create_tl_object<liteServer_accountDispatchQueueInfo>());
      return true;
    case liteServer_accountId::ID:
      func(create_tl_object<liteServer_accountId>());
      return true;
    case liteServer_accountState::ID:
      func(create_tl_object<liteServer_accountState>());
      return true;
    case liteServer_allShardsInfo::ID:
      func(create_tl_object<liteServer_allShardsInfo>());
      return true;
    case liteServer_blockData::ID:
      func(create_tl_object<liteServer_blockData>());
      return true;
    case liteServer_blockHeader::ID:
      func(create_tl_object<liteServer_blockHeader>());
      return true;
    case liteServer_blockLinkBack::ID:
      func(create_tl_object<liteServer_blockLinkBack>());
      return true;
    case liteServer_blockLinkForward::ID:
      func(create_tl_object<liteServer_blockLinkForward>());
      return true;
    case liteServer_blockOutMsgQueueSize::ID:
      func(create_tl_object<liteServer_blockOutMsgQueueSize>());
      return true;
    case liteServer_blockState::ID:
      func(create_tl_object<liteServer_blockState>());
      return true;
    case liteServer_blockTransactions::ID:
      func(create_tl_object<liteServer_blockTransactions>());
      return true;
    case liteServer_blockTransactionsExt::ID:
      func(create_tl_object<liteServer_blockTransactionsExt>());
      return true;
    case liteServer_configInfo::ID:
      func(create_tl_object<liteServer_configInfo>());
      return true;
    case liteServer_currentTime::ID:
      func(create_tl_object<liteServer_currentTime>());
      return true;
    case liteServer_dispatchQueueInfo::ID:
      func(create_tl_object<liteServer_dispatchQueueInfo>());
      return true;
    case liteServer_dispatchQueueMessage::ID:
      func(create_tl_object<liteServer_dispatchQueueMessage>());
      return true;
    case liteServer_dispatchQueueMessages::ID:
      func(create_tl_object<liteServer_dispatchQueueMessages>());
      return true;
    case liteServer_error::ID:
      func(create_tl_object<liteServer_error>());
      return true;
    case liteServer_libraryEntry::ID:
      func(create_tl_object<liteServer_libraryEntry>());
      return true;
    case liteServer_libraryResult::ID:
      func(create_tl_object<liteServer_libraryResult>());
      return true;
    case liteServer_libraryResultWithProof::ID:
      func(create_tl_object<liteServer_libraryResultWithProof>());
      return true;
    case liteServer_lookupBlockResult::ID:
      func(create_tl_object<liteServer_lookupBlockResult>());
      return true;
    case liteServer_masterchainInfo::ID:
      func(create_tl_object<liteServer_masterchainInfo>());
      return true;
    case liteServer_masterchainInfoExt::ID:
      func(create_tl_object<liteServer_masterchainInfoExt>());
      return true;
    case liteServer_outMsgQueueSize::ID:
      func(create_tl_object<liteServer_outMsgQueueSize>());
      return true;
    case liteServer_outMsgQueueSizes::ID:
      func(create_tl_object<liteServer_outMsgQueueSizes>());
      return true;
    case liteServer_partialBlockProof::ID:
      func(create_tl_object<liteServer_partialBlockProof>());
      return true;
    case liteServer_runMethodResult::ID:
      func(create_tl_object<liteServer_runMethodResult>());
      return true;
    case liteServer_sendMsgStatus::ID:
      func(create_tl_object<liteServer_sendMsgStatus>());
      return true;
    case liteServer_shardBlockLink::ID:
      func(create_tl_object<liteServer_shardBlockLink>());
      return true;
    case liteServer_shardBlockProof::ID:
      func(create_tl_object<liteServer_shardBlockProof>());
      return true;
    case liteServer_shardInfo::ID:
      func(create_tl_object<liteServer_shardInfo>());
      return true;
    case liteServer_signature::ID:
      func(create_tl_object<liteServer_signature>());
      return true;
    case liteServer_signatureSet::ID:
      func(create_tl_object<liteServer_signatureSet>());
      return true;
    case liteServer_transactionId::ID:
      func(create_tl_object<liteServer_transactionId>());
      return true;
    case liteServer_transactionId3::ID:
      func(create_tl_object<liteServer_transactionId3>());
      return true;
    case liteServer_transactionInfo::ID:
      func(create_tl_object<liteServer_transactionInfo>());
      return true;
    case liteServer_transactionList::ID:
      func(create_tl_object<liteServer_transactionList>());
      return true;
    case liteServer_transactionMetadata::ID:
      func(create_tl_object<liteServer_transactionMetadata>());
      return true;
    case liteServer_validatorStats::ID:
      func(create_tl_object<liteServer_validatorStats>());
      return true;
    case liteServer_version::ID:
      func(create_tl_object<liteServer_version>());
      return true;
    case liteServer_debug_verbosity::ID:
      func(create_tl_object<liteServer_debug_verbosity>());
      return true;
    case liteServer_nonfinal_candidate::ID:
      func(create_tl_object<liteServer_nonfinal_candidate>());
      return true;
    case liteServer_nonfinal_candidateId::ID:
      func(create_tl_object<liteServer_nonfinal_candidateId>());
      return true;
    case liteServer_nonfinal_candidateInfo::ID:
      func(create_tl_object<liteServer_nonfinal_candidateInfo>());
      return true;
    case liteServer_nonfinal_validatorGroupInfo::ID:
      func(create_tl_object<liteServer_nonfinal_validatorGroupInfo>());
      return true;
    case liteServer_nonfinal_validatorGroups::ID:
      func(create_tl_object<liteServer_nonfinal_validatorGroups>());
      return true;
    case tonNode_blockId::ID:
      func(create_tl_object<tonNode_blockId>());
      return true;
    case tonNode_blockIdExt::ID:
      func(create_tl_object<tonNode_blockIdExt>());
      return true;
    case tonNode_zeroStateIdExt::ID:
      func(create_tl_object<tonNode_zeroStateIdExt>());
      return true;
    default:
      return false;
  }
}

/**
 * Calls specified function object with the specified object downcasted to the most-derived type.
 * \param[in] obj Object to pass as an argument to the function object.
 * \param[in] func Function object to which the object will be passed.
 * \returns whether function object call has happened. Should always return true for correct parameters.
 */
template <class T>
bool downcast_call(Function &obj, const T &func) {
  switch (obj.get_id()) {
    case liteServer_getAccountState::ID:
      func(static_cast<liteServer_getAccountState &>(obj));
      return true;
    case liteServer_getAccountStatePrunned::ID:
      func(static_cast<liteServer_getAccountStatePrunned &>(obj));
      return true;
    case liteServer_getAllShardsInfo::ID:
      func(static_cast<liteServer_getAllShardsInfo &>(obj));
      return true;
    case liteServer_getBlock::ID:
      func(static_cast<liteServer_getBlock &>(obj));
      return true;
    case liteServer_getBlockHeader::ID:
      func(static_cast<liteServer_getBlockHeader &>(obj));
      return true;
    case liteServer_getBlockOutMsgQueueSize::ID:
      func(static_cast<liteServer_getBlockOutMsgQueueSize &>(obj));
      return true;
    case liteServer_getBlockProof::ID:
      func(static_cast<liteServer_getBlockProof &>(obj));
      return true;
    case liteServer_getConfigAll::ID:
      func(static_cast<liteServer_getConfigAll &>(obj));
      return true;
    case liteServer_getConfigParams::ID:
      func(static_cast<liteServer_getConfigParams &>(obj));
      return true;
    case liteServer_getDispatchQueueInfo::ID:
      func(static_cast<liteServer_getDispatchQueueInfo &>(obj));
      return true;
    case liteServer_getDispatchQueueMessages::ID:
      func(static_cast<liteServer_getDispatchQueueMessages &>(obj));
      return true;
    case liteServer_getLibraries::ID:
      func(static_cast<liteServer_getLibraries &>(obj));
      return true;
    case liteServer_getLibrariesWithProof::ID:
      func(static_cast<liteServer_getLibrariesWithProof &>(obj));
      return true;
    case liteServer_getMasterchainInfo::ID:
      func(static_cast<liteServer_getMasterchainInfo &>(obj));
      return true;
    case liteServer_getMasterchainInfoExt::ID:
      func(static_cast<liteServer_getMasterchainInfoExt &>(obj));
      return true;
    case liteServer_getOneTransaction::ID:
      func(static_cast<liteServer_getOneTransaction &>(obj));
      return true;
    case liteServer_getOutMsgQueueSizes::ID:
      func(static_cast<liteServer_getOutMsgQueueSizes &>(obj));
      return true;
    case liteServer_getShardBlockProof::ID:
      func(static_cast<liteServer_getShardBlockProof &>(obj));
      return true;
    case liteServer_getShardInfo::ID:
      func(static_cast<liteServer_getShardInfo &>(obj));
      return true;
    case liteServer_getState::ID:
      func(static_cast<liteServer_getState &>(obj));
      return true;
    case liteServer_getTime::ID:
      func(static_cast<liteServer_getTime &>(obj));
      return true;
    case liteServer_getTransactions::ID:
      func(static_cast<liteServer_getTransactions &>(obj));
      return true;
    case liteServer_getValidatorStats::ID:
      func(static_cast<liteServer_getValidatorStats &>(obj));
      return true;
    case liteServer_getVersion::ID:
      func(static_cast<liteServer_getVersion &>(obj));
      return true;
    case liteServer_listBlockTransactions::ID:
      func(static_cast<liteServer_listBlockTransactions &>(obj));
      return true;
    case liteServer_listBlockTransactionsExt::ID:
      func(static_cast<liteServer_listBlockTransactionsExt &>(obj));
      return true;
    case liteServer_lookupBlock::ID:
      func(static_cast<liteServer_lookupBlock &>(obj));
      return true;
    case liteServer_lookupBlockWithProof::ID:
      func(static_cast<liteServer_lookupBlockWithProof &>(obj));
      return true;
    case liteServer_nonfinal_getCandidate::ID:
      func(static_cast<liteServer_nonfinal_getCandidate &>(obj));
      return true;
    case liteServer_nonfinal_getValidatorGroups::ID:
      func(static_cast<liteServer_nonfinal_getValidatorGroups &>(obj));
      return true;
    case liteServer_query::ID:
      func(static_cast<liteServer_query &>(obj));
      return true;
    case liteServer_queryPrefix::ID:
      func(static_cast<liteServer_queryPrefix &>(obj));
      return true;
    case liteServer_runSmcMethod::ID:
      func(static_cast<liteServer_runSmcMethod &>(obj));
      return true;
    case liteServer_sendMessage::ID:
      func(static_cast<liteServer_sendMessage &>(obj));
      return true;
    case liteServer_waitMasterchainSeqno::ID:
      func(static_cast<liteServer_waitMasterchainSeqno &>(obj));
      return true;
    default:
      return false;
  }
}

/**
* Constructs tl_object_ptr with the object of the same type as the specified object, calls the specified function.
 * \param[in] obj Object to get the type from.
 * \param[in] func Function object to which the new object will be passed.
 * \returns whether function object call has happened. Should always return true for correct parameters.
*/template <class T>
bool downcast_construct(Function &obj, const T &func) {
switch (obj.get_id()) {    case liteServer_getAccountState::ID:
      func(create_tl_object<liteServer_getAccountState>());
      return true;
    case liteServer_getAccountStatePrunned::ID:
      func(create_tl_object<liteServer_getAccountStatePrunned>());
      return true;
    case liteServer_getAllShardsInfo::ID:
      func(create_tl_object<liteServer_getAllShardsInfo>());
      return true;
    case liteServer_getBlock::ID:
      func(create_tl_object<liteServer_getBlock>());
      return true;
    case liteServer_getBlockHeader::ID:
      func(create_tl_object<liteServer_getBlockHeader>());
      return true;
    case liteServer_getBlockOutMsgQueueSize::ID:
      func(create_tl_object<liteServer_getBlockOutMsgQueueSize>());
      return true;
    case liteServer_getBlockProof::ID:
      func(create_tl_object<liteServer_getBlockProof>());
      return true;
    case liteServer_getConfigAll::ID:
      func(create_tl_object<liteServer_getConfigAll>());
      return true;
    case liteServer_getConfigParams::ID:
      func(create_tl_object<liteServer_getConfigParams>());
      return true;
    case liteServer_getDispatchQueueInfo::ID:
      func(create_tl_object<liteServer_getDispatchQueueInfo>());
      return true;
    case liteServer_getDispatchQueueMessages::ID:
      func(create_tl_object<liteServer_getDispatchQueueMessages>());
      return true;
    case liteServer_getLibraries::ID:
      func(create_tl_object<liteServer_getLibraries>());
      return true;
    case liteServer_getLibrariesWithProof::ID:
      func(create_tl_object<liteServer_getLibrariesWithProof>());
      return true;
    case liteServer_getMasterchainInfo::ID:
      func(create_tl_object<liteServer_getMasterchainInfo>());
      return true;
    case liteServer_getMasterchainInfoExt::ID:
      func(create_tl_object<liteServer_getMasterchainInfoExt>());
      return true;
    case liteServer_getOneTransaction::ID:
      func(create_tl_object<liteServer_getOneTransaction>());
      return true;
    case liteServer_getOutMsgQueueSizes::ID:
      func(create_tl_object<liteServer_getOutMsgQueueSizes>());
      return true;
    case liteServer_getShardBlockProof::ID:
      func(create_tl_object<liteServer_getShardBlockProof>());
      return true;
    case liteServer_getShardInfo::ID:
      func(create_tl_object<liteServer_getShardInfo>());
      return true;
    case liteServer_getState::ID:
      func(create_tl_object<liteServer_getState>());
      return true;
    case liteServer_getTime::ID:
      func(create_tl_object<liteServer_getTime>());
      return true;
    case liteServer_getTransactions::ID:
      func(create_tl_object<liteServer_getTransactions>());
      return true;
    case liteServer_getValidatorStats::ID:
      func(create_tl_object<liteServer_getValidatorStats>());
      return true;
    case liteServer_getVersion::ID:
      func(create_tl_object<liteServer_getVersion>());
      return true;
    case liteServer_listBlockTransactions::ID:
      func(create_tl_object<liteServer_listBlockTransactions>());
      return true;
    case liteServer_listBlockTransactionsExt::ID:
      func(create_tl_object<liteServer_listBlockTransactionsExt>());
      return true;
    case liteServer_lookupBlock::ID:
      func(create_tl_object<liteServer_lookupBlock>());
      return true;
    case liteServer_lookupBlockWithProof::ID:
      func(create_tl_object<liteServer_lookupBlockWithProof>());
      return true;
    case liteServer_nonfinal_getCandidate::ID:
      func(create_tl_object<liteServer_nonfinal_getCandidate>());
      return true;
    case liteServer_nonfinal_getValidatorGroups::ID:
      func(create_tl_object<liteServer_nonfinal_getValidatorGroups>());
      return true;
    case liteServer_query::ID:
      func(create_tl_object<liteServer_query>());
      return true;
    case liteServer_queryPrefix::ID:
      func(create_tl_object<liteServer_queryPrefix>());
      return true;
    case liteServer_runSmcMethod::ID:
      func(create_tl_object<liteServer_runSmcMethod>());
      return true;
    case liteServer_sendMessage::ID:
      func(create_tl_object<liteServer_sendMessage>());
      return true;
    case liteServer_waitMasterchainSeqno::ID:
      func(create_tl_object<liteServer_waitMasterchainSeqno>());
      return true;
    default:
      return false;
  }
}

/**
 * Calls specified function object with the specified object downcasted to the most-derived type.
 * \param[in] obj Object to pass as an argument to the function object.
 * \param[in] func Function object to which the object will be passed.
 * \returns whether function object call has happened. Should always return true for correct parameters.
 */
template <class T>
bool downcast_call(adnl_Message &obj, const T &func) {
  switch (obj.get_id()) {
    case adnl_message_query::ID:
      func(static_cast<adnl_message_query &>(obj));
      return true;
    case adnl_message_answer::ID:
      func(static_cast<adnl_message_answer &>(obj));
      return true;
    default:
      return false;
  }
}

/**
* Constructs tl_object_ptr with the object of the same type as the specified object, calls the specified function.
 * \param[in] obj Object to get the type from.
 * \param[in] func Function object to which the new object will be passed.
 * \returns whether function object call has happened. Should always return true for correct parameters.
*/template <class T>
bool downcast_construct(adnl_Message &obj, const T &func) {
switch (obj.get_id()) {    case adnl_message_query::ID:
      func(create_tl_object<adnl_message_query>());
      return true;
    case adnl_message_answer::ID:
      func(create_tl_object<adnl_message_answer>());
      return true;
    default:
      return false;
  }
}

/**
 * Calls specified function object with the specified object downcasted to the most-derived type.
 * \param[in] obj Object to pass as an argument to the function object.
 * \param[in] func Function object to which the object will be passed.
 * \returns whether function object call has happened. Should always return true for correct parameters.
 */
template <class T>
bool downcast_call(liteServer_BlockLink &obj, const T &func) {
  switch (obj.get_id()) {
    case liteServer_blockLinkBack::ID:
      func(static_cast<liteServer_blockLinkBack &>(obj));
      return true;
    case liteServer_blockLinkForward::ID:
      func(static_cast<liteServer_blockLinkForward &>(obj));
      return true;
    default:
      return false;
  }
}

/**
* Constructs tl_object_ptr with the object of the same type as the specified object, calls the specified function.
 * \param[in] obj Object to get the type from.
 * \param[in] func Function object to which the new object will be passed.
 * \returns whether function object call has happened. Should always return true for correct parameters.
*/template <class T>
bool downcast_construct(liteServer_BlockLink &obj, const T &func) {
switch (obj.get_id()) {    case liteServer_blockLinkBack::ID:
      func(create_tl_object<liteServer_blockLinkBack>());
      return true;
    case liteServer_blockLinkForward::ID:
      func(create_tl_object<liteServer_blockLinkForward>());
      return true;
    default:
      return false;
  }
}

}  // namespace lite_api
}  // namespace ton 
