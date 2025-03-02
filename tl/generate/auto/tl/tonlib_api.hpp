#pragma once

#include "tonlib_api.h"

namespace ton {
namespace tonlib_api {

/**
 * Calls specified function object with the specified object downcasted to the most-derived type.
 * \param[in] obj Object to pass as an argument to the function object.
 * \param[in] func Function object to which the object will be passed.
 * \returns whether function object call has happened. Should always return true for correct parameters.
 */
template <class T>
bool downcast_call(Object &obj, const T &func) {
  switch (obj.get_id()) {
    case accountAddress::ID:
      func(static_cast<accountAddress &>(obj));
      return true;
    case accountList::ID:
      func(static_cast<accountList &>(obj));
      return true;
    case accountRevisionList::ID:
      func(static_cast<accountRevisionList &>(obj));
      return true;
    case raw_accountState::ID:
      func(static_cast<raw_accountState &>(obj));
      return true;
    case wallet_v3_accountState::ID:
      func(static_cast<wallet_v3_accountState &>(obj));
      return true;
    case wallet_v4_accountState::ID:
      func(static_cast<wallet_v4_accountState &>(obj));
      return true;
    case wallet_highload_v1_accountState::ID:
      func(static_cast<wallet_highload_v1_accountState &>(obj));
      return true;
    case wallet_highload_v2_accountState::ID:
      func(static_cast<wallet_highload_v2_accountState &>(obj));
      return true;
    case dns_accountState::ID:
      func(static_cast<dns_accountState &>(obj));
      return true;
    case rwallet_accountState::ID:
      func(static_cast<rwallet_accountState &>(obj));
      return true;
    case pchan_accountState::ID:
      func(static_cast<pchan_accountState &>(obj));
      return true;
    case uninited_accountState::ID:
      func(static_cast<uninited_accountState &>(obj));
      return true;
    case actionNoop::ID:
      func(static_cast<actionNoop &>(obj));
      return true;
    case actionMsg::ID:
      func(static_cast<actionMsg &>(obj));
      return true;
    case actionDns::ID:
      func(static_cast<actionDns &>(obj));
      return true;
    case actionPchan::ID:
      func(static_cast<actionPchan &>(obj));
      return true;
    case actionRwallet::ID:
      func(static_cast<actionRwallet &>(obj));
      return true;
    case adnlAddress::ID:
      func(static_cast<adnlAddress &>(obj));
      return true;
    case bip39Hints::ID:
      func(static_cast<bip39Hints &>(obj));
      return true;
    case config::ID:
      func(static_cast<config &>(obj));
      return true;
    case configInfo::ID:
      func(static_cast<configInfo &>(obj));
      return true;
    case data::ID:
      func(static_cast<data &>(obj));
      return true;
    case error::ID:
      func(static_cast<error &>(obj));
      return true;
    case exportedEncryptedKey::ID:
      func(static_cast<exportedEncryptedKey &>(obj));
      return true;
    case exportedKey::ID:
      func(static_cast<exportedKey &>(obj));
      return true;
    case exportedPemKey::ID:
      func(static_cast<exportedPemKey &>(obj));
      return true;
    case exportedUnencryptedKey::ID:
      func(static_cast<exportedUnencryptedKey &>(obj));
      return true;
    case extraCurrency::ID:
      func(static_cast<extraCurrency &>(obj));
      return true;
    case fees::ID:
      func(static_cast<fees &>(obj));
      return true;
    case fullAccountState::ID:
      func(static_cast<fullAccountState &>(obj));
      return true;
    case raw_initialAccountState::ID:
      func(static_cast<raw_initialAccountState &>(obj));
      return true;
    case wallet_v3_initialAccountState::ID:
      func(static_cast<wallet_v3_initialAccountState &>(obj));
      return true;
    case wallet_v4_initialAccountState::ID:
      func(static_cast<wallet_v4_initialAccountState &>(obj));
      return true;
    case wallet_highload_v1_initialAccountState::ID:
      func(static_cast<wallet_highload_v1_initialAccountState &>(obj));
      return true;
    case wallet_highload_v2_initialAccountState::ID:
      func(static_cast<wallet_highload_v2_initialAccountState &>(obj));
      return true;
    case rwallet_initialAccountState::ID:
      func(static_cast<rwallet_initialAccountState &>(obj));
      return true;
    case dns_initialAccountState::ID:
      func(static_cast<dns_initialAccountState &>(obj));
      return true;
    case pchan_initialAccountState::ID:
      func(static_cast<pchan_initialAccountState &>(obj));
      return true;
    case inputKeyRegular::ID:
      func(static_cast<inputKeyRegular &>(obj));
      return true;
    case inputKeyFake::ID:
      func(static_cast<inputKeyFake &>(obj));
      return true;
    case key::ID:
      func(static_cast<key &>(obj));
      return true;
    case keyStoreTypeDirectory::ID:
      func(static_cast<keyStoreTypeDirectory &>(obj));
      return true;
    case keyStoreTypeInMemory::ID:
      func(static_cast<keyStoreTypeInMemory &>(obj));
      return true;
    case logStreamDefault::ID:
      func(static_cast<logStreamDefault &>(obj));
      return true;
    case logStreamFile::ID:
      func(static_cast<logStreamFile &>(obj));
      return true;
    case logStreamEmpty::ID:
      func(static_cast<logStreamEmpty &>(obj));
      return true;
    case logTags::ID:
      func(static_cast<logTags &>(obj));
      return true;
    case logVerbosityLevel::ID:
      func(static_cast<logVerbosityLevel &>(obj));
      return true;
    case ok::ID:
      func(static_cast<ok &>(obj));
      return true;
    case options::ID:
      func(static_cast<options &>(obj));
      return true;
    case syncStateDone::ID:
      func(static_cast<syncStateDone &>(obj));
      return true;
    case syncStateInProgress::ID:
      func(static_cast<syncStateInProgress &>(obj));
      return true;
    case unpackedAccountAddress::ID:
      func(static_cast<unpackedAccountAddress &>(obj));
      return true;
    case updateSendLiteServerQuery::ID:
      func(static_cast<updateSendLiteServerQuery &>(obj));
      return true;
    case updateSyncState::ID:
      func(static_cast<updateSyncState &>(obj));
      return true;
    case blocks_accountTransactionId::ID:
      func(static_cast<blocks_accountTransactionId &>(obj));
      return true;
    case blocks_blockLinkBack::ID:
      func(static_cast<blocks_blockLinkBack &>(obj));
      return true;
    case blocks_blockSignatures::ID:
      func(static_cast<blocks_blockSignatures &>(obj));
      return true;
    case blocks_header::ID:
      func(static_cast<blocks_header &>(obj));
      return true;
    case blocks_masterchainInfo::ID:
      func(static_cast<blocks_masterchainInfo &>(obj));
      return true;
    case blocks_outMsgQueueSize::ID:
      func(static_cast<blocks_outMsgQueueSize &>(obj));
      return true;
    case blocks_outMsgQueueSizes::ID:
      func(static_cast<blocks_outMsgQueueSizes &>(obj));
      return true;
    case blocks_shardBlockLink::ID:
      func(static_cast<blocks_shardBlockLink &>(obj));
      return true;
    case blocks_shardBlockProof::ID:
      func(static_cast<blocks_shardBlockProof &>(obj));
      return true;
    case blocks_shards::ID:
      func(static_cast<blocks_shards &>(obj));
      return true;
    case blocks_signature::ID:
      func(static_cast<blocks_signature &>(obj));
      return true;
    case blocks_transactions::ID:
      func(static_cast<blocks_transactions &>(obj));
      return true;
    case blocks_transactionsExt::ID:
      func(static_cast<blocks_transactionsExt &>(obj));
      return true;
    case dns_actionDeleteAll::ID:
      func(static_cast<dns_actionDeleteAll &>(obj));
      return true;
    case dns_actionDelete::ID:
      func(static_cast<dns_actionDelete &>(obj));
      return true;
    case dns_actionSet::ID:
      func(static_cast<dns_actionSet &>(obj));
      return true;
    case dns_entry::ID:
      func(static_cast<dns_entry &>(obj));
      return true;
    case dns_entryDataUnknown::ID:
      func(static_cast<dns_entryDataUnknown &>(obj));
      return true;
    case dns_entryDataText::ID:
      func(static_cast<dns_entryDataText &>(obj));
      return true;
    case dns_entryDataNextResolver::ID:
      func(static_cast<dns_entryDataNextResolver &>(obj));
      return true;
    case dns_entryDataSmcAddress::ID:
      func(static_cast<dns_entryDataSmcAddress &>(obj));
      return true;
    case dns_entryDataAdnlAddress::ID:
      func(static_cast<dns_entryDataAdnlAddress &>(obj));
      return true;
    case dns_entryDataStorageAddress::ID:
      func(static_cast<dns_entryDataStorageAddress &>(obj));
      return true;
    case dns_resolved::ID:
      func(static_cast<dns_resolved &>(obj));
      return true;
    case ton_blockId::ID:
      func(static_cast<ton_blockId &>(obj));
      return true;
    case internal_transactionId::ID:
      func(static_cast<internal_transactionId &>(obj));
      return true;
    case liteServer_info::ID:
      func(static_cast<liteServer_info &>(obj));
      return true;
    case blocks_shortTxId::ID:
      func(static_cast<blocks_shortTxId &>(obj));
      return true;
    case msg_dataRaw::ID:
      func(static_cast<msg_dataRaw &>(obj));
      return true;
    case msg_dataText::ID:
      func(static_cast<msg_dataText &>(obj));
      return true;
    case msg_dataDecryptedText::ID:
      func(static_cast<msg_dataDecryptedText &>(obj));
      return true;
    case msg_dataEncryptedText::ID:
      func(static_cast<msg_dataEncryptedText &>(obj));
      return true;
    case msg_dataDecrypted::ID:
      func(static_cast<msg_dataDecrypted &>(obj));
      return true;
    case msg_dataDecryptedArray::ID:
      func(static_cast<msg_dataDecryptedArray &>(obj));
      return true;
    case msg_dataEncrypted::ID:
      func(static_cast<msg_dataEncrypted &>(obj));
      return true;
    case msg_dataEncryptedArray::ID:
      func(static_cast<msg_dataEncryptedArray &>(obj));
      return true;
    case msg_message::ID:
      func(static_cast<msg_message &>(obj));
      return true;
    case options_configInfo::ID:
      func(static_cast<options_configInfo &>(obj));
      return true;
    case options_info::ID:
      func(static_cast<options_info &>(obj));
      return true;
    case pchan_actionInit::ID:
      func(static_cast<pchan_actionInit &>(obj));
      return true;
    case pchan_actionClose::ID:
      func(static_cast<pchan_actionClose &>(obj));
      return true;
    case pchan_actionTimeout::ID:
      func(static_cast<pchan_actionTimeout &>(obj));
      return true;
    case pchan_config::ID:
      func(static_cast<pchan_config &>(obj));
      return true;
    case pchan_promise::ID:
      func(static_cast<pchan_promise &>(obj));
      return true;
    case pchan_stateInit::ID:
      func(static_cast<pchan_stateInit &>(obj));
      return true;
    case pchan_stateClose::ID:
      func(static_cast<pchan_stateClose &>(obj));
      return true;
    case pchan_statePayout::ID:
      func(static_cast<pchan_statePayout &>(obj));
      return true;
    case query_fees::ID:
      func(static_cast<query_fees &>(obj));
      return true;
    case query_info::ID:
      func(static_cast<query_info &>(obj));
      return true;
    case raw_extMessageInfo::ID:
      func(static_cast<raw_extMessageInfo &>(obj));
      return true;
    case raw_fullAccountState::ID:
      func(static_cast<raw_fullAccountState &>(obj));
      return true;
    case raw_message::ID:
      func(static_cast<raw_message &>(obj));
      return true;
    case raw_transaction::ID:
      func(static_cast<raw_transaction &>(obj));
      return true;
    case raw_transactions::ID:
      func(static_cast<raw_transactions &>(obj));
      return true;
    case rwallet_actionInit::ID:
      func(static_cast<rwallet_actionInit &>(obj));
      return true;
    case rwallet_config::ID:
      func(static_cast<rwallet_config &>(obj));
      return true;
    case rwallet_limit::ID:
      func(static_cast<rwallet_limit &>(obj));
      return true;
    case smc_info::ID:
      func(static_cast<smc_info &>(obj));
      return true;
    case smc_libraryEntry::ID:
      func(static_cast<smc_libraryEntry &>(obj));
      return true;
    case smc_libraryQueryExt_one::ID:
      func(static_cast<smc_libraryQueryExt_one &>(obj));
      return true;
    case smc_libraryQueryExt_scanBoc::ID:
      func(static_cast<smc_libraryQueryExt_scanBoc &>(obj));
      return true;
    case smc_libraryResult::ID:
      func(static_cast<smc_libraryResult &>(obj));
      return true;
    case smc_libraryResultExt::ID:
      func(static_cast<smc_libraryResultExt &>(obj));
      return true;
    case smc_methodIdNumber::ID:
      func(static_cast<smc_methodIdNumber &>(obj));
      return true;
    case smc_methodIdName::ID:
      func(static_cast<smc_methodIdName &>(obj));
      return true;
    case smc_runResult::ID:
      func(static_cast<smc_runResult &>(obj));
      return true;
    case ton_blockIdExt::ID:
      func(static_cast<ton_blockIdExt &>(obj));
      return true;
    case tvm_cell::ID:
      func(static_cast<tvm_cell &>(obj));
      return true;
    case tvm_list::ID:
      func(static_cast<tvm_list &>(obj));
      return true;
    case tvm_numberDecimal::ID:
      func(static_cast<tvm_numberDecimal &>(obj));
      return true;
    case tvm_slice::ID:
      func(static_cast<tvm_slice &>(obj));
      return true;
    case tvm_stackEntrySlice::ID:
      func(static_cast<tvm_stackEntrySlice &>(obj));
      return true;
    case tvm_stackEntryCell::ID:
      func(static_cast<tvm_stackEntryCell &>(obj));
      return true;
    case tvm_stackEntryNumber::ID:
      func(static_cast<tvm_stackEntryNumber &>(obj));
      return true;
    case tvm_stackEntryTuple::ID:
      func(static_cast<tvm_stackEntryTuple &>(obj));
      return true;
    case tvm_stackEntryList::ID:
      func(static_cast<tvm_stackEntryList &>(obj));
      return true;
    case tvm_stackEntryUnsupported::ID:
      func(static_cast<tvm_stackEntryUnsupported &>(obj));
      return true;
    case tvm_tuple::ID:
      func(static_cast<tvm_tuple &>(obj));
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
switch (obj.get_id()) {    case accountAddress::ID:
      func(create_tl_object<accountAddress>());
      return true;
    case accountList::ID:
      func(create_tl_object<accountList>());
      return true;
    case accountRevisionList::ID:
      func(create_tl_object<accountRevisionList>());
      return true;
    case raw_accountState::ID:
      func(create_tl_object<raw_accountState>());
      return true;
    case wallet_v3_accountState::ID:
      func(create_tl_object<wallet_v3_accountState>());
      return true;
    case wallet_v4_accountState::ID:
      func(create_tl_object<wallet_v4_accountState>());
      return true;
    case wallet_highload_v1_accountState::ID:
      func(create_tl_object<wallet_highload_v1_accountState>());
      return true;
    case wallet_highload_v2_accountState::ID:
      func(create_tl_object<wallet_highload_v2_accountState>());
      return true;
    case dns_accountState::ID:
      func(create_tl_object<dns_accountState>());
      return true;
    case rwallet_accountState::ID:
      func(create_tl_object<rwallet_accountState>());
      return true;
    case pchan_accountState::ID:
      func(create_tl_object<pchan_accountState>());
      return true;
    case uninited_accountState::ID:
      func(create_tl_object<uninited_accountState>());
      return true;
    case actionNoop::ID:
      func(create_tl_object<actionNoop>());
      return true;
    case actionMsg::ID:
      func(create_tl_object<actionMsg>());
      return true;
    case actionDns::ID:
      func(create_tl_object<actionDns>());
      return true;
    case actionPchan::ID:
      func(create_tl_object<actionPchan>());
      return true;
    case actionRwallet::ID:
      func(create_tl_object<actionRwallet>());
      return true;
    case adnlAddress::ID:
      func(create_tl_object<adnlAddress>());
      return true;
    case bip39Hints::ID:
      func(create_tl_object<bip39Hints>());
      return true;
    case config::ID:
      func(create_tl_object<config>());
      return true;
    case configInfo::ID:
      func(create_tl_object<configInfo>());
      return true;
    case data::ID:
      func(create_tl_object<data>());
      return true;
    case error::ID:
      func(create_tl_object<error>());
      return true;
    case exportedEncryptedKey::ID:
      func(create_tl_object<exportedEncryptedKey>());
      return true;
    case exportedKey::ID:
      func(create_tl_object<exportedKey>());
      return true;
    case exportedPemKey::ID:
      func(create_tl_object<exportedPemKey>());
      return true;
    case exportedUnencryptedKey::ID:
      func(create_tl_object<exportedUnencryptedKey>());
      return true;
    case extraCurrency::ID:
      func(create_tl_object<extraCurrency>());
      return true;
    case fees::ID:
      func(create_tl_object<fees>());
      return true;
    case fullAccountState::ID:
      func(create_tl_object<fullAccountState>());
      return true;
    case raw_initialAccountState::ID:
      func(create_tl_object<raw_initialAccountState>());
      return true;
    case wallet_v3_initialAccountState::ID:
      func(create_tl_object<wallet_v3_initialAccountState>());
      return true;
    case wallet_v4_initialAccountState::ID:
      func(create_tl_object<wallet_v4_initialAccountState>());
      return true;
    case wallet_highload_v1_initialAccountState::ID:
      func(create_tl_object<wallet_highload_v1_initialAccountState>());
      return true;
    case wallet_highload_v2_initialAccountState::ID:
      func(create_tl_object<wallet_highload_v2_initialAccountState>());
      return true;
    case rwallet_initialAccountState::ID:
      func(create_tl_object<rwallet_initialAccountState>());
      return true;
    case dns_initialAccountState::ID:
      func(create_tl_object<dns_initialAccountState>());
      return true;
    case pchan_initialAccountState::ID:
      func(create_tl_object<pchan_initialAccountState>());
      return true;
    case inputKeyRegular::ID:
      func(create_tl_object<inputKeyRegular>());
      return true;
    case inputKeyFake::ID:
      func(create_tl_object<inputKeyFake>());
      return true;
    case key::ID:
      func(create_tl_object<key>());
      return true;
    case keyStoreTypeDirectory::ID:
      func(create_tl_object<keyStoreTypeDirectory>());
      return true;
    case keyStoreTypeInMemory::ID:
      func(create_tl_object<keyStoreTypeInMemory>());
      return true;
    case logStreamDefault::ID:
      func(create_tl_object<logStreamDefault>());
      return true;
    case logStreamFile::ID:
      func(create_tl_object<logStreamFile>());
      return true;
    case logStreamEmpty::ID:
      func(create_tl_object<logStreamEmpty>());
      return true;
    case logTags::ID:
      func(create_tl_object<logTags>());
      return true;
    case logVerbosityLevel::ID:
      func(create_tl_object<logVerbosityLevel>());
      return true;
    case ok::ID:
      func(create_tl_object<ok>());
      return true;
    case options::ID:
      func(create_tl_object<options>());
      return true;
    case syncStateDone::ID:
      func(create_tl_object<syncStateDone>());
      return true;
    case syncStateInProgress::ID:
      func(create_tl_object<syncStateInProgress>());
      return true;
    case unpackedAccountAddress::ID:
      func(create_tl_object<unpackedAccountAddress>());
      return true;
    case updateSendLiteServerQuery::ID:
      func(create_tl_object<updateSendLiteServerQuery>());
      return true;
    case updateSyncState::ID:
      func(create_tl_object<updateSyncState>());
      return true;
    case blocks_accountTransactionId::ID:
      func(create_tl_object<blocks_accountTransactionId>());
      return true;
    case blocks_blockLinkBack::ID:
      func(create_tl_object<blocks_blockLinkBack>());
      return true;
    case blocks_blockSignatures::ID:
      func(create_tl_object<blocks_blockSignatures>());
      return true;
    case blocks_header::ID:
      func(create_tl_object<blocks_header>());
      return true;
    case blocks_masterchainInfo::ID:
      func(create_tl_object<blocks_masterchainInfo>());
      return true;
    case blocks_outMsgQueueSize::ID:
      func(create_tl_object<blocks_outMsgQueueSize>());
      return true;
    case blocks_outMsgQueueSizes::ID:
      func(create_tl_object<blocks_outMsgQueueSizes>());
      return true;
    case blocks_shardBlockLink::ID:
      func(create_tl_object<blocks_shardBlockLink>());
      return true;
    case blocks_shardBlockProof::ID:
      func(create_tl_object<blocks_shardBlockProof>());
      return true;
    case blocks_shards::ID:
      func(create_tl_object<blocks_shards>());
      return true;
    case blocks_signature::ID:
      func(create_tl_object<blocks_signature>());
      return true;
    case blocks_transactions::ID:
      func(create_tl_object<blocks_transactions>());
      return true;
    case blocks_transactionsExt::ID:
      func(create_tl_object<blocks_transactionsExt>());
      return true;
    case dns_actionDeleteAll::ID:
      func(create_tl_object<dns_actionDeleteAll>());
      return true;
    case dns_actionDelete::ID:
      func(create_tl_object<dns_actionDelete>());
      return true;
    case dns_actionSet::ID:
      func(create_tl_object<dns_actionSet>());
      return true;
    case dns_entry::ID:
      func(create_tl_object<dns_entry>());
      return true;
    case dns_entryDataUnknown::ID:
      func(create_tl_object<dns_entryDataUnknown>());
      return true;
    case dns_entryDataText::ID:
      func(create_tl_object<dns_entryDataText>());
      return true;
    case dns_entryDataNextResolver::ID:
      func(create_tl_object<dns_entryDataNextResolver>());
      return true;
    case dns_entryDataSmcAddress::ID:
      func(create_tl_object<dns_entryDataSmcAddress>());
      return true;
    case dns_entryDataAdnlAddress::ID:
      func(create_tl_object<dns_entryDataAdnlAddress>());
      return true;
    case dns_entryDataStorageAddress::ID:
      func(create_tl_object<dns_entryDataStorageAddress>());
      return true;
    case dns_resolved::ID:
      func(create_tl_object<dns_resolved>());
      return true;
    case ton_blockId::ID:
      func(create_tl_object<ton_blockId>());
      return true;
    case internal_transactionId::ID:
      func(create_tl_object<internal_transactionId>());
      return true;
    case liteServer_info::ID:
      func(create_tl_object<liteServer_info>());
      return true;
    case blocks_shortTxId::ID:
      func(create_tl_object<blocks_shortTxId>());
      return true;
    case msg_dataRaw::ID:
      func(create_tl_object<msg_dataRaw>());
      return true;
    case msg_dataText::ID:
      func(create_tl_object<msg_dataText>());
      return true;
    case msg_dataDecryptedText::ID:
      func(create_tl_object<msg_dataDecryptedText>());
      return true;
    case msg_dataEncryptedText::ID:
      func(create_tl_object<msg_dataEncryptedText>());
      return true;
    case msg_dataDecrypted::ID:
      func(create_tl_object<msg_dataDecrypted>());
      return true;
    case msg_dataDecryptedArray::ID:
      func(create_tl_object<msg_dataDecryptedArray>());
      return true;
    case msg_dataEncrypted::ID:
      func(create_tl_object<msg_dataEncrypted>());
      return true;
    case msg_dataEncryptedArray::ID:
      func(create_tl_object<msg_dataEncryptedArray>());
      return true;
    case msg_message::ID:
      func(create_tl_object<msg_message>());
      return true;
    case options_configInfo::ID:
      func(create_tl_object<options_configInfo>());
      return true;
    case options_info::ID:
      func(create_tl_object<options_info>());
      return true;
    case pchan_actionInit::ID:
      func(create_tl_object<pchan_actionInit>());
      return true;
    case pchan_actionClose::ID:
      func(create_tl_object<pchan_actionClose>());
      return true;
    case pchan_actionTimeout::ID:
      func(create_tl_object<pchan_actionTimeout>());
      return true;
    case pchan_config::ID:
      func(create_tl_object<pchan_config>());
      return true;
    case pchan_promise::ID:
      func(create_tl_object<pchan_promise>());
      return true;
    case pchan_stateInit::ID:
      func(create_tl_object<pchan_stateInit>());
      return true;
    case pchan_stateClose::ID:
      func(create_tl_object<pchan_stateClose>());
      return true;
    case pchan_statePayout::ID:
      func(create_tl_object<pchan_statePayout>());
      return true;
    case query_fees::ID:
      func(create_tl_object<query_fees>());
      return true;
    case query_info::ID:
      func(create_tl_object<query_info>());
      return true;
    case raw_extMessageInfo::ID:
      func(create_tl_object<raw_extMessageInfo>());
      return true;
    case raw_fullAccountState::ID:
      func(create_tl_object<raw_fullAccountState>());
      return true;
    case raw_message::ID:
      func(create_tl_object<raw_message>());
      return true;
    case raw_transaction::ID:
      func(create_tl_object<raw_transaction>());
      return true;
    case raw_transactions::ID:
      func(create_tl_object<raw_transactions>());
      return true;
    case rwallet_actionInit::ID:
      func(create_tl_object<rwallet_actionInit>());
      return true;
    case rwallet_config::ID:
      func(create_tl_object<rwallet_config>());
      return true;
    case rwallet_limit::ID:
      func(create_tl_object<rwallet_limit>());
      return true;
    case smc_info::ID:
      func(create_tl_object<smc_info>());
      return true;
    case smc_libraryEntry::ID:
      func(create_tl_object<smc_libraryEntry>());
      return true;
    case smc_libraryQueryExt_one::ID:
      func(create_tl_object<smc_libraryQueryExt_one>());
      return true;
    case smc_libraryQueryExt_scanBoc::ID:
      func(create_tl_object<smc_libraryQueryExt_scanBoc>());
      return true;
    case smc_libraryResult::ID:
      func(create_tl_object<smc_libraryResult>());
      return true;
    case smc_libraryResultExt::ID:
      func(create_tl_object<smc_libraryResultExt>());
      return true;
    case smc_methodIdNumber::ID:
      func(create_tl_object<smc_methodIdNumber>());
      return true;
    case smc_methodIdName::ID:
      func(create_tl_object<smc_methodIdName>());
      return true;
    case smc_runResult::ID:
      func(create_tl_object<smc_runResult>());
      return true;
    case ton_blockIdExt::ID:
      func(create_tl_object<ton_blockIdExt>());
      return true;
    case tvm_cell::ID:
      func(create_tl_object<tvm_cell>());
      return true;
    case tvm_list::ID:
      func(create_tl_object<tvm_list>());
      return true;
    case tvm_numberDecimal::ID:
      func(create_tl_object<tvm_numberDecimal>());
      return true;
    case tvm_slice::ID:
      func(create_tl_object<tvm_slice>());
      return true;
    case tvm_stackEntrySlice::ID:
      func(create_tl_object<tvm_stackEntrySlice>());
      return true;
    case tvm_stackEntryCell::ID:
      func(create_tl_object<tvm_stackEntryCell>());
      return true;
    case tvm_stackEntryNumber::ID:
      func(create_tl_object<tvm_stackEntryNumber>());
      return true;
    case tvm_stackEntryTuple::ID:
      func(create_tl_object<tvm_stackEntryTuple>());
      return true;
    case tvm_stackEntryList::ID:
      func(create_tl_object<tvm_stackEntryList>());
      return true;
    case tvm_stackEntryUnsupported::ID:
      func(create_tl_object<tvm_stackEntryUnsupported>());
      return true;
    case tvm_tuple::ID:
      func(create_tl_object<tvm_tuple>());
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
    case addLogMessage::ID:
      func(static_cast<addLogMessage &>(obj));
      return true;
    case blocks_getBlockHeader::ID:
      func(static_cast<blocks_getBlockHeader &>(obj));
      return true;
    case blocks_getMasterchainBlockSignatures::ID:
      func(static_cast<blocks_getMasterchainBlockSignatures &>(obj));
      return true;
    case blocks_getMasterchainInfo::ID:
      func(static_cast<blocks_getMasterchainInfo &>(obj));
      return true;
    case blocks_getOutMsgQueueSizes::ID:
      func(static_cast<blocks_getOutMsgQueueSizes &>(obj));
      return true;
    case blocks_getShardBlockProof::ID:
      func(static_cast<blocks_getShardBlockProof &>(obj));
      return true;
    case blocks_getShards::ID:
      func(static_cast<blocks_getShards &>(obj));
      return true;
    case blocks_getTransactions::ID:
      func(static_cast<blocks_getTransactions &>(obj));
      return true;
    case blocks_getTransactionsExt::ID:
      func(static_cast<blocks_getTransactionsExt &>(obj));
      return true;
    case blocks_lookupBlock::ID:
      func(static_cast<blocks_lookupBlock &>(obj));
      return true;
    case changeLocalPassword::ID:
      func(static_cast<changeLocalPassword &>(obj));
      return true;
    case close::ID:
      func(static_cast<close &>(obj));
      return true;
    case createNewKey::ID:
      func(static_cast<createNewKey &>(obj));
      return true;
    case createQuery::ID:
      func(static_cast<createQuery &>(obj));
      return true;
    case decrypt::ID:
      func(static_cast<decrypt &>(obj));
      return true;
    case deleteAllKeys::ID:
      func(static_cast<deleteAllKeys &>(obj));
      return true;
    case deleteKey::ID:
      func(static_cast<deleteKey &>(obj));
      return true;
    case dns_resolve::ID:
      func(static_cast<dns_resolve &>(obj));
      return true;
    case encrypt::ID:
      func(static_cast<encrypt &>(obj));
      return true;
    case exportEncryptedKey::ID:
      func(static_cast<exportEncryptedKey &>(obj));
      return true;
    case exportKey::ID:
      func(static_cast<exportKey &>(obj));
      return true;
    case exportPemKey::ID:
      func(static_cast<exportPemKey &>(obj));
      return true;
    case exportUnencryptedKey::ID:
      func(static_cast<exportUnencryptedKey &>(obj));
      return true;
    case getAccountAddress::ID:
      func(static_cast<getAccountAddress &>(obj));
      return true;
    case getAccountState::ID:
      func(static_cast<getAccountState &>(obj));
      return true;
    case getAccountStateByTransaction::ID:
      func(static_cast<getAccountStateByTransaction &>(obj));
      return true;
    case getBip39Hints::ID:
      func(static_cast<getBip39Hints &>(obj));
      return true;
    case getConfigAll::ID:
      func(static_cast<getConfigAll &>(obj));
      return true;
    case getConfigParam::ID:
      func(static_cast<getConfigParam &>(obj));
      return true;
    case getLogStream::ID:
      func(static_cast<getLogStream &>(obj));
      return true;
    case getLogTagVerbosityLevel::ID:
      func(static_cast<getLogTagVerbosityLevel &>(obj));
      return true;
    case getLogTags::ID:
      func(static_cast<getLogTags &>(obj));
      return true;
    case getLogVerbosityLevel::ID:
      func(static_cast<getLogVerbosityLevel &>(obj));
      return true;
    case getShardAccountCell::ID:
      func(static_cast<getShardAccountCell &>(obj));
      return true;
    case getShardAccountCellByTransaction::ID:
      func(static_cast<getShardAccountCellByTransaction &>(obj));
      return true;
    case guessAccount::ID:
      func(static_cast<guessAccount &>(obj));
      return true;
    case guessAccountRevision::ID:
      func(static_cast<guessAccountRevision &>(obj));
      return true;
    case importEncryptedKey::ID:
      func(static_cast<importEncryptedKey &>(obj));
      return true;
    case importKey::ID:
      func(static_cast<importKey &>(obj));
      return true;
    case importPemKey::ID:
      func(static_cast<importPemKey &>(obj));
      return true;
    case importUnencryptedKey::ID:
      func(static_cast<importUnencryptedKey &>(obj));
      return true;
    case init::ID:
      func(static_cast<init &>(obj));
      return true;
    case kdf::ID:
      func(static_cast<kdf &>(obj));
      return true;
    case liteServer_getInfo::ID:
      func(static_cast<liteServer_getInfo &>(obj));
      return true;
    case msg_decrypt::ID:
      func(static_cast<msg_decrypt &>(obj));
      return true;
    case msg_decryptWithProof::ID:
      func(static_cast<msg_decryptWithProof &>(obj));
      return true;
    case onLiteServerQueryError::ID:
      func(static_cast<onLiteServerQueryError &>(obj));
      return true;
    case onLiteServerQueryResult::ID:
      func(static_cast<onLiteServerQueryResult &>(obj));
      return true;
    case options_setConfig::ID:
      func(static_cast<options_setConfig &>(obj));
      return true;
    case options_validateConfig::ID:
      func(static_cast<options_validateConfig &>(obj));
      return true;
    case packAccountAddress::ID:
      func(static_cast<packAccountAddress &>(obj));
      return true;
    case pchan_packPromise::ID:
      func(static_cast<pchan_packPromise &>(obj));
      return true;
    case pchan_signPromise::ID:
      func(static_cast<pchan_signPromise &>(obj));
      return true;
    case pchan_unpackPromise::ID:
      func(static_cast<pchan_unpackPromise &>(obj));
      return true;
    case pchan_validatePromise::ID:
      func(static_cast<pchan_validatePromise &>(obj));
      return true;
    case query_estimateFees::ID:
      func(static_cast<query_estimateFees &>(obj));
      return true;
    case query_forget::ID:
      func(static_cast<query_forget &>(obj));
      return true;
    case query_getInfo::ID:
      func(static_cast<query_getInfo &>(obj));
      return true;
    case query_send::ID:
      func(static_cast<query_send &>(obj));
      return true;
    case raw_createAndSendMessage::ID:
      func(static_cast<raw_createAndSendMessage &>(obj));
      return true;
    case raw_createQuery::ID:
      func(static_cast<raw_createQuery &>(obj));
      return true;
    case raw_getAccountState::ID:
      func(static_cast<raw_getAccountState &>(obj));
      return true;
    case raw_getAccountStateByTransaction::ID:
      func(static_cast<raw_getAccountStateByTransaction &>(obj));
      return true;
    case raw_getTransactions::ID:
      func(static_cast<raw_getTransactions &>(obj));
      return true;
    case raw_getTransactionsV2::ID:
      func(static_cast<raw_getTransactionsV2 &>(obj));
      return true;
    case raw_sendMessage::ID:
      func(static_cast<raw_sendMessage &>(obj));
      return true;
    case raw_sendMessageReturnHash::ID:
      func(static_cast<raw_sendMessageReturnHash &>(obj));
      return true;
    case runTests::ID:
      func(static_cast<runTests &>(obj));
      return true;
    case setLogStream::ID:
      func(static_cast<setLogStream &>(obj));
      return true;
    case setLogTagVerbosityLevel::ID:
      func(static_cast<setLogTagVerbosityLevel &>(obj));
      return true;
    case setLogVerbosityLevel::ID:
      func(static_cast<setLogVerbosityLevel &>(obj));
      return true;
    case smc_forget::ID:
      func(static_cast<smc_forget &>(obj));
      return true;
    case smc_getCode::ID:
      func(static_cast<smc_getCode &>(obj));
      return true;
    case smc_getData::ID:
      func(static_cast<smc_getData &>(obj));
      return true;
    case smc_getLibraries::ID:
      func(static_cast<smc_getLibraries &>(obj));
      return true;
    case smc_getLibrariesExt::ID:
      func(static_cast<smc_getLibrariesExt &>(obj));
      return true;
    case smc_getRawFullAccountState::ID:
      func(static_cast<smc_getRawFullAccountState &>(obj));
      return true;
    case smc_getState::ID:
      func(static_cast<smc_getState &>(obj));
      return true;
    case smc_load::ID:
      func(static_cast<smc_load &>(obj));
      return true;
    case smc_loadByTransaction::ID:
      func(static_cast<smc_loadByTransaction &>(obj));
      return true;
    case smc_runGetMethod::ID:
      func(static_cast<smc_runGetMethod &>(obj));
      return true;
    case sync::ID:
      func(static_cast<sync &>(obj));
      return true;
    case unpackAccountAddress::ID:
      func(static_cast<unpackAccountAddress &>(obj));
      return true;
    case withBlock::ID:
      func(static_cast<withBlock &>(obj));
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
switch (obj.get_id()) {    case addLogMessage::ID:
      func(create_tl_object<addLogMessage>());
      return true;
    case blocks_getBlockHeader::ID:
      func(create_tl_object<blocks_getBlockHeader>());
      return true;
    case blocks_getMasterchainBlockSignatures::ID:
      func(create_tl_object<blocks_getMasterchainBlockSignatures>());
      return true;
    case blocks_getMasterchainInfo::ID:
      func(create_tl_object<blocks_getMasterchainInfo>());
      return true;
    case blocks_getOutMsgQueueSizes::ID:
      func(create_tl_object<blocks_getOutMsgQueueSizes>());
      return true;
    case blocks_getShardBlockProof::ID:
      func(create_tl_object<blocks_getShardBlockProof>());
      return true;
    case blocks_getShards::ID:
      func(create_tl_object<blocks_getShards>());
      return true;
    case blocks_getTransactions::ID:
      func(create_tl_object<blocks_getTransactions>());
      return true;
    case blocks_getTransactionsExt::ID:
      func(create_tl_object<blocks_getTransactionsExt>());
      return true;
    case blocks_lookupBlock::ID:
      func(create_tl_object<blocks_lookupBlock>());
      return true;
    case changeLocalPassword::ID:
      func(create_tl_object<changeLocalPassword>());
      return true;
    case close::ID:
      func(create_tl_object<close>());
      return true;
    case createNewKey::ID:
      func(create_tl_object<createNewKey>());
      return true;
    case createQuery::ID:
      func(create_tl_object<createQuery>());
      return true;
    case decrypt::ID:
      func(create_tl_object<decrypt>());
      return true;
    case deleteAllKeys::ID:
      func(create_tl_object<deleteAllKeys>());
      return true;
    case deleteKey::ID:
      func(create_tl_object<deleteKey>());
      return true;
    case dns_resolve::ID:
      func(create_tl_object<dns_resolve>());
      return true;
    case encrypt::ID:
      func(create_tl_object<encrypt>());
      return true;
    case exportEncryptedKey::ID:
      func(create_tl_object<exportEncryptedKey>());
      return true;
    case exportKey::ID:
      func(create_tl_object<exportKey>());
      return true;
    case exportPemKey::ID:
      func(create_tl_object<exportPemKey>());
      return true;
    case exportUnencryptedKey::ID:
      func(create_tl_object<exportUnencryptedKey>());
      return true;
    case getAccountAddress::ID:
      func(create_tl_object<getAccountAddress>());
      return true;
    case getAccountState::ID:
      func(create_tl_object<getAccountState>());
      return true;
    case getAccountStateByTransaction::ID:
      func(create_tl_object<getAccountStateByTransaction>());
      return true;
    case getBip39Hints::ID:
      func(create_tl_object<getBip39Hints>());
      return true;
    case getConfigAll::ID:
      func(create_tl_object<getConfigAll>());
      return true;
    case getConfigParam::ID:
      func(create_tl_object<getConfigParam>());
      return true;
    case getLogStream::ID:
      func(create_tl_object<getLogStream>());
      return true;
    case getLogTagVerbosityLevel::ID:
      func(create_tl_object<getLogTagVerbosityLevel>());
      return true;
    case getLogTags::ID:
      func(create_tl_object<getLogTags>());
      return true;
    case getLogVerbosityLevel::ID:
      func(create_tl_object<getLogVerbosityLevel>());
      return true;
    case getShardAccountCell::ID:
      func(create_tl_object<getShardAccountCell>());
      return true;
    case getShardAccountCellByTransaction::ID:
      func(create_tl_object<getShardAccountCellByTransaction>());
      return true;
    case guessAccount::ID:
      func(create_tl_object<guessAccount>());
      return true;
    case guessAccountRevision::ID:
      func(create_tl_object<guessAccountRevision>());
      return true;
    case importEncryptedKey::ID:
      func(create_tl_object<importEncryptedKey>());
      return true;
    case importKey::ID:
      func(create_tl_object<importKey>());
      return true;
    case importPemKey::ID:
      func(create_tl_object<importPemKey>());
      return true;
    case importUnencryptedKey::ID:
      func(create_tl_object<importUnencryptedKey>());
      return true;
    case init::ID:
      func(create_tl_object<init>());
      return true;
    case kdf::ID:
      func(create_tl_object<kdf>());
      return true;
    case liteServer_getInfo::ID:
      func(create_tl_object<liteServer_getInfo>());
      return true;
    case msg_decrypt::ID:
      func(create_tl_object<msg_decrypt>());
      return true;
    case msg_decryptWithProof::ID:
      func(create_tl_object<msg_decryptWithProof>());
      return true;
    case onLiteServerQueryError::ID:
      func(create_tl_object<onLiteServerQueryError>());
      return true;
    case onLiteServerQueryResult::ID:
      func(create_tl_object<onLiteServerQueryResult>());
      return true;
    case options_setConfig::ID:
      func(create_tl_object<options_setConfig>());
      return true;
    case options_validateConfig::ID:
      func(create_tl_object<options_validateConfig>());
      return true;
    case packAccountAddress::ID:
      func(create_tl_object<packAccountAddress>());
      return true;
    case pchan_packPromise::ID:
      func(create_tl_object<pchan_packPromise>());
      return true;
    case pchan_signPromise::ID:
      func(create_tl_object<pchan_signPromise>());
      return true;
    case pchan_unpackPromise::ID:
      func(create_tl_object<pchan_unpackPromise>());
      return true;
    case pchan_validatePromise::ID:
      func(create_tl_object<pchan_validatePromise>());
      return true;
    case query_estimateFees::ID:
      func(create_tl_object<query_estimateFees>());
      return true;
    case query_forget::ID:
      func(create_tl_object<query_forget>());
      return true;
    case query_getInfo::ID:
      func(create_tl_object<query_getInfo>());
      return true;
    case query_send::ID:
      func(create_tl_object<query_send>());
      return true;
    case raw_createAndSendMessage::ID:
      func(create_tl_object<raw_createAndSendMessage>());
      return true;
    case raw_createQuery::ID:
      func(create_tl_object<raw_createQuery>());
      return true;
    case raw_getAccountState::ID:
      func(create_tl_object<raw_getAccountState>());
      return true;
    case raw_getAccountStateByTransaction::ID:
      func(create_tl_object<raw_getAccountStateByTransaction>());
      return true;
    case raw_getTransactions::ID:
      func(create_tl_object<raw_getTransactions>());
      return true;
    case raw_getTransactionsV2::ID:
      func(create_tl_object<raw_getTransactionsV2>());
      return true;
    case raw_sendMessage::ID:
      func(create_tl_object<raw_sendMessage>());
      return true;
    case raw_sendMessageReturnHash::ID:
      func(create_tl_object<raw_sendMessageReturnHash>());
      return true;
    case runTests::ID:
      func(create_tl_object<runTests>());
      return true;
    case setLogStream::ID:
      func(create_tl_object<setLogStream>());
      return true;
    case setLogTagVerbosityLevel::ID:
      func(create_tl_object<setLogTagVerbosityLevel>());
      return true;
    case setLogVerbosityLevel::ID:
      func(create_tl_object<setLogVerbosityLevel>());
      return true;
    case smc_forget::ID:
      func(create_tl_object<smc_forget>());
      return true;
    case smc_getCode::ID:
      func(create_tl_object<smc_getCode>());
      return true;
    case smc_getData::ID:
      func(create_tl_object<smc_getData>());
      return true;
    case smc_getLibraries::ID:
      func(create_tl_object<smc_getLibraries>());
      return true;
    case smc_getLibrariesExt::ID:
      func(create_tl_object<smc_getLibrariesExt>());
      return true;
    case smc_getRawFullAccountState::ID:
      func(create_tl_object<smc_getRawFullAccountState>());
      return true;
    case smc_getState::ID:
      func(create_tl_object<smc_getState>());
      return true;
    case smc_load::ID:
      func(create_tl_object<smc_load>());
      return true;
    case smc_loadByTransaction::ID:
      func(create_tl_object<smc_loadByTransaction>());
      return true;
    case smc_runGetMethod::ID:
      func(create_tl_object<smc_runGetMethod>());
      return true;
    case sync::ID:
      func(create_tl_object<sync>());
      return true;
    case unpackAccountAddress::ID:
      func(create_tl_object<unpackAccountAddress>());
      return true;
    case withBlock::ID:
      func(create_tl_object<withBlock>());
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
bool downcast_call(AccountState &obj, const T &func) {
  switch (obj.get_id()) {
    case raw_accountState::ID:
      func(static_cast<raw_accountState &>(obj));
      return true;
    case wallet_v3_accountState::ID:
      func(static_cast<wallet_v3_accountState &>(obj));
      return true;
    case wallet_v4_accountState::ID:
      func(static_cast<wallet_v4_accountState &>(obj));
      return true;
    case wallet_highload_v1_accountState::ID:
      func(static_cast<wallet_highload_v1_accountState &>(obj));
      return true;
    case wallet_highload_v2_accountState::ID:
      func(static_cast<wallet_highload_v2_accountState &>(obj));
      return true;
    case dns_accountState::ID:
      func(static_cast<dns_accountState &>(obj));
      return true;
    case rwallet_accountState::ID:
      func(static_cast<rwallet_accountState &>(obj));
      return true;
    case pchan_accountState::ID:
      func(static_cast<pchan_accountState &>(obj));
      return true;
    case uninited_accountState::ID:
      func(static_cast<uninited_accountState &>(obj));
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
bool downcast_construct(AccountState &obj, const T &func) {
switch (obj.get_id()) {    case raw_accountState::ID:
      func(create_tl_object<raw_accountState>());
      return true;
    case wallet_v3_accountState::ID:
      func(create_tl_object<wallet_v3_accountState>());
      return true;
    case wallet_v4_accountState::ID:
      func(create_tl_object<wallet_v4_accountState>());
      return true;
    case wallet_highload_v1_accountState::ID:
      func(create_tl_object<wallet_highload_v1_accountState>());
      return true;
    case wallet_highload_v2_accountState::ID:
      func(create_tl_object<wallet_highload_v2_accountState>());
      return true;
    case dns_accountState::ID:
      func(create_tl_object<dns_accountState>());
      return true;
    case rwallet_accountState::ID:
      func(create_tl_object<rwallet_accountState>());
      return true;
    case pchan_accountState::ID:
      func(create_tl_object<pchan_accountState>());
      return true;
    case uninited_accountState::ID:
      func(create_tl_object<uninited_accountState>());
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
bool downcast_call(Action &obj, const T &func) {
  switch (obj.get_id()) {
    case actionNoop::ID:
      func(static_cast<actionNoop &>(obj));
      return true;
    case actionMsg::ID:
      func(static_cast<actionMsg &>(obj));
      return true;
    case actionDns::ID:
      func(static_cast<actionDns &>(obj));
      return true;
    case actionPchan::ID:
      func(static_cast<actionPchan &>(obj));
      return true;
    case actionRwallet::ID:
      func(static_cast<actionRwallet &>(obj));
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
bool downcast_construct(Action &obj, const T &func) {
switch (obj.get_id()) {    case actionNoop::ID:
      func(create_tl_object<actionNoop>());
      return true;
    case actionMsg::ID:
      func(create_tl_object<actionMsg>());
      return true;
    case actionDns::ID:
      func(create_tl_object<actionDns>());
      return true;
    case actionPchan::ID:
      func(create_tl_object<actionPchan>());
      return true;
    case actionRwallet::ID:
      func(create_tl_object<actionRwallet>());
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
bool downcast_call(InitialAccountState &obj, const T &func) {
  switch (obj.get_id()) {
    case raw_initialAccountState::ID:
      func(static_cast<raw_initialAccountState &>(obj));
      return true;
    case wallet_v3_initialAccountState::ID:
      func(static_cast<wallet_v3_initialAccountState &>(obj));
      return true;
    case wallet_v4_initialAccountState::ID:
      func(static_cast<wallet_v4_initialAccountState &>(obj));
      return true;
    case wallet_highload_v1_initialAccountState::ID:
      func(static_cast<wallet_highload_v1_initialAccountState &>(obj));
      return true;
    case wallet_highload_v2_initialAccountState::ID:
      func(static_cast<wallet_highload_v2_initialAccountState &>(obj));
      return true;
    case rwallet_initialAccountState::ID:
      func(static_cast<rwallet_initialAccountState &>(obj));
      return true;
    case dns_initialAccountState::ID:
      func(static_cast<dns_initialAccountState &>(obj));
      return true;
    case pchan_initialAccountState::ID:
      func(static_cast<pchan_initialAccountState &>(obj));
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
bool downcast_construct(InitialAccountState &obj, const T &func) {
switch (obj.get_id()) {    case raw_initialAccountState::ID:
      func(create_tl_object<raw_initialAccountState>());
      return true;
    case wallet_v3_initialAccountState::ID:
      func(create_tl_object<wallet_v3_initialAccountState>());
      return true;
    case wallet_v4_initialAccountState::ID:
      func(create_tl_object<wallet_v4_initialAccountState>());
      return true;
    case wallet_highload_v1_initialAccountState::ID:
      func(create_tl_object<wallet_highload_v1_initialAccountState>());
      return true;
    case wallet_highload_v2_initialAccountState::ID:
      func(create_tl_object<wallet_highload_v2_initialAccountState>());
      return true;
    case rwallet_initialAccountState::ID:
      func(create_tl_object<rwallet_initialAccountState>());
      return true;
    case dns_initialAccountState::ID:
      func(create_tl_object<dns_initialAccountState>());
      return true;
    case pchan_initialAccountState::ID:
      func(create_tl_object<pchan_initialAccountState>());
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
bool downcast_call(InputKey &obj, const T &func) {
  switch (obj.get_id()) {
    case inputKeyRegular::ID:
      func(static_cast<inputKeyRegular &>(obj));
      return true;
    case inputKeyFake::ID:
      func(static_cast<inputKeyFake &>(obj));
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
bool downcast_construct(InputKey &obj, const T &func) {
switch (obj.get_id()) {    case inputKeyRegular::ID:
      func(create_tl_object<inputKeyRegular>());
      return true;
    case inputKeyFake::ID:
      func(create_tl_object<inputKeyFake>());
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
bool downcast_call(KeyStoreType &obj, const T &func) {
  switch (obj.get_id()) {
    case keyStoreTypeDirectory::ID:
      func(static_cast<keyStoreTypeDirectory &>(obj));
      return true;
    case keyStoreTypeInMemory::ID:
      func(static_cast<keyStoreTypeInMemory &>(obj));
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
bool downcast_construct(KeyStoreType &obj, const T &func) {
switch (obj.get_id()) {    case keyStoreTypeDirectory::ID:
      func(create_tl_object<keyStoreTypeDirectory>());
      return true;
    case keyStoreTypeInMemory::ID:
      func(create_tl_object<keyStoreTypeInMemory>());
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
bool downcast_call(LogStream &obj, const T &func) {
  switch (obj.get_id()) {
    case logStreamDefault::ID:
      func(static_cast<logStreamDefault &>(obj));
      return true;
    case logStreamFile::ID:
      func(static_cast<logStreamFile &>(obj));
      return true;
    case logStreamEmpty::ID:
      func(static_cast<logStreamEmpty &>(obj));
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
bool downcast_construct(LogStream &obj, const T &func) {
switch (obj.get_id()) {    case logStreamDefault::ID:
      func(create_tl_object<logStreamDefault>());
      return true;
    case logStreamFile::ID:
      func(create_tl_object<logStreamFile>());
      return true;
    case logStreamEmpty::ID:
      func(create_tl_object<logStreamEmpty>());
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
bool downcast_call(SyncState &obj, const T &func) {
  switch (obj.get_id()) {
    case syncStateDone::ID:
      func(static_cast<syncStateDone &>(obj));
      return true;
    case syncStateInProgress::ID:
      func(static_cast<syncStateInProgress &>(obj));
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
bool downcast_construct(SyncState &obj, const T &func) {
switch (obj.get_id()) {    case syncStateDone::ID:
      func(create_tl_object<syncStateDone>());
      return true;
    case syncStateInProgress::ID:
      func(create_tl_object<syncStateInProgress>());
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
bool downcast_call(Update &obj, const T &func) {
  switch (obj.get_id()) {
    case updateSendLiteServerQuery::ID:
      func(static_cast<updateSendLiteServerQuery &>(obj));
      return true;
    case updateSyncState::ID:
      func(static_cast<updateSyncState &>(obj));
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
bool downcast_construct(Update &obj, const T &func) {
switch (obj.get_id()) {    case updateSendLiteServerQuery::ID:
      func(create_tl_object<updateSendLiteServerQuery>());
      return true;
    case updateSyncState::ID:
      func(create_tl_object<updateSyncState>());
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
bool downcast_call(dns_Action &obj, const T &func) {
  switch (obj.get_id()) {
    case dns_actionDeleteAll::ID:
      func(static_cast<dns_actionDeleteAll &>(obj));
      return true;
    case dns_actionDelete::ID:
      func(static_cast<dns_actionDelete &>(obj));
      return true;
    case dns_actionSet::ID:
      func(static_cast<dns_actionSet &>(obj));
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
bool downcast_construct(dns_Action &obj, const T &func) {
switch (obj.get_id()) {    case dns_actionDeleteAll::ID:
      func(create_tl_object<dns_actionDeleteAll>());
      return true;
    case dns_actionDelete::ID:
      func(create_tl_object<dns_actionDelete>());
      return true;
    case dns_actionSet::ID:
      func(create_tl_object<dns_actionSet>());
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
bool downcast_call(dns_EntryData &obj, const T &func) {
  switch (obj.get_id()) {
    case dns_entryDataUnknown::ID:
      func(static_cast<dns_entryDataUnknown &>(obj));
      return true;
    case dns_entryDataText::ID:
      func(static_cast<dns_entryDataText &>(obj));
      return true;
    case dns_entryDataNextResolver::ID:
      func(static_cast<dns_entryDataNextResolver &>(obj));
      return true;
    case dns_entryDataSmcAddress::ID:
      func(static_cast<dns_entryDataSmcAddress &>(obj));
      return true;
    case dns_entryDataAdnlAddress::ID:
      func(static_cast<dns_entryDataAdnlAddress &>(obj));
      return true;
    case dns_entryDataStorageAddress::ID:
      func(static_cast<dns_entryDataStorageAddress &>(obj));
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
bool downcast_construct(dns_EntryData &obj, const T &func) {
switch (obj.get_id()) {    case dns_entryDataUnknown::ID:
      func(create_tl_object<dns_entryDataUnknown>());
      return true;
    case dns_entryDataText::ID:
      func(create_tl_object<dns_entryDataText>());
      return true;
    case dns_entryDataNextResolver::ID:
      func(create_tl_object<dns_entryDataNextResolver>());
      return true;
    case dns_entryDataSmcAddress::ID:
      func(create_tl_object<dns_entryDataSmcAddress>());
      return true;
    case dns_entryDataAdnlAddress::ID:
      func(create_tl_object<dns_entryDataAdnlAddress>());
      return true;
    case dns_entryDataStorageAddress::ID:
      func(create_tl_object<dns_entryDataStorageAddress>());
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
bool downcast_call(msg_Data &obj, const T &func) {
  switch (obj.get_id()) {
    case msg_dataRaw::ID:
      func(static_cast<msg_dataRaw &>(obj));
      return true;
    case msg_dataText::ID:
      func(static_cast<msg_dataText &>(obj));
      return true;
    case msg_dataDecryptedText::ID:
      func(static_cast<msg_dataDecryptedText &>(obj));
      return true;
    case msg_dataEncryptedText::ID:
      func(static_cast<msg_dataEncryptedText &>(obj));
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
bool downcast_construct(msg_Data &obj, const T &func) {
switch (obj.get_id()) {    case msg_dataRaw::ID:
      func(create_tl_object<msg_dataRaw>());
      return true;
    case msg_dataText::ID:
      func(create_tl_object<msg_dataText>());
      return true;
    case msg_dataDecryptedText::ID:
      func(create_tl_object<msg_dataDecryptedText>());
      return true;
    case msg_dataEncryptedText::ID:
      func(create_tl_object<msg_dataEncryptedText>());
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
bool downcast_call(pchan_Action &obj, const T &func) {
  switch (obj.get_id()) {
    case pchan_actionInit::ID:
      func(static_cast<pchan_actionInit &>(obj));
      return true;
    case pchan_actionClose::ID:
      func(static_cast<pchan_actionClose &>(obj));
      return true;
    case pchan_actionTimeout::ID:
      func(static_cast<pchan_actionTimeout &>(obj));
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
bool downcast_construct(pchan_Action &obj, const T &func) {
switch (obj.get_id()) {    case pchan_actionInit::ID:
      func(create_tl_object<pchan_actionInit>());
      return true;
    case pchan_actionClose::ID:
      func(create_tl_object<pchan_actionClose>());
      return true;
    case pchan_actionTimeout::ID:
      func(create_tl_object<pchan_actionTimeout>());
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
bool downcast_call(pchan_State &obj, const T &func) {
  switch (obj.get_id()) {
    case pchan_stateInit::ID:
      func(static_cast<pchan_stateInit &>(obj));
      return true;
    case pchan_stateClose::ID:
      func(static_cast<pchan_stateClose &>(obj));
      return true;
    case pchan_statePayout::ID:
      func(static_cast<pchan_statePayout &>(obj));
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
bool downcast_construct(pchan_State &obj, const T &func) {
switch (obj.get_id()) {    case pchan_stateInit::ID:
      func(create_tl_object<pchan_stateInit>());
      return true;
    case pchan_stateClose::ID:
      func(create_tl_object<pchan_stateClose>());
      return true;
    case pchan_statePayout::ID:
      func(create_tl_object<pchan_statePayout>());
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
bool downcast_call(smc_LibraryQueryExt &obj, const T &func) {
  switch (obj.get_id()) {
    case smc_libraryQueryExt_one::ID:
      func(static_cast<smc_libraryQueryExt_one &>(obj));
      return true;
    case smc_libraryQueryExt_scanBoc::ID:
      func(static_cast<smc_libraryQueryExt_scanBoc &>(obj));
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
bool downcast_construct(smc_LibraryQueryExt &obj, const T &func) {
switch (obj.get_id()) {    case smc_libraryQueryExt_one::ID:
      func(create_tl_object<smc_libraryQueryExt_one>());
      return true;
    case smc_libraryQueryExt_scanBoc::ID:
      func(create_tl_object<smc_libraryQueryExt_scanBoc>());
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
bool downcast_call(smc_MethodId &obj, const T &func) {
  switch (obj.get_id()) {
    case smc_methodIdNumber::ID:
      func(static_cast<smc_methodIdNumber &>(obj));
      return true;
    case smc_methodIdName::ID:
      func(static_cast<smc_methodIdName &>(obj));
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
bool downcast_construct(smc_MethodId &obj, const T &func) {
switch (obj.get_id()) {    case smc_methodIdNumber::ID:
      func(create_tl_object<smc_methodIdNumber>());
      return true;
    case smc_methodIdName::ID:
      func(create_tl_object<smc_methodIdName>());
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
bool downcast_call(tvm_StackEntry &obj, const T &func) {
  switch (obj.get_id()) {
    case tvm_stackEntrySlice::ID:
      func(static_cast<tvm_stackEntrySlice &>(obj));
      return true;
    case tvm_stackEntryCell::ID:
      func(static_cast<tvm_stackEntryCell &>(obj));
      return true;
    case tvm_stackEntryNumber::ID:
      func(static_cast<tvm_stackEntryNumber &>(obj));
      return true;
    case tvm_stackEntryTuple::ID:
      func(static_cast<tvm_stackEntryTuple &>(obj));
      return true;
    case tvm_stackEntryList::ID:
      func(static_cast<tvm_stackEntryList &>(obj));
      return true;
    case tvm_stackEntryUnsupported::ID:
      func(static_cast<tvm_stackEntryUnsupported &>(obj));
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
bool downcast_construct(tvm_StackEntry &obj, const T &func) {
switch (obj.get_id()) {    case tvm_stackEntrySlice::ID:
      func(create_tl_object<tvm_stackEntrySlice>());
      return true;
    case tvm_stackEntryCell::ID:
      func(create_tl_object<tvm_stackEntryCell>());
      return true;
    case tvm_stackEntryNumber::ID:
      func(create_tl_object<tvm_stackEntryNumber>());
      return true;
    case tvm_stackEntryTuple::ID:
      func(create_tl_object<tvm_stackEntryTuple>());
      return true;
    case tvm_stackEntryList::ID:
      func(create_tl_object<tvm_stackEntryList>());
      return true;
    case tvm_stackEntryUnsupported::ID:
      func(create_tl_object<tvm_stackEntryUnsupported>());
      return true;
    default:
      return false;
  }
}

}  // namespace tonlib_api
}  // namespace ton 
