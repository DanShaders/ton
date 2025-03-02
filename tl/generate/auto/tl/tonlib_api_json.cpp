#include "auto/tl/tonlib_api_json.h"

#include "auto/tl/tonlib_api.h"
#include "auto/tl/tonlib_api.hpp"

#include "tl/tl_json.h"

#include "td/utils/base64.h"
#include "td/utils/common.h"
#include "td/utils/Slice.h"

#include <functional>
#include <unordered_map>

namespace ton {
namespace tonlib_api{
  using namespace td;
Result<int32> tl_constructor_from_string(tonlib_api::AccountState *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"raw.accountState", -531917254},
    {"wallet.v3.accountState", -1619351478},
    {"wallet.v4.accountState", -735144074},
    {"wallet.highload.v1.accountState", 1616372956},
    {"wallet.highload.v2.accountState", -1803723441},
    {"dns.accountState", 1727715434},
    {"rwallet.accountState", -739540008},
    {"pchan.accountState", 1612869496},
    {"uninited.accountState", 1522374408}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::Action *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"actionNoop", 1135848603},
    {"actionMsg", 246839120},
    {"actionDns", 1193750561},
    {"actionPchan", -1490172447},
    {"actionRwallet", -117295163}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::InitialAccountState *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"raw.initialAccountState", -337945529},
    {"wallet.v3.initialAccountState", -118074048},
    {"wallet.v4.initialAccountState", -789986619},
    {"wallet.highload.v1.initialAccountState", -327901626},
    {"wallet.highload.v2.initialAccountState", 1966373161},
    {"rwallet.initialAccountState", 1169755156},
    {"dns.initialAccountState", 1842062527},
    {"pchan.initialAccountState", -1304552124}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::InputKey *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"inputKeyRegular", -555399522},
    {"inputKeyFake", -1074054722}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::KeyStoreType *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"keyStoreTypeDirectory", -378990038},
    {"keyStoreTypeInMemory", -2106848825}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::LogStream *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"logStreamDefault", 1390581436},
    {"logStreamFile", -1880085930},
    {"logStreamEmpty", -499912244}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::SyncState *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"syncStateDone", 1408448777},
    {"syncStateInProgress", 107726023}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::Update *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"updateSendLiteServerQuery", -1555130916},
    {"updateSyncState", 1204298718}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::dns_Action *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"dns.actionDeleteAll", 1067356318},
    {"dns.actionDelete", 1141342033},
    {"dns.actionSet", -1374965309}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::dns_EntryData *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"dns.entryDataUnknown", -1285893248},
    {"dns.entryDataText", -792485614},
    {"dns.entryDataNextResolver", 330382792},
    {"dns.entryDataSmcAddress", -1759937982},
    {"dns.entryDataAdnlAddress", -1114064368},
    {"dns.entryDataStorageAddress", -1751100388}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::msg_Data *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"msg.dataRaw", -1928962698},
    {"msg.dataText", -341560688},
    {"msg.dataDecryptedText", -1289133895},
    {"msg.dataEncryptedText", -296612902}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::pchan_Action *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"pchan.actionInit", 439088778},
    {"pchan.actionClose", 1671187222},
    {"pchan.actionTimeout", 1998487795}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::pchan_State *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"pchan.stateInit", -1188426504},
    {"pchan.stateClose", 887226867},
    {"pchan.statePayout", 664671303}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::smc_LibraryQueryExt *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"smc.libraryQueryExt.one", 1680141027},
    {"smc.libraryQueryExt.scanBoc", -548100891}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::smc_MethodId *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"smc.methodIdNumber", -1541162500},
    {"smc.methodIdName", -249036908}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::tvm_StackEntry *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"tvm.stackEntrySlice", 1395485477},
    {"tvm.stackEntryCell", 1303473952},
    {"tvm.stackEntryNumber", 1358642622},
    {"tvm.stackEntryTuple", -157391908},
    {"tvm.stackEntryList", -1186714229},
    {"tvm.stackEntryUnsupported", 378880498}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::Object *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"accountAddress", 755613099},
    {"accountList", 2017374805},
    {"accountRevisionList", 527197386},
    {"raw.accountState", -531917254},
    {"wallet.v3.accountState", -1619351478},
    {"wallet.v4.accountState", -735144074},
    {"wallet.highload.v1.accountState", 1616372956},
    {"wallet.highload.v2.accountState", -1803723441},
    {"dns.accountState", 1727715434},
    {"rwallet.accountState", -739540008},
    {"pchan.accountState", 1612869496},
    {"uninited.accountState", 1522374408},
    {"actionNoop", 1135848603},
    {"actionMsg", 246839120},
    {"actionDns", 1193750561},
    {"actionPchan", -1490172447},
    {"actionRwallet", -117295163},
    {"adnlAddress", 70358284},
    {"bip39Hints", 1012243456},
    {"config", -1538391496},
    {"configInfo", 687887871},
    {"data", -414733967},
    {"error", -1679978726},
    {"exportedEncryptedKey", 2024406612},
    {"exportedKey", -1449248297},
    {"exportedPemKey", 1425473725},
    {"exportedUnencryptedKey", 730045160},
    {"extraCurrency", 347022801},
    {"fees", 1676273340},
    {"fullAccountState", -676896151},
    {"raw.initialAccountState", -337945529},
    {"wallet.v3.initialAccountState", -118074048},
    {"wallet.v4.initialAccountState", -789986619},
    {"wallet.highload.v1.initialAccountState", -327901626},
    {"wallet.highload.v2.initialAccountState", 1966373161},
    {"rwallet.initialAccountState", 1169755156},
    {"dns.initialAccountState", 1842062527},
    {"pchan.initialAccountState", -1304552124},
    {"inputKeyRegular", -555399522},
    {"inputKeyFake", -1074054722},
    {"key", -1978362923},
    {"keyStoreTypeDirectory", -378990038},
    {"keyStoreTypeInMemory", -2106848825},
    {"logStreamDefault", 1390581436},
    {"logStreamFile", -1880085930},
    {"logStreamEmpty", -499912244},
    {"logTags", -1604930601},
    {"logVerbosityLevel", 1734624234},
    {"ok", -722616727},
    {"options", -1924388359},
    {"syncStateDone", 1408448777},
    {"syncStateInProgress", 107726023},
    {"unpackedAccountAddress", 1892946998},
    {"updateSendLiteServerQuery", -1555130916},
    {"updateSyncState", 1204298718},
    {"blocks.accountTransactionId", -872996220},
    {"blocks.blockLinkBack", 1099726901},
    {"blocks.blockSignatures", -402531429},
    {"blocks.header", 1479116386},
    {"blocks.masterchainInfo", 835339083},
    {"blocks.outMsgQueueSize", 1650494287},
    {"blocks.outMsgQueueSizes", -586969790},
    {"blocks.shardBlockLink", -1495263895},
    {"blocks.shardBlockProof", -69836973},
    {"blocks.shards", 2069473610},
    {"blocks.signature", -1223522111},
    {"blocks.transactions", -1922305900},
    {"blocks.transactionsExt", 1338592797},
    {"dns.actionDeleteAll", 1067356318},
    {"dns.actionDelete", 1141342033},
    {"dns.actionSet", -1374965309},
    {"dns.entry", 505104294},
    {"dns.entryDataUnknown", -1285893248},
    {"dns.entryDataText", -792485614},
    {"dns.entryDataNextResolver", 330382792},
    {"dns.entryDataSmcAddress", -1759937982},
    {"dns.entryDataAdnlAddress", -1114064368},
    {"dns.entryDataStorageAddress", -1751100388},
    {"dns.resolved", 2090272150},
    {"ton.blockId", -1185382494},
    {"internal.transactionId", -989527262},
    {"liteServer.info", -1250165133},
    {"blocks.shortTxId", 487976773},
    {"msg.dataRaw", -1928962698},
    {"msg.dataText", -341560688},
    {"msg.dataDecryptedText", -1289133895},
    {"msg.dataEncryptedText", -296612902},
    {"msg.dataDecrypted", 195649769},
    {"msg.dataDecryptedArray", -480491767},
    {"msg.dataEncrypted", 564215121},
    {"msg.dataEncryptedArray", 608655794},
    {"msg.message", -1652961002},
    {"options.configInfo", 129457942},
    {"options.info", -64676736},
    {"pchan.actionInit", 439088778},
    {"pchan.actionClose", 1671187222},
    {"pchan.actionTimeout", 1998487795},
    {"pchan.config", -2071530442},
    {"pchan.promise", -1576102819},
    {"pchan.stateInit", -1188426504},
    {"pchan.stateClose", 887226867},
    {"pchan.statePayout", 664671303},
    {"query.fees", 1614616510},
    {"query.info", 1451875440},
    {"raw.extMessageInfo", 874086318},
    {"raw.fullAccountState", -1002565503},
    {"raw.message", -399085505},
    {"raw.transaction", -1905746086},
    {"raw.transactions", -2063931155},
    {"rwallet.actionInit", 624147819},
    {"rwallet.config", -85490534},
    {"rwallet.limit", 1222571646},
    {"smc.info", 1134270012},
    {"smc.libraryEntry", -1546268148},
    {"smc.libraryQueryExt.one", 1680141027},
    {"smc.libraryQueryExt.scanBoc", -548100891},
    {"smc.libraryResult", 203930622},
    {"smc.libraryResultExt", -1260458638},
    {"smc.methodIdNumber", -1541162500},
    {"smc.methodIdName", -249036908},
    {"smc.runResult", 1413805043},
    {"ton.blockIdExt", 2031156378},
    {"tvm.cell", -413424735},
    {"tvm.list", 1270320392},
    {"tvm.numberDecimal", 1172477619},
    {"tvm.slice", 537299687},
    {"tvm.stackEntrySlice", 1395485477},
    {"tvm.stackEntryCell", 1303473952},
    {"tvm.stackEntryNumber", 1358642622},
    {"tvm.stackEntryTuple", -157391908},
    {"tvm.stackEntryList", -1186714229},
    {"tvm.stackEntryUnsupported", 378880498},
    {"tvm.tuple", -1363953053}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Result<int32> tl_constructor_from_string(tonlib_api::Function *object, const std::string &str) {
  static const std::unordered_map<Slice, int32, SliceHash> m = {
    {"addLogMessage", 1597427692},
    {"blocks.getBlockHeader", 1915102018},
    {"blocks.getMasterchainBlockSignatures", 1616913876},
    {"blocks.getMasterchainInfo", -45493615},
    {"blocks.getOutMsgQueueSizes", -1187352660},
    {"blocks.getShardBlockProof", 435003111},
    {"blocks.getShards", 2072773677},
    {"blocks.getTransactions", -896152271},
    {"blocks.getTransactionsExt", -1527774002},
    {"blocks.lookupBlock", 1418484659},
    {"changeLocalPassword", -401590337},
    {"close", -1187782273},
    {"createNewKey", -1861385712},
    {"createQuery", -242540347},
    {"decrypt", 357991854},
    {"deleteAllKeys", 1608776483},
    {"deleteKey", -1579595571},
    {"dns.resolve", 1791399222},
    {"encrypt", -1821422820},
    {"exportEncryptedKey", 218237311},
    {"exportKey", -1622353549},
    {"exportPemKey", -643259462},
    {"exportUnencryptedKey", -634665152},
    {"getAccountAddress", 512468424},
    {"getAccountState", -2116357050},
    {"getAccountStateByTransaction", 756813774},
    {"getBip39Hints", -1889640982},
    {"getConfigAll", 484522064},
    {"getConfigParam", -234421517},
    {"getLogStream", 1167608667},
    {"getLogTagVerbosityLevel", 951004547},
    {"getLogTags", -254449190},
    {"getLogVerbosityLevel", 594057956},
    {"getShardAccountCell", -1734437861},
    {"getShardAccountCellByTransaction", 406971329},
    {"guessAccount", -1737659296},
    {"guessAccountRevision", 1857589922},
    {"importEncryptedKey", 656724958},
    {"importKey", -1607900903},
    {"importPemKey", 76385617},
    {"importUnencryptedKey", -1184671467},
    {"init", -1000594762},
    {"kdf", -1667861635},
    {"liteServer.getInfo", 1435327470},
    {"msg.decrypt", 223596297},
    {"msg.decryptWithProof", -2111649663},
    {"onLiteServerQueryError", -677427533},
    {"onLiteServerQueryResult", 2056444510},
    {"options.setConfig", 1870064579},
    {"options.validateConfig", -346965447},
    {"packAccountAddress", -1388561940},
    {"pchan.packPromise", -851703103},
    {"pchan.signPromise", 1814322974},
    {"pchan.unpackPromise", -1250106157},
    {"pchan.validatePromise", 258262242},
    {"query.estimateFees", -957002175},
    {"query.forget", -1211985313},
    {"query.getInfo", -799333669},
    {"query.send", 925242739},
    {"raw.createAndSendMessage", -772224603},
    {"raw.createQuery", -1928557909},
    {"raw.getAccountState", -1327847118},
    {"raw.getAccountStateByTransaction", 714199203},
    {"raw.getTransactions", 1029612317},
    {"raw.getTransactionsV2", -566264666},
    {"raw.sendMessage", -1789427488},
    {"raw.sendMessageReturnHash", -1228445927},
    {"runTests", -2039925427},
    {"setLogStream", -1364199535},
    {"setLogTagVerbosityLevel", -2095589738},
    {"setLogVerbosityLevel", -303429678},
    {"smc.forget", 911028710},
    {"smc.getCode", -2115626088},
    {"smc.getData", -427601079},
    {"smc.getLibraries", 814345749},
    {"smc.getLibrariesExt", 931219762},
    {"smc.getRawFullAccountState", -672334072},
    {"smc.getState", -214390293},
    {"smc.load", -903491521},
    {"smc.loadByTransaction", -2019210447},
    {"smc.runGetMethod", -255261270},
    {"sync", -1875977070},
    {"unpackAccountAddress", -682459063},
    {"withBlock", -789093723}
  };
  auto it = m.find(str);
  if (it == m.end()) {
    return Status::Error(PSLICE() << "Unknown class \"" << str << "\"");
  }
  return it->second;
}

Status from_json(tonlib_api::accountAddress &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "account_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.account_address_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::accountList &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "accounts", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.accounts_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::accountRevisionList &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "revisions", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.revisions_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::raw_accountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "code", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.code_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.data_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "frozen_hash", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.frozen_hash_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::wallet_v3_accountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "wallet_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.wallet_id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "seqno", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.seqno_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::wallet_v4_accountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "wallet_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.wallet_id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "seqno", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.seqno_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::wallet_highload_v1_accountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "wallet_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.wallet_id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "seqno", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.seqno_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::wallet_highload_v2_accountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "wallet_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.wallet_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::dns_accountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "wallet_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.wallet_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::rwallet_accountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "wallet_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.wallet_id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "seqno", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.seqno_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "unlocked_balance", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.unlocked_balance_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "config", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.config_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::pchan_accountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "config", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.config_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "state", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.state_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "description", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.description_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::uninited_accountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "frozen_hash", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.frozen_hash_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::actionNoop &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::actionMsg &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "messages", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.messages_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "allow_send_to_uninited", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.allow_send_to_uninited_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::actionDns &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "actions", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.actions_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::actionPchan &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "action", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.action_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::actionRwallet &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "action", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.action_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::adnlAddress &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "adnl_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.adnl_address_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::bip39Hints &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "words", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.words_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::config &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "config", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.config_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "blockchain_name", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.blockchain_name_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "use_callbacks_for_network", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.use_callbacks_for_network_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "ignore_cache", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.ignore_cache_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::configInfo &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "config", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.config_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::data &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "bytes", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.bytes_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::error &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "code", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.code_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "message", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.message_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::exportedEncryptedKey &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.data_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::exportedKey &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "word_list", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.word_list_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::exportedPemKey &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "pem", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.pem_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::exportedUnencryptedKey &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.data_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::extraCurrency &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "amount", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.amount_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::fees &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "in_fwd_fee", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.in_fwd_fee_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "storage_fee", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.storage_fee_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "gas_fee", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.gas_fee_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "fwd_fee", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.fwd_fee_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::fullAccountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.address_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "balance", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.balance_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "extra_currencies", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.extra_currencies_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "last_transaction_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.last_transaction_id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "block_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.block_id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "sync_utime", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.sync_utime_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "account_state", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.account_state_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "revision", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.revision_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::raw_initialAccountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "code", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.code_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.data_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::wallet_v3_initialAccountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "public_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.public_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "wallet_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.wallet_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::wallet_v4_initialAccountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "public_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.public_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "wallet_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.wallet_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::wallet_highload_v1_initialAccountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "public_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.public_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "wallet_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.wallet_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::wallet_highload_v2_initialAccountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "public_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.public_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "wallet_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.wallet_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::rwallet_initialAccountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "init_public_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.init_public_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "public_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.public_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "wallet_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.wallet_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::dns_initialAccountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "public_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.public_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "wallet_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.wallet_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::pchan_initialAccountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "config", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.config_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::inputKeyRegular &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "local_password", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.local_password_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::inputKeyFake &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::key &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "public_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.public_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "secret", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.secret_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::keyStoreTypeDirectory &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "directory", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.directory_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::keyStoreTypeInMemory &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::logStreamDefault &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::logStreamFile &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "path", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.path_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "max_file_size", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.max_file_size_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::logStreamEmpty &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::logTags &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "tags", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.tags_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::logVerbosityLevel &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "verbosity_level", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.verbosity_level_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::ok &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::options &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "config", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.config_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "keystore_type", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.keystore_type_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::syncStateDone &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::syncStateInProgress &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "from_seqno", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.from_seqno_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "to_seqno", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.to_seqno_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "current_seqno", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.current_seqno_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::unpackedAccountAddress &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "workchain_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.workchain_id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "bounceable", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.bounceable_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "testnet", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.testnet_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "addr", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.addr_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::updateSendLiteServerQuery &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.data_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::updateSyncState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "sync_state", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.sync_state_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_accountTransactionId &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "account", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.account_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "lt", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.lt_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_blockLinkBack &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "to_key_block", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.to_key_block_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "from", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.from_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "to", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.to_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "dest_proof", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.dest_proof_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "proof", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.proof_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "state_proof", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.state_proof_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_blockSignatures &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "signatures", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.signatures_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_header &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "global_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.global_id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "version", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.version_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "flags", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.flags_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "after_merge", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.after_merge_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "after_split", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.after_split_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "before_split", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.before_split_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "want_merge", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.want_merge_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "want_split", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.want_split_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "validator_list_hash_short", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.validator_list_hash_short_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "catchain_seqno", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.catchain_seqno_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "min_ref_mc_seqno", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.min_ref_mc_seqno_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "is_key_block", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.is_key_block_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "prev_key_block_seqno", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.prev_key_block_seqno_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "start_lt", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.start_lt_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "end_lt", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.end_lt_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "gen_utime", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.gen_utime_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "vert_seqno", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.vert_seqno_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "prev_blocks", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.prev_blocks_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_masterchainInfo &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "last", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.last_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "state_root_hash", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.state_root_hash_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "init", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.init_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_outMsgQueueSize &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "size", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.size_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_outMsgQueueSizes &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "shards", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.shards_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "ext_msg_queue_size_limit", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.ext_msg_queue_size_limit_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_shardBlockLink &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "proof", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.proof_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_shardBlockProof &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "from", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.from_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "mc_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.mc_id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "links", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.links_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "mc_proof", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.mc_proof_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_shards &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "shards", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.shards_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_signature &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "node_id_short", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.node_id_short_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "signature", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.signature_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_transactions &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "req_count", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.req_count_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "incomplete", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.incomplete_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "transactions", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.transactions_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_transactionsExt &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "req_count", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.req_count_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "incomplete", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.incomplete_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "transactions", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.transactions_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::dns_actionDeleteAll &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::dns_actionDelete &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "name", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.name_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "category", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.category_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::dns_actionSet &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "entry", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.entry_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::dns_entry &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "name", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.name_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "category", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.category_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "entry", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.entry_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::dns_entryDataUnknown &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "bytes", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.bytes_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::dns_entryDataText &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "text", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.text_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::dns_entryDataNextResolver &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "resolver", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.resolver_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::dns_entryDataSmcAddress &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "smc_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.smc_address_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::dns_entryDataAdnlAddress &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "adnl_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.adnl_address_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::dns_entryDataStorageAddress &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "bag_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.bag_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::dns_resolved &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "entries", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.entries_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::ton_blockId &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "workchain", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.workchain_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "shard", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.shard_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "seqno", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.seqno_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::internal_transactionId &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "lt", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.lt_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "hash", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.hash_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::liteServer_info &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "now", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.now_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "version", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.version_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "capabilities", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.capabilities_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_shortTxId &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "mode", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.mode_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "account", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.account_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "lt", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.lt_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "hash", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.hash_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::msg_dataRaw &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "body", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.body_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "init_state", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.init_state_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::msg_dataText &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "text", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.text_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::msg_dataDecryptedText &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "text", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.text_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::msg_dataEncryptedText &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "text", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.text_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::msg_dataDecrypted &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "proof", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.proof_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.data_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::msg_dataDecryptedArray &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "elements", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.elements_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::msg_dataEncrypted &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "source", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.source_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.data_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::msg_dataEncryptedArray &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "elements", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.elements_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::msg_message &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "destination", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.destination_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "public_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.public_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "amount", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.amount_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "extra_currencies", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.extra_currencies_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.data_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "send_mode", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.send_mode_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::options_configInfo &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "default_wallet_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.default_wallet_id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "default_rwallet_init_public_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.default_rwallet_init_public_key_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::options_info &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "config_info", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.config_info_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::pchan_actionInit &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "inc_A", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.inc_A_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "inc_B", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.inc_B_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "min_A", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.min_A_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "min_B", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.min_B_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::pchan_actionClose &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "extra_A", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.extra_A_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "extra_B", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.extra_B_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "promise", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.promise_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::pchan_actionTimeout &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::pchan_config &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "alice_public_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.alice_public_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "alice_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.alice_address_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "bob_public_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.bob_public_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "bob_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.bob_address_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "init_timeout", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.init_timeout_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "close_timeout", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.close_timeout_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "channel_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.channel_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::pchan_promise &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "signature", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.signature_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "promise_A", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.promise_A_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "promise_B", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.promise_B_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "channel_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.channel_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::pchan_stateInit &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "signed_A", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.signed_A_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "signed_B", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.signed_B_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "min_A", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.min_A_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "min_B", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.min_B_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "expire_at", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.expire_at_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "A", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.A_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "B", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.B_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::pchan_stateClose &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "signed_A", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.signed_A_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "signed_B", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.signed_B_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "min_A", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.min_A_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "min_B", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.min_B_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "expire_at", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.expire_at_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "A", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.A_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "B", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.B_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::pchan_statePayout &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "A", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.A_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "B", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.B_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::query_fees &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "source_fees", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.source_fees_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "destination_fees", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.destination_fees_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::query_info &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "valid_until", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.valid_until_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "body_hash", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.body_hash_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "body", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.body_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "init_state", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.init_state_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::raw_extMessageInfo &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "hash", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.hash_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::raw_fullAccountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "balance", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.balance_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "extra_currencies", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.extra_currencies_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "code", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.code_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.data_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "last_transaction_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.last_transaction_id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "block_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.block_id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "frozen_hash", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.frozen_hash_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "sync_utime", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.sync_utime_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::raw_message &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "hash", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.hash_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "source", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.source_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "destination", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.destination_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "value", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.value_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "extra_currencies", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.extra_currencies_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "fwd_fee", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.fwd_fee_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "ihr_fee", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.ihr_fee_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "created_lt", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.created_lt_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "body_hash", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.body_hash_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "msg_data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.msg_data_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::raw_transaction &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.address_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "utime", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.utime_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.data_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "transaction_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.transaction_id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "fee", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.fee_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "storage_fee", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.storage_fee_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "other_fee", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.other_fee_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "in_msg", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.in_msg_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "out_msgs", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.out_msgs_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::raw_transactions &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "transactions", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.transactions_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "previous_transaction_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.previous_transaction_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::rwallet_actionInit &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "config", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.config_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::rwallet_config &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "start_at", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.start_at_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "limits", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.limits_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::rwallet_limit &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "seconds", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.seconds_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "value", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.value_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_info &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_libraryEntry &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "hash", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.hash_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.data_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_libraryQueryExt_one &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "hash", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.hash_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_libraryQueryExt_scanBoc &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "boc", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.boc_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "max_libs", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.max_libs_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_libraryResult &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "result", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.result_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_libraryResultExt &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "dict_boc", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.dict_boc_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "libs_ok", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.libs_ok_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "libs_not_found", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.libs_not_found_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_methodIdNumber &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "number", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.number_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_methodIdName &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "name", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.name_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_runResult &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "gas_used", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.gas_used_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "stack", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.stack_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "exit_code", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.exit_code_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::ton_blockIdExt &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "workchain", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.workchain_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "shard", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.shard_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "seqno", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.seqno_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "root_hash", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.root_hash_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "file_hash", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.file_hash_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::tvm_cell &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "bytes", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.bytes_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::tvm_list &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "elements", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.elements_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::tvm_numberDecimal &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "number", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.number_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::tvm_slice &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "bytes", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.bytes_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::tvm_stackEntrySlice &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "slice", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.slice_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::tvm_stackEntryCell &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "cell", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.cell_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::tvm_stackEntryNumber &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "number", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.number_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::tvm_stackEntryTuple &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "tuple", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.tuple_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::tvm_stackEntryList &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "list", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.list_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::tvm_stackEntryUnsupported &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::tvm_tuple &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "elements", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.elements_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::addLogMessage &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "verbosity_level", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.verbosity_level_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "text", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.text_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_getBlockHeader &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_getMasterchainBlockSignatures &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "seqno", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.seqno_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_getMasterchainInfo &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::blocks_getOutMsgQueueSizes &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "mode", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.mode_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "wc", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.wc_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "shard", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.shard_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_getShardBlockProof &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "mode", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.mode_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "from", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.from_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_getShards &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_getTransactions &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "mode", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.mode_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "count", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.count_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "after", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.after_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_getTransactionsExt &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "mode", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.mode_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "count", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.count_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "after", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.after_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::blocks_lookupBlock &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "mode", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.mode_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "lt", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.lt_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "utime", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.utime_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::changeLocalPassword &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "input_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.input_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "new_local_password", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.new_local_password_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::close &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::createNewKey &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "local_password", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.local_password_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "mnemonic_password", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.mnemonic_password_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "random_extra_seed", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.random_extra_seed_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::createQuery &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "private_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.private_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.address_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "timeout", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.timeout_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "action", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.action_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "initial_account_state", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.initial_account_state_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::decrypt &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "encrypted_data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.encrypted_data_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "secret", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.secret_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::deleteAllKeys &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::deleteKey &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.key_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::dns_resolve &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "account_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.account_address_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "name", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.name_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "category", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.category_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "ttl", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.ttl_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::encrypt &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "decrypted_data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.decrypted_data_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "secret", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.secret_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::exportEncryptedKey &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "input_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.input_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "key_password", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.key_password_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::exportKey &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "input_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.input_key_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::exportPemKey &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "input_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.input_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "key_password", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.key_password_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::exportUnencryptedKey &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "input_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.input_key_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::getAccountAddress &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "initial_account_state", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.initial_account_state_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "revision", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.revision_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "workchain_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.workchain_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::getAccountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "account_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.account_address_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::getAccountStateByTransaction &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "account_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.account_address_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "transaction_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.transaction_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::getBip39Hints &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "prefix", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.prefix_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::getConfigAll &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "mode", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.mode_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::getConfigParam &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "mode", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.mode_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "param", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.param_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::getLogStream &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::getLogTagVerbosityLevel &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "tag", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.tag_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::getLogTags &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::getLogVerbosityLevel &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::getShardAccountCell &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "account_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.account_address_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::getShardAccountCellByTransaction &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "account_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.account_address_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "transaction_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.transaction_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::guessAccount &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "public_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.public_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "rwallet_init_public_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.rwallet_init_public_key_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::guessAccountRevision &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "initial_account_state", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.initial_account_state_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "workchain_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.workchain_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::importEncryptedKey &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "local_password", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.local_password_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "key_password", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.key_password_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "exported_encrypted_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.exported_encrypted_key_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::importKey &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "local_password", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.local_password_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "mnemonic_password", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.mnemonic_password_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "exported_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.exported_key_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::importPemKey &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "local_password", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.local_password_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "key_password", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.key_password_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "exported_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.exported_key_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::importUnencryptedKey &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "local_password", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.local_password_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "exported_unencrypted_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.exported_unencrypted_key_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::init &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "options", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.options_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::kdf &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "password", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.password_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "salt", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.salt_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "iterations", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.iterations_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::liteServer_getInfo &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::msg_decrypt &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "input_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.input_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.data_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::msg_decryptWithProof &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "proof", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.proof_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.data_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::onLiteServerQueryError &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "error", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.error_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::onLiteServerQueryResult &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "bytes", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.bytes_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::options_setConfig &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "config", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.config_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::options_validateConfig &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "config", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.config_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::packAccountAddress &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "account_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.account_address_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::pchan_packPromise &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "promise", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.promise_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::pchan_signPromise &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "input_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.input_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "promise", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.promise_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::pchan_unpackPromise &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.data_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::pchan_validatePromise &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "public_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.public_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "promise", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.promise_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::query_estimateFees &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "ignore_chksig", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.ignore_chksig_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::query_forget &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::query_getInfo &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::query_send &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::raw_createAndSendMessage &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "destination", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.destination_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "initial_account_state", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.initial_account_state_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.data_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::raw_createQuery &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "destination", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.destination_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "init_code", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.init_code_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "init_data", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.init_data_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "body", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.body_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::raw_getAccountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "account_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.account_address_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::raw_getAccountStateByTransaction &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "account_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.account_address_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "transaction_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.transaction_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::raw_getTransactions &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "private_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.private_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "account_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.account_address_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "from_transaction_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.from_transaction_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::raw_getTransactionsV2 &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "private_key", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.private_key_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "account_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.account_address_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "from_transaction_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.from_transaction_id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "count", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.count_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "try_decode_messages", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.try_decode_messages_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::raw_sendMessage &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "body", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.body_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::raw_sendMessageReturnHash &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "body", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json_bytes(to.body_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::runTests &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "dir", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.dir_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::setLogStream &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "log_stream", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.log_stream_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::setLogTagVerbosityLevel &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "tag", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.tag_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "new_verbosity_level", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.new_verbosity_level_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::setLogVerbosityLevel &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "new_verbosity_level", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.new_verbosity_level_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_forget &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_getCode &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_getData &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_getLibraries &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "library_list", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.library_list_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_getLibrariesExt &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "list", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.list_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_getRawFullAccountState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_getState &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_load &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "account_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.account_address_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_loadByTransaction &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "account_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.account_address_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "transaction_id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.transaction_id_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::smc_runGetMethod &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "method", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.method_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "stack", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.stack_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::sync &to, JsonObject &from) {
  return Status::OK();
}
Status from_json(tonlib_api::unpackAccountAddress &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "account_address", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.account_address_, std::move(value)));
    }
  }
  return Status::OK();
}
Status from_json(tonlib_api::withBlock &to, JsonObject &from) {
  {
    TRY_RESULT(value, get_json_object_field(from, "id", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.id_, std::move(value)));
    }
  }
  {
    TRY_RESULT(value, get_json_object_field(from, "function", JsonValue::Type::Null, true));
    if (value.type() != JsonValue::Type::Null) {
      TRY_STATUS(from_json(to.function_, std::move(value)));
    }
  }
  return Status::OK();
}
void to_json(JsonValueScope &jv, const tonlib_api::accountAddress &object) {
  auto jo = jv.enter_object();
  jo("@type", "accountAddress");
  jo("account_address", ToJson(object.account_address_));
}
void to_json(JsonValueScope &jv, const tonlib_api::accountList &object) {
  auto jo = jv.enter_object();
  jo("@type", "accountList");
  jo("accounts", ToJson(object.accounts_));
}
void to_json(JsonValueScope &jv, const tonlib_api::accountRevisionList &object) {
  auto jo = jv.enter_object();
  jo("@type", "accountRevisionList");
  jo("revisions", ToJson(object.revisions_));
}
void to_json(JsonValueScope &jv, const tonlib_api::AccountState &object) {
  tonlib_api::downcast_call(const_cast<tonlib_api::AccountState &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
void to_json(JsonValueScope &jv, const tonlib_api::raw_accountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "raw.accountState");
  jo("code", ToJson(JsonBytes{object.code_}));
  jo("data", ToJson(JsonBytes{object.data_}));
  jo("frozen_hash", ToJson(JsonBytes{object.frozen_hash_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::wallet_v3_accountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "wallet.v3.accountState");
  jo("wallet_id", ToJson(JsonInt64{object.wallet_id_}));
  jo("seqno", ToJson(object.seqno_));
}
void to_json(JsonValueScope &jv, const tonlib_api::wallet_v4_accountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "wallet.v4.accountState");
  jo("wallet_id", ToJson(JsonInt64{object.wallet_id_}));
  jo("seqno", ToJson(object.seqno_));
}
void to_json(JsonValueScope &jv, const tonlib_api::wallet_highload_v1_accountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "wallet.highload.v1.accountState");
  jo("wallet_id", ToJson(JsonInt64{object.wallet_id_}));
  jo("seqno", ToJson(object.seqno_));
}
void to_json(JsonValueScope &jv, const tonlib_api::wallet_highload_v2_accountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "wallet.highload.v2.accountState");
  jo("wallet_id", ToJson(JsonInt64{object.wallet_id_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::dns_accountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "dns.accountState");
  jo("wallet_id", ToJson(JsonInt64{object.wallet_id_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::rwallet_accountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "rwallet.accountState");
  jo("wallet_id", ToJson(JsonInt64{object.wallet_id_}));
  jo("seqno", ToJson(object.seqno_));
  jo("unlocked_balance", ToJson(JsonInt64{object.unlocked_balance_}));
  if (object.config_) {
    jo("config", ToJson(object.config_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::pchan_accountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "pchan.accountState");
  if (object.config_) {
    jo("config", ToJson(object.config_));
  }
  if (object.state_) {
    jo("state", ToJson(object.state_));
  }
  jo("description", ToJson(object.description_));
}
void to_json(JsonValueScope &jv, const tonlib_api::uninited_accountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "uninited.accountState");
  jo("frozen_hash", ToJson(JsonBytes{object.frozen_hash_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::Action &object) {
  tonlib_api::downcast_call(const_cast<tonlib_api::Action &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
void to_json(JsonValueScope &jv, const tonlib_api::actionNoop &object) {
  auto jo = jv.enter_object();
  jo("@type", "actionNoop");
}
void to_json(JsonValueScope &jv, const tonlib_api::actionMsg &object) {
  auto jo = jv.enter_object();
  jo("@type", "actionMsg");
  jo("messages", ToJson(object.messages_));
  jo("allow_send_to_uninited", ToJson(JsonBool{object.allow_send_to_uninited_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::actionDns &object) {
  auto jo = jv.enter_object();
  jo("@type", "actionDns");
  jo("actions", ToJson(object.actions_));
}
void to_json(JsonValueScope &jv, const tonlib_api::actionPchan &object) {
  auto jo = jv.enter_object();
  jo("@type", "actionPchan");
  if (object.action_) {
    jo("action", ToJson(object.action_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::actionRwallet &object) {
  auto jo = jv.enter_object();
  jo("@type", "actionRwallet");
  if (object.action_) {
    jo("action", ToJson(object.action_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::adnlAddress &object) {
  auto jo = jv.enter_object();
  jo("@type", "adnlAddress");
  jo("adnl_address", ToJson(object.adnl_address_));
}
void to_json(JsonValueScope &jv, const tonlib_api::bip39Hints &object) {
  auto jo = jv.enter_object();
  jo("@type", "bip39Hints");
  jo("words", ToJson(object.words_));
}
void to_json(JsonValueScope &jv, const tonlib_api::config &object) {
  auto jo = jv.enter_object();
  jo("@type", "config");
  jo("config", ToJson(object.config_));
  jo("blockchain_name", ToJson(object.blockchain_name_));
  jo("use_callbacks_for_network", ToJson(JsonBool{object.use_callbacks_for_network_}));
  jo("ignore_cache", ToJson(JsonBool{object.ignore_cache_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::configInfo &object) {
  auto jo = jv.enter_object();
  jo("@type", "configInfo");
  if (object.config_) {
    jo("config", ToJson(object.config_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::data &object) {
  auto jo = jv.enter_object();
  jo("@type", "data");
  jo("bytes", ToJson(JsonBytes{object.bytes_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::error &object) {
  auto jo = jv.enter_object();
  jo("@type", "error");
  jo("code", ToJson(object.code_));
  jo("message", ToJson(object.message_));
}
void to_json(JsonValueScope &jv, const tonlib_api::exportedEncryptedKey &object) {
  auto jo = jv.enter_object();
  jo("@type", "exportedEncryptedKey");
  jo("data", ToJson(JsonBytes{object.data_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::exportedKey &object) {
  auto jo = jv.enter_object();
  jo("@type", "exportedKey");
  jo("word_list", ToJson(object.word_list_));
}
void to_json(JsonValueScope &jv, const tonlib_api::exportedPemKey &object) {
  auto jo = jv.enter_object();
  jo("@type", "exportedPemKey");
  jo("pem", ToJson(object.pem_));
}
void to_json(JsonValueScope &jv, const tonlib_api::exportedUnencryptedKey &object) {
  auto jo = jv.enter_object();
  jo("@type", "exportedUnencryptedKey");
  jo("data", ToJson(JsonBytes{object.data_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::extraCurrency &object) {
  auto jo = jv.enter_object();
  jo("@type", "extraCurrency");
  jo("id", ToJson(object.id_));
  jo("amount", ToJson(JsonInt64{object.amount_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::fees &object) {
  auto jo = jv.enter_object();
  jo("@type", "fees");
  jo("in_fwd_fee", ToJson(object.in_fwd_fee_));
  jo("storage_fee", ToJson(object.storage_fee_));
  jo("gas_fee", ToJson(object.gas_fee_));
  jo("fwd_fee", ToJson(object.fwd_fee_));
}
void to_json(JsonValueScope &jv, const tonlib_api::fullAccountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "fullAccountState");
  if (object.address_) {
    jo("address", ToJson(object.address_));
  }
  jo("balance", ToJson(JsonInt64{object.balance_}));
  jo("extra_currencies", ToJson(object.extra_currencies_));
  if (object.last_transaction_id_) {
    jo("last_transaction_id", ToJson(object.last_transaction_id_));
  }
  if (object.block_id_) {
    jo("block_id", ToJson(object.block_id_));
  }
  jo("sync_utime", ToJson(object.sync_utime_));
  if (object.account_state_) {
    jo("account_state", ToJson(object.account_state_));
  }
  jo("revision", ToJson(object.revision_));
}
void to_json(JsonValueScope &jv, const tonlib_api::InitialAccountState &object) {
  tonlib_api::downcast_call(const_cast<tonlib_api::InitialAccountState &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
void to_json(JsonValueScope &jv, const tonlib_api::raw_initialAccountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "raw.initialAccountState");
  jo("code", ToJson(JsonBytes{object.code_}));
  jo("data", ToJson(JsonBytes{object.data_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::wallet_v3_initialAccountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "wallet.v3.initialAccountState");
  jo("public_key", ToJson(object.public_key_));
  jo("wallet_id", ToJson(JsonInt64{object.wallet_id_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::wallet_v4_initialAccountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "wallet.v4.initialAccountState");
  jo("public_key", ToJson(object.public_key_));
  jo("wallet_id", ToJson(JsonInt64{object.wallet_id_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::wallet_highload_v1_initialAccountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "wallet.highload.v1.initialAccountState");
  jo("public_key", ToJson(object.public_key_));
  jo("wallet_id", ToJson(JsonInt64{object.wallet_id_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::wallet_highload_v2_initialAccountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "wallet.highload.v2.initialAccountState");
  jo("public_key", ToJson(object.public_key_));
  jo("wallet_id", ToJson(JsonInt64{object.wallet_id_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::rwallet_initialAccountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "rwallet.initialAccountState");
  jo("init_public_key", ToJson(object.init_public_key_));
  jo("public_key", ToJson(object.public_key_));
  jo("wallet_id", ToJson(JsonInt64{object.wallet_id_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::dns_initialAccountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "dns.initialAccountState");
  jo("public_key", ToJson(object.public_key_));
  jo("wallet_id", ToJson(JsonInt64{object.wallet_id_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::pchan_initialAccountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "pchan.initialAccountState");
  if (object.config_) {
    jo("config", ToJson(object.config_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::InputKey &object) {
  tonlib_api::downcast_call(const_cast<tonlib_api::InputKey &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
void to_json(JsonValueScope &jv, const tonlib_api::inputKeyRegular &object) {
  auto jo = jv.enter_object();
  jo("@type", "inputKeyRegular");
  if (object.key_) {
    jo("key", ToJson(object.key_));
  }
  jo("local_password", ToJson(JsonBytes{object.local_password_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::inputKeyFake &object) {
  auto jo = jv.enter_object();
  jo("@type", "inputKeyFake");
}
void to_json(JsonValueScope &jv, const tonlib_api::key &object) {
  auto jo = jv.enter_object();
  jo("@type", "key");
  jo("public_key", ToJson(object.public_key_));
  jo("secret", ToJson(JsonBytes{object.secret_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::KeyStoreType &object) {
  tonlib_api::downcast_call(const_cast<tonlib_api::KeyStoreType &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
void to_json(JsonValueScope &jv, const tonlib_api::keyStoreTypeDirectory &object) {
  auto jo = jv.enter_object();
  jo("@type", "keyStoreTypeDirectory");
  jo("directory", ToJson(object.directory_));
}
void to_json(JsonValueScope &jv, const tonlib_api::keyStoreTypeInMemory &object) {
  auto jo = jv.enter_object();
  jo("@type", "keyStoreTypeInMemory");
}
void to_json(JsonValueScope &jv, const tonlib_api::LogStream &object) {
  tonlib_api::downcast_call(const_cast<tonlib_api::LogStream &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
void to_json(JsonValueScope &jv, const tonlib_api::logStreamDefault &object) {
  auto jo = jv.enter_object();
  jo("@type", "logStreamDefault");
}
void to_json(JsonValueScope &jv, const tonlib_api::logStreamFile &object) {
  auto jo = jv.enter_object();
  jo("@type", "logStreamFile");
  jo("path", ToJson(object.path_));
  jo("max_file_size", ToJson(object.max_file_size_));
}
void to_json(JsonValueScope &jv, const tonlib_api::logStreamEmpty &object) {
  auto jo = jv.enter_object();
  jo("@type", "logStreamEmpty");
}
void to_json(JsonValueScope &jv, const tonlib_api::logTags &object) {
  auto jo = jv.enter_object();
  jo("@type", "logTags");
  jo("tags", ToJson(object.tags_));
}
void to_json(JsonValueScope &jv, const tonlib_api::logVerbosityLevel &object) {
  auto jo = jv.enter_object();
  jo("@type", "logVerbosityLevel");
  jo("verbosity_level", ToJson(object.verbosity_level_));
}
void to_json(JsonValueScope &jv, const tonlib_api::ok &object) {
  auto jo = jv.enter_object();
  jo("@type", "ok");
}
void to_json(JsonValueScope &jv, const tonlib_api::options &object) {
  auto jo = jv.enter_object();
  jo("@type", "options");
  if (object.config_) {
    jo("config", ToJson(object.config_));
  }
  if (object.keystore_type_) {
    jo("keystore_type", ToJson(object.keystore_type_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::SyncState &object) {
  tonlib_api::downcast_call(const_cast<tonlib_api::SyncState &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
void to_json(JsonValueScope &jv, const tonlib_api::syncStateDone &object) {
  auto jo = jv.enter_object();
  jo("@type", "syncStateDone");
}
void to_json(JsonValueScope &jv, const tonlib_api::syncStateInProgress &object) {
  auto jo = jv.enter_object();
  jo("@type", "syncStateInProgress");
  jo("from_seqno", ToJson(object.from_seqno_));
  jo("to_seqno", ToJson(object.to_seqno_));
  jo("current_seqno", ToJson(object.current_seqno_));
}
void to_json(JsonValueScope &jv, const tonlib_api::unpackedAccountAddress &object) {
  auto jo = jv.enter_object();
  jo("@type", "unpackedAccountAddress");
  jo("workchain_id", ToJson(object.workchain_id_));
  jo("bounceable", ToJson(JsonBool{object.bounceable_}));
  jo("testnet", ToJson(JsonBool{object.testnet_}));
  jo("addr", ToJson(JsonBytes{object.addr_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::Update &object) {
  tonlib_api::downcast_call(const_cast<tonlib_api::Update &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
void to_json(JsonValueScope &jv, const tonlib_api::updateSendLiteServerQuery &object) {
  auto jo = jv.enter_object();
  jo("@type", "updateSendLiteServerQuery");
  jo("id", ToJson(JsonInt64{object.id_}));
  jo("data", ToJson(JsonBytes{object.data_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::updateSyncState &object) {
  auto jo = jv.enter_object();
  jo("@type", "updateSyncState");
  if (object.sync_state_) {
    jo("sync_state", ToJson(object.sync_state_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_accountTransactionId &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.accountTransactionId");
  jo("account", ToJson(JsonBytes{object.account_}));
  jo("lt", ToJson(JsonInt64{object.lt_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_blockLinkBack &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.blockLinkBack");
  jo("to_key_block", ToJson(JsonBool{object.to_key_block_}));
  if (object.from_) {
    jo("from", ToJson(object.from_));
  }
  if (object.to_) {
    jo("to", ToJson(object.to_));
  }
  jo("dest_proof", ToJson(JsonBytes{object.dest_proof_}));
  jo("proof", ToJson(JsonBytes{object.proof_}));
  jo("state_proof", ToJson(JsonBytes{object.state_proof_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_blockSignatures &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.blockSignatures");
  if (object.id_) {
    jo("id", ToJson(object.id_));
  }
  jo("signatures", ToJson(object.signatures_));
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_header &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.header");
  if (object.id_) {
    jo("id", ToJson(object.id_));
  }
  jo("global_id", ToJson(object.global_id_));
  jo("version", ToJson(object.version_));
  if (object.flags_) {
    jo("flags", ToJson(object.flags_));
  }
  jo("after_merge", ToJson(JsonBool{object.after_merge_}));
  jo("after_split", ToJson(JsonBool{object.after_split_}));
  jo("before_split", ToJson(JsonBool{object.before_split_}));
  jo("want_merge", ToJson(JsonBool{object.want_merge_}));
  jo("want_split", ToJson(JsonBool{object.want_split_}));
  jo("validator_list_hash_short", ToJson(object.validator_list_hash_short_));
  jo("catchain_seqno", ToJson(object.catchain_seqno_));
  jo("min_ref_mc_seqno", ToJson(object.min_ref_mc_seqno_));
  jo("is_key_block", ToJson(JsonBool{object.is_key_block_}));
  jo("prev_key_block_seqno", ToJson(object.prev_key_block_seqno_));
  jo("start_lt", ToJson(JsonInt64{object.start_lt_}));
  jo("end_lt", ToJson(JsonInt64{object.end_lt_}));
  jo("gen_utime", ToJson(object.gen_utime_));
  if (object.vert_seqno_) {
    jo("vert_seqno", ToJson(object.vert_seqno_));
  }
  jo("prev_blocks", ToJson(object.prev_blocks_));
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_masterchainInfo &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.masterchainInfo");
  if (object.last_) {
    jo("last", ToJson(object.last_));
  }
  jo("state_root_hash", ToJson(JsonBytes{object.state_root_hash_}));
  if (object.init_) {
    jo("init", ToJson(object.init_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_outMsgQueueSize &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.outMsgQueueSize");
  if (object.id_) {
    jo("id", ToJson(object.id_));
  }
  jo("size", ToJson(object.size_));
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_outMsgQueueSizes &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.outMsgQueueSizes");
  jo("shards", ToJson(object.shards_));
  jo("ext_msg_queue_size_limit", ToJson(object.ext_msg_queue_size_limit_));
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_shardBlockLink &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.shardBlockLink");
  if (object.id_) {
    jo("id", ToJson(object.id_));
  }
  jo("proof", ToJson(JsonBytes{object.proof_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_shardBlockProof &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.shardBlockProof");
  if (object.from_) {
    jo("from", ToJson(object.from_));
  }
  if (object.mc_id_) {
    jo("mc_id", ToJson(object.mc_id_));
  }
  jo("links", ToJson(object.links_));
  jo("mc_proof", ToJson(object.mc_proof_));
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_shards &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.shards");
  jo("shards", ToJson(object.shards_));
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_signature &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.signature");
  jo("node_id_short", ToJson(object.node_id_short_));
  jo("signature", ToJson(JsonBytes{object.signature_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_transactions &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.transactions");
  if (object.id_) {
    jo("id", ToJson(object.id_));
  }
  jo("req_count", ToJson(object.req_count_));
  jo("incomplete", ToJson(JsonBool{object.incomplete_}));
  jo("transactions", ToJson(object.transactions_));
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_transactionsExt &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.transactionsExt");
  if (object.id_) {
    jo("id", ToJson(object.id_));
  }
  jo("req_count", ToJson(object.req_count_));
  jo("incomplete", ToJson(JsonBool{object.incomplete_}));
  jo("transactions", ToJson(object.transactions_));
}
void to_json(JsonValueScope &jv, const tonlib_api::dns_Action &object) {
  tonlib_api::downcast_call(const_cast<tonlib_api::dns_Action &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
void to_json(JsonValueScope &jv, const tonlib_api::dns_actionDeleteAll &object) {
  auto jo = jv.enter_object();
  jo("@type", "dns.actionDeleteAll");
}
void to_json(JsonValueScope &jv, const tonlib_api::dns_actionDelete &object) {
  auto jo = jv.enter_object();
  jo("@type", "dns.actionDelete");
  jo("name", ToJson(object.name_));
  jo("category", ToJson(object.category_));
}
void to_json(JsonValueScope &jv, const tonlib_api::dns_actionSet &object) {
  auto jo = jv.enter_object();
  jo("@type", "dns.actionSet");
  if (object.entry_) {
    jo("entry", ToJson(object.entry_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::dns_entry &object) {
  auto jo = jv.enter_object();
  jo("@type", "dns.entry");
  jo("name", ToJson(object.name_));
  jo("category", ToJson(object.category_));
  if (object.entry_) {
    jo("entry", ToJson(object.entry_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::dns_EntryData &object) {
  tonlib_api::downcast_call(const_cast<tonlib_api::dns_EntryData &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
void to_json(JsonValueScope &jv, const tonlib_api::dns_entryDataUnknown &object) {
  auto jo = jv.enter_object();
  jo("@type", "dns.entryDataUnknown");
  jo("bytes", ToJson(JsonBytes{object.bytes_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::dns_entryDataText &object) {
  auto jo = jv.enter_object();
  jo("@type", "dns.entryDataText");
  jo("text", ToJson(object.text_));
}
void to_json(JsonValueScope &jv, const tonlib_api::dns_entryDataNextResolver &object) {
  auto jo = jv.enter_object();
  jo("@type", "dns.entryDataNextResolver");
  if (object.resolver_) {
    jo("resolver", ToJson(object.resolver_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::dns_entryDataSmcAddress &object) {
  auto jo = jv.enter_object();
  jo("@type", "dns.entryDataSmcAddress");
  if (object.smc_address_) {
    jo("smc_address", ToJson(object.smc_address_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::dns_entryDataAdnlAddress &object) {
  auto jo = jv.enter_object();
  jo("@type", "dns.entryDataAdnlAddress");
  if (object.adnl_address_) {
    jo("adnl_address", ToJson(object.adnl_address_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::dns_entryDataStorageAddress &object) {
  auto jo = jv.enter_object();
  jo("@type", "dns.entryDataStorageAddress");
  jo("bag_id", ToJson(object.bag_id_));
}
void to_json(JsonValueScope &jv, const tonlib_api::dns_resolved &object) {
  auto jo = jv.enter_object();
  jo("@type", "dns.resolved");
  jo("entries", ToJson(object.entries_));
}
void to_json(JsonValueScope &jv, const tonlib_api::ton_blockId &object) {
  auto jo = jv.enter_object();
  jo("@type", "ton.blockId");
  jo("workchain", ToJson(object.workchain_));
  jo("shard", ToJson(JsonInt64{object.shard_}));
  jo("seqno", ToJson(object.seqno_));
}
void to_json(JsonValueScope &jv, const tonlib_api::internal_transactionId &object) {
  auto jo = jv.enter_object();
  jo("@type", "internal.transactionId");
  jo("lt", ToJson(JsonInt64{object.lt_}));
  jo("hash", ToJson(JsonBytes{object.hash_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::liteServer_info &object) {
  auto jo = jv.enter_object();
  jo("@type", "liteServer.info");
  jo("now", ToJson(object.now_));
  jo("version", ToJson(object.version_));
  jo("capabilities", ToJson(JsonInt64{object.capabilities_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_shortTxId &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.shortTxId");
  if (object.mode_) {
    jo("mode", ToJson(object.mode_));
  }
  if ((object.mode_ & 1)) {
    jo("account", ToJson(JsonBytes{object.account_}));
  }
  if ((object.mode_ & 2)) {
    jo("lt", ToJson(JsonInt64{object.lt_}));
  }
  if ((object.mode_ & 4)) {
    jo("hash", ToJson(JsonBytes{object.hash_}));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::msg_Data &object) {
  tonlib_api::downcast_call(const_cast<tonlib_api::msg_Data &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
void to_json(JsonValueScope &jv, const tonlib_api::msg_dataRaw &object) {
  auto jo = jv.enter_object();
  jo("@type", "msg.dataRaw");
  jo("body", ToJson(JsonBytes{object.body_}));
  jo("init_state", ToJson(JsonBytes{object.init_state_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::msg_dataText &object) {
  auto jo = jv.enter_object();
  jo("@type", "msg.dataText");
  jo("text", ToJson(JsonBytes{object.text_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::msg_dataDecryptedText &object) {
  auto jo = jv.enter_object();
  jo("@type", "msg.dataDecryptedText");
  jo("text", ToJson(JsonBytes{object.text_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::msg_dataEncryptedText &object) {
  auto jo = jv.enter_object();
  jo("@type", "msg.dataEncryptedText");
  jo("text", ToJson(JsonBytes{object.text_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::msg_dataDecrypted &object) {
  auto jo = jv.enter_object();
  jo("@type", "msg.dataDecrypted");
  jo("proof", ToJson(JsonBytes{object.proof_}));
  if (object.data_) {
    jo("data", ToJson(object.data_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::msg_dataDecryptedArray &object) {
  auto jo = jv.enter_object();
  jo("@type", "msg.dataDecryptedArray");
  jo("elements", ToJson(object.elements_));
}
void to_json(JsonValueScope &jv, const tonlib_api::msg_dataEncrypted &object) {
  auto jo = jv.enter_object();
  jo("@type", "msg.dataEncrypted");
  if (object.source_) {
    jo("source", ToJson(object.source_));
  }
  if (object.data_) {
    jo("data", ToJson(object.data_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::msg_dataEncryptedArray &object) {
  auto jo = jv.enter_object();
  jo("@type", "msg.dataEncryptedArray");
  jo("elements", ToJson(object.elements_));
}
void to_json(JsonValueScope &jv, const tonlib_api::msg_message &object) {
  auto jo = jv.enter_object();
  jo("@type", "msg.message");
  if (object.destination_) {
    jo("destination", ToJson(object.destination_));
  }
  jo("public_key", ToJson(object.public_key_));
  jo("amount", ToJson(JsonInt64{object.amount_}));
  jo("extra_currencies", ToJson(object.extra_currencies_));
  if (object.data_) {
    jo("data", ToJson(object.data_));
  }
  jo("send_mode", ToJson(object.send_mode_));
}
void to_json(JsonValueScope &jv, const tonlib_api::options_configInfo &object) {
  auto jo = jv.enter_object();
  jo("@type", "options.configInfo");
  jo("default_wallet_id", ToJson(JsonInt64{object.default_wallet_id_}));
  jo("default_rwallet_init_public_key", ToJson(object.default_rwallet_init_public_key_));
}
void to_json(JsonValueScope &jv, const tonlib_api::options_info &object) {
  auto jo = jv.enter_object();
  jo("@type", "options.info");
  if (object.config_info_) {
    jo("config_info", ToJson(object.config_info_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::pchan_Action &object) {
  tonlib_api::downcast_call(const_cast<tonlib_api::pchan_Action &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
void to_json(JsonValueScope &jv, const tonlib_api::pchan_actionInit &object) {
  auto jo = jv.enter_object();
  jo("@type", "pchan.actionInit");
  jo("inc_A", ToJson(JsonInt64{object.inc_A_}));
  jo("inc_B", ToJson(JsonInt64{object.inc_B_}));
  jo("min_A", ToJson(JsonInt64{object.min_A_}));
  jo("min_B", ToJson(JsonInt64{object.min_B_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::pchan_actionClose &object) {
  auto jo = jv.enter_object();
  jo("@type", "pchan.actionClose");
  jo("extra_A", ToJson(JsonInt64{object.extra_A_}));
  jo("extra_B", ToJson(JsonInt64{object.extra_B_}));
  if (object.promise_) {
    jo("promise", ToJson(object.promise_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::pchan_actionTimeout &object) {
  auto jo = jv.enter_object();
  jo("@type", "pchan.actionTimeout");
}
void to_json(JsonValueScope &jv, const tonlib_api::pchan_config &object) {
  auto jo = jv.enter_object();
  jo("@type", "pchan.config");
  jo("alice_public_key", ToJson(object.alice_public_key_));
  if (object.alice_address_) {
    jo("alice_address", ToJson(object.alice_address_));
  }
  jo("bob_public_key", ToJson(object.bob_public_key_));
  if (object.bob_address_) {
    jo("bob_address", ToJson(object.bob_address_));
  }
  jo("init_timeout", ToJson(object.init_timeout_));
  jo("close_timeout", ToJson(object.close_timeout_));
  jo("channel_id", ToJson(JsonInt64{object.channel_id_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::pchan_promise &object) {
  auto jo = jv.enter_object();
  jo("@type", "pchan.promise");
  jo("signature", ToJson(JsonBytes{object.signature_}));
  jo("promise_A", ToJson(JsonInt64{object.promise_A_}));
  jo("promise_B", ToJson(JsonInt64{object.promise_B_}));
  jo("channel_id", ToJson(JsonInt64{object.channel_id_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::pchan_State &object) {
  tonlib_api::downcast_call(const_cast<tonlib_api::pchan_State &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
void to_json(JsonValueScope &jv, const tonlib_api::pchan_stateInit &object) {
  auto jo = jv.enter_object();
  jo("@type", "pchan.stateInit");
  jo("signed_A", ToJson(JsonBool{object.signed_A_}));
  jo("signed_B", ToJson(JsonBool{object.signed_B_}));
  jo("min_A", ToJson(JsonInt64{object.min_A_}));
  jo("min_B", ToJson(JsonInt64{object.min_B_}));
  jo("expire_at", ToJson(object.expire_at_));
  jo("A", ToJson(JsonInt64{object.A_}));
  jo("B", ToJson(JsonInt64{object.B_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::pchan_stateClose &object) {
  auto jo = jv.enter_object();
  jo("@type", "pchan.stateClose");
  jo("signed_A", ToJson(JsonBool{object.signed_A_}));
  jo("signed_B", ToJson(JsonBool{object.signed_B_}));
  jo("min_A", ToJson(JsonInt64{object.min_A_}));
  jo("min_B", ToJson(JsonInt64{object.min_B_}));
  jo("expire_at", ToJson(object.expire_at_));
  jo("A", ToJson(JsonInt64{object.A_}));
  jo("B", ToJson(JsonInt64{object.B_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::pchan_statePayout &object) {
  auto jo = jv.enter_object();
  jo("@type", "pchan.statePayout");
  jo("A", ToJson(JsonInt64{object.A_}));
  jo("B", ToJson(JsonInt64{object.B_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::query_fees &object) {
  auto jo = jv.enter_object();
  jo("@type", "query.fees");
  if (object.source_fees_) {
    jo("source_fees", ToJson(object.source_fees_));
  }
  jo("destination_fees", ToJson(object.destination_fees_));
}
void to_json(JsonValueScope &jv, const tonlib_api::query_info &object) {
  auto jo = jv.enter_object();
  jo("@type", "query.info");
  jo("id", ToJson(object.id_));
  jo("valid_until", ToJson(object.valid_until_));
  jo("body_hash", ToJson(JsonBytes{object.body_hash_}));
  jo("body", ToJson(JsonBytes{object.body_}));
  jo("init_state", ToJson(JsonBytes{object.init_state_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::raw_extMessageInfo &object) {
  auto jo = jv.enter_object();
  jo("@type", "raw.extMessageInfo");
  jo("hash", ToJson(JsonBytes{object.hash_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::raw_fullAccountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "raw.fullAccountState");
  jo("balance", ToJson(JsonInt64{object.balance_}));
  jo("extra_currencies", ToJson(object.extra_currencies_));
  jo("code", ToJson(JsonBytes{object.code_}));
  jo("data", ToJson(JsonBytes{object.data_}));
  if (object.last_transaction_id_) {
    jo("last_transaction_id", ToJson(object.last_transaction_id_));
  }
  if (object.block_id_) {
    jo("block_id", ToJson(object.block_id_));
  }
  jo("frozen_hash", ToJson(JsonBytes{object.frozen_hash_}));
  jo("sync_utime", ToJson(object.sync_utime_));
}
void to_json(JsonValueScope &jv, const tonlib_api::raw_message &object) {
  auto jo = jv.enter_object();
  jo("@type", "raw.message");
  jo("hash", ToJson(JsonBytes{object.hash_}));
  if (object.source_) {
    jo("source", ToJson(object.source_));
  }
  if (object.destination_) {
    jo("destination", ToJson(object.destination_));
  }
  jo("value", ToJson(JsonInt64{object.value_}));
  jo("extra_currencies", ToJson(object.extra_currencies_));
  jo("fwd_fee", ToJson(JsonInt64{object.fwd_fee_}));
  jo("ihr_fee", ToJson(JsonInt64{object.ihr_fee_}));
  jo("created_lt", ToJson(JsonInt64{object.created_lt_}));
  jo("body_hash", ToJson(JsonBytes{object.body_hash_}));
  if (object.msg_data_) {
    jo("msg_data", ToJson(object.msg_data_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::raw_transaction &object) {
  auto jo = jv.enter_object();
  jo("@type", "raw.transaction");
  if (object.address_) {
    jo("address", ToJson(object.address_));
  }
  jo("utime", ToJson(object.utime_));
  jo("data", ToJson(JsonBytes{object.data_}));
  if (object.transaction_id_) {
    jo("transaction_id", ToJson(object.transaction_id_));
  }
  jo("fee", ToJson(JsonInt64{object.fee_}));
  jo("storage_fee", ToJson(JsonInt64{object.storage_fee_}));
  jo("other_fee", ToJson(JsonInt64{object.other_fee_}));
  if (object.in_msg_) {
    jo("in_msg", ToJson(object.in_msg_));
  }
  jo("out_msgs", ToJson(object.out_msgs_));
}
void to_json(JsonValueScope &jv, const tonlib_api::raw_transactions &object) {
  auto jo = jv.enter_object();
  jo("@type", "raw.transactions");
  jo("transactions", ToJson(object.transactions_));
  if (object.previous_transaction_id_) {
    jo("previous_transaction_id", ToJson(object.previous_transaction_id_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::rwallet_actionInit &object) {
  auto jo = jv.enter_object();
  jo("@type", "rwallet.actionInit");
  if (object.config_) {
    jo("config", ToJson(object.config_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::rwallet_config &object) {
  auto jo = jv.enter_object();
  jo("@type", "rwallet.config");
  jo("start_at", ToJson(object.start_at_));
  jo("limits", ToJson(object.limits_));
}
void to_json(JsonValueScope &jv, const tonlib_api::rwallet_limit &object) {
  auto jo = jv.enter_object();
  jo("@type", "rwallet.limit");
  jo("seconds", ToJson(object.seconds_));
  jo("value", ToJson(JsonInt64{object.value_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_info &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.info");
  jo("id", ToJson(object.id_));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_libraryEntry &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.libraryEntry");
  jo("hash", ToJson(object.hash_));
  jo("data", ToJson(JsonBytes{object.data_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_LibraryQueryExt &object) {
  tonlib_api::downcast_call(const_cast<tonlib_api::smc_LibraryQueryExt &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_libraryQueryExt_one &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.libraryQueryExt.one");
  jo("hash", ToJson(object.hash_));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_libraryQueryExt_scanBoc &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.libraryQueryExt.scanBoc");
  jo("boc", ToJson(JsonBytes{object.boc_}));
  jo("max_libs", ToJson(object.max_libs_));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_libraryResult &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.libraryResult");
  jo("result", ToJson(object.result_));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_libraryResultExt &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.libraryResultExt");
  jo("dict_boc", ToJson(JsonBytes{object.dict_boc_}));
  jo("libs_ok", ToJson(object.libs_ok_));
  jo("libs_not_found", ToJson(object.libs_not_found_));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_MethodId &object) {
  tonlib_api::downcast_call(const_cast<tonlib_api::smc_MethodId &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_methodIdNumber &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.methodIdNumber");
  jo("number", ToJson(object.number_));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_methodIdName &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.methodIdName");
  jo("name", ToJson(object.name_));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_runResult &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.runResult");
  jo("gas_used", ToJson(object.gas_used_));
  jo("stack", ToJson(object.stack_));
  jo("exit_code", ToJson(object.exit_code_));
}
void to_json(JsonValueScope &jv, const tonlib_api::ton_blockIdExt &object) {
  auto jo = jv.enter_object();
  jo("@type", "ton.blockIdExt");
  jo("workchain", ToJson(object.workchain_));
  jo("shard", ToJson(JsonInt64{object.shard_}));
  jo("seqno", ToJson(object.seqno_));
  jo("root_hash", ToJson(JsonBytes{object.root_hash_}));
  jo("file_hash", ToJson(JsonBytes{object.file_hash_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::tvm_cell &object) {
  auto jo = jv.enter_object();
  jo("@type", "tvm.cell");
  jo("bytes", ToJson(JsonBytes{object.bytes_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::tvm_list &object) {
  auto jo = jv.enter_object();
  jo("@type", "tvm.list");
  jo("elements", ToJson(object.elements_));
}
void to_json(JsonValueScope &jv, const tonlib_api::tvm_numberDecimal &object) {
  auto jo = jv.enter_object();
  jo("@type", "tvm.numberDecimal");
  jo("number", ToJson(object.number_));
}
void to_json(JsonValueScope &jv, const tonlib_api::tvm_slice &object) {
  auto jo = jv.enter_object();
  jo("@type", "tvm.slice");
  jo("bytes", ToJson(JsonBytes{object.bytes_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::tvm_StackEntry &object) {
  tonlib_api::downcast_call(const_cast<tonlib_api::tvm_StackEntry &>(object), [&jv](const auto &object) { to_json(jv, object); });
}
void to_json(JsonValueScope &jv, const tonlib_api::tvm_stackEntrySlice &object) {
  auto jo = jv.enter_object();
  jo("@type", "tvm.stackEntrySlice");
  if (object.slice_) {
    jo("slice", ToJson(object.slice_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::tvm_stackEntryCell &object) {
  auto jo = jv.enter_object();
  jo("@type", "tvm.stackEntryCell");
  if (object.cell_) {
    jo("cell", ToJson(object.cell_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::tvm_stackEntryNumber &object) {
  auto jo = jv.enter_object();
  jo("@type", "tvm.stackEntryNumber");
  if (object.number_) {
    jo("number", ToJson(object.number_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::tvm_stackEntryTuple &object) {
  auto jo = jv.enter_object();
  jo("@type", "tvm.stackEntryTuple");
  if (object.tuple_) {
    jo("tuple", ToJson(object.tuple_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::tvm_stackEntryList &object) {
  auto jo = jv.enter_object();
  jo("@type", "tvm.stackEntryList");
  if (object.list_) {
    jo("list", ToJson(object.list_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::tvm_stackEntryUnsupported &object) {
  auto jo = jv.enter_object();
  jo("@type", "tvm.stackEntryUnsupported");
}
void to_json(JsonValueScope &jv, const tonlib_api::tvm_tuple &object) {
  auto jo = jv.enter_object();
  jo("@type", "tvm.tuple");
  jo("elements", ToJson(object.elements_));
}
void to_json(JsonValueScope &jv, const tonlib_api::addLogMessage &object) {
  auto jo = jv.enter_object();
  jo("@type", "addLogMessage");
  jo("verbosity_level", ToJson(object.verbosity_level_));
  jo("text", ToJson(object.text_));
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_getBlockHeader &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.getBlockHeader");
  if (object.id_) {
    jo("id", ToJson(object.id_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_getMasterchainBlockSignatures &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.getMasterchainBlockSignatures");
  jo("seqno", ToJson(object.seqno_));
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_getMasterchainInfo &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.getMasterchainInfo");
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_getOutMsgQueueSizes &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.getOutMsgQueueSizes");
  if (object.mode_) {
    jo("mode", ToJson(object.mode_));
  }
  if ((object.mode_ & 1)) {
    jo("wc", ToJson(object.wc_));
  }
  if ((object.mode_ & 1)) {
    jo("shard", ToJson(JsonInt64{object.shard_}));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_getShardBlockProof &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.getShardBlockProof");
  if (object.id_) {
    jo("id", ToJson(object.id_));
  }
  if (object.mode_) {
    jo("mode", ToJson(object.mode_));
  }
  if (object.from_ && (object.mode_ & 1)) {
    jo("from", ToJson(object.from_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_getShards &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.getShards");
  if (object.id_) {
    jo("id", ToJson(object.id_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_getTransactions &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.getTransactions");
  if (object.id_) {
    jo("id", ToJson(object.id_));
  }
  if (object.mode_) {
    jo("mode", ToJson(object.mode_));
  }
  if (object.count_) {
    jo("count", ToJson(object.count_));
  }
  if (object.after_) {
    jo("after", ToJson(object.after_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_getTransactionsExt &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.getTransactionsExt");
  if (object.id_) {
    jo("id", ToJson(object.id_));
  }
  if (object.mode_) {
    jo("mode", ToJson(object.mode_));
  }
  if (object.count_) {
    jo("count", ToJson(object.count_));
  }
  if (object.after_) {
    jo("after", ToJson(object.after_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::blocks_lookupBlock &object) {
  auto jo = jv.enter_object();
  jo("@type", "blocks.lookupBlock");
  jo("mode", ToJson(object.mode_));
  if (object.id_) {
    jo("id", ToJson(object.id_));
  }
  jo("lt", ToJson(JsonInt64{object.lt_}));
  jo("utime", ToJson(object.utime_));
}
void to_json(JsonValueScope &jv, const tonlib_api::changeLocalPassword &object) {
  auto jo = jv.enter_object();
  jo("@type", "changeLocalPassword");
  if (object.input_key_) {
    jo("input_key", ToJson(object.input_key_));
  }
  jo("new_local_password", ToJson(JsonBytes{object.new_local_password_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::close &object) {
  auto jo = jv.enter_object();
  jo("@type", "close");
}
void to_json(JsonValueScope &jv, const tonlib_api::createNewKey &object) {
  auto jo = jv.enter_object();
  jo("@type", "createNewKey");
  jo("local_password", ToJson(JsonBytes{object.local_password_}));
  jo("mnemonic_password", ToJson(JsonBytes{object.mnemonic_password_}));
  jo("random_extra_seed", ToJson(JsonBytes{object.random_extra_seed_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::createQuery &object) {
  auto jo = jv.enter_object();
  jo("@type", "createQuery");
  if (object.private_key_) {
    jo("private_key", ToJson(object.private_key_));
  }
  if (object.address_) {
    jo("address", ToJson(object.address_));
  }
  jo("timeout", ToJson(object.timeout_));
  if (object.action_) {
    jo("action", ToJson(object.action_));
  }
  if (object.initial_account_state_) {
    jo("initial_account_state", ToJson(object.initial_account_state_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::decrypt &object) {
  auto jo = jv.enter_object();
  jo("@type", "decrypt");
  jo("encrypted_data", ToJson(JsonBytes{object.encrypted_data_}));
  jo("secret", ToJson(JsonBytes{object.secret_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::deleteAllKeys &object) {
  auto jo = jv.enter_object();
  jo("@type", "deleteAllKeys");
}
void to_json(JsonValueScope &jv, const tonlib_api::deleteKey &object) {
  auto jo = jv.enter_object();
  jo("@type", "deleteKey");
  if (object.key_) {
    jo("key", ToJson(object.key_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::dns_resolve &object) {
  auto jo = jv.enter_object();
  jo("@type", "dns.resolve");
  if (object.account_address_) {
    jo("account_address", ToJson(object.account_address_));
  }
  jo("name", ToJson(object.name_));
  jo("category", ToJson(object.category_));
  jo("ttl", ToJson(object.ttl_));
}
void to_json(JsonValueScope &jv, const tonlib_api::encrypt &object) {
  auto jo = jv.enter_object();
  jo("@type", "encrypt");
  jo("decrypted_data", ToJson(JsonBytes{object.decrypted_data_}));
  jo("secret", ToJson(JsonBytes{object.secret_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::exportEncryptedKey &object) {
  auto jo = jv.enter_object();
  jo("@type", "exportEncryptedKey");
  if (object.input_key_) {
    jo("input_key", ToJson(object.input_key_));
  }
  jo("key_password", ToJson(JsonBytes{object.key_password_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::exportKey &object) {
  auto jo = jv.enter_object();
  jo("@type", "exportKey");
  if (object.input_key_) {
    jo("input_key", ToJson(object.input_key_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::exportPemKey &object) {
  auto jo = jv.enter_object();
  jo("@type", "exportPemKey");
  if (object.input_key_) {
    jo("input_key", ToJson(object.input_key_));
  }
  jo("key_password", ToJson(JsonBytes{object.key_password_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::exportUnencryptedKey &object) {
  auto jo = jv.enter_object();
  jo("@type", "exportUnencryptedKey");
  if (object.input_key_) {
    jo("input_key", ToJson(object.input_key_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::getAccountAddress &object) {
  auto jo = jv.enter_object();
  jo("@type", "getAccountAddress");
  if (object.initial_account_state_) {
    jo("initial_account_state", ToJson(object.initial_account_state_));
  }
  jo("revision", ToJson(object.revision_));
  jo("workchain_id", ToJson(object.workchain_id_));
}
void to_json(JsonValueScope &jv, const tonlib_api::getAccountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "getAccountState");
  if (object.account_address_) {
    jo("account_address", ToJson(object.account_address_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::getAccountStateByTransaction &object) {
  auto jo = jv.enter_object();
  jo("@type", "getAccountStateByTransaction");
  if (object.account_address_) {
    jo("account_address", ToJson(object.account_address_));
  }
  if (object.transaction_id_) {
    jo("transaction_id", ToJson(object.transaction_id_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::getBip39Hints &object) {
  auto jo = jv.enter_object();
  jo("@type", "getBip39Hints");
  jo("prefix", ToJson(object.prefix_));
}
void to_json(JsonValueScope &jv, const tonlib_api::getConfigAll &object) {
  auto jo = jv.enter_object();
  jo("@type", "getConfigAll");
  if (object.mode_) {
    jo("mode", ToJson(object.mode_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::getConfigParam &object) {
  auto jo = jv.enter_object();
  jo("@type", "getConfigParam");
  if (object.mode_) {
    jo("mode", ToJson(object.mode_));
  }
  if (object.param_) {
    jo("param", ToJson(object.param_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::getLogStream &object) {
  auto jo = jv.enter_object();
  jo("@type", "getLogStream");
}
void to_json(JsonValueScope &jv, const tonlib_api::getLogTagVerbosityLevel &object) {
  auto jo = jv.enter_object();
  jo("@type", "getLogTagVerbosityLevel");
  jo("tag", ToJson(object.tag_));
}
void to_json(JsonValueScope &jv, const tonlib_api::getLogTags &object) {
  auto jo = jv.enter_object();
  jo("@type", "getLogTags");
}
void to_json(JsonValueScope &jv, const tonlib_api::getLogVerbosityLevel &object) {
  auto jo = jv.enter_object();
  jo("@type", "getLogVerbosityLevel");
}
void to_json(JsonValueScope &jv, const tonlib_api::getShardAccountCell &object) {
  auto jo = jv.enter_object();
  jo("@type", "getShardAccountCell");
  if (object.account_address_) {
    jo("account_address", ToJson(object.account_address_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::getShardAccountCellByTransaction &object) {
  auto jo = jv.enter_object();
  jo("@type", "getShardAccountCellByTransaction");
  if (object.account_address_) {
    jo("account_address", ToJson(object.account_address_));
  }
  if (object.transaction_id_) {
    jo("transaction_id", ToJson(object.transaction_id_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::guessAccount &object) {
  auto jo = jv.enter_object();
  jo("@type", "guessAccount");
  jo("public_key", ToJson(object.public_key_));
  jo("rwallet_init_public_key", ToJson(object.rwallet_init_public_key_));
}
void to_json(JsonValueScope &jv, const tonlib_api::guessAccountRevision &object) {
  auto jo = jv.enter_object();
  jo("@type", "guessAccountRevision");
  if (object.initial_account_state_) {
    jo("initial_account_state", ToJson(object.initial_account_state_));
  }
  jo("workchain_id", ToJson(object.workchain_id_));
}
void to_json(JsonValueScope &jv, const tonlib_api::importEncryptedKey &object) {
  auto jo = jv.enter_object();
  jo("@type", "importEncryptedKey");
  jo("local_password", ToJson(JsonBytes{object.local_password_}));
  jo("key_password", ToJson(JsonBytes{object.key_password_}));
  if (object.exported_encrypted_key_) {
    jo("exported_encrypted_key", ToJson(object.exported_encrypted_key_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::importKey &object) {
  auto jo = jv.enter_object();
  jo("@type", "importKey");
  jo("local_password", ToJson(JsonBytes{object.local_password_}));
  jo("mnemonic_password", ToJson(JsonBytes{object.mnemonic_password_}));
  if (object.exported_key_) {
    jo("exported_key", ToJson(object.exported_key_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::importPemKey &object) {
  auto jo = jv.enter_object();
  jo("@type", "importPemKey");
  jo("local_password", ToJson(JsonBytes{object.local_password_}));
  jo("key_password", ToJson(JsonBytes{object.key_password_}));
  if (object.exported_key_) {
    jo("exported_key", ToJson(object.exported_key_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::importUnencryptedKey &object) {
  auto jo = jv.enter_object();
  jo("@type", "importUnencryptedKey");
  jo("local_password", ToJson(JsonBytes{object.local_password_}));
  if (object.exported_unencrypted_key_) {
    jo("exported_unencrypted_key", ToJson(object.exported_unencrypted_key_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::init &object) {
  auto jo = jv.enter_object();
  jo("@type", "init");
  if (object.options_) {
    jo("options", ToJson(object.options_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::kdf &object) {
  auto jo = jv.enter_object();
  jo("@type", "kdf");
  jo("password", ToJson(JsonBytes{object.password_}));
  jo("salt", ToJson(JsonBytes{object.salt_}));
  jo("iterations", ToJson(object.iterations_));
}
void to_json(JsonValueScope &jv, const tonlib_api::liteServer_getInfo &object) {
  auto jo = jv.enter_object();
  jo("@type", "liteServer.getInfo");
}
void to_json(JsonValueScope &jv, const tonlib_api::msg_decrypt &object) {
  auto jo = jv.enter_object();
  jo("@type", "msg.decrypt");
  if (object.input_key_) {
    jo("input_key", ToJson(object.input_key_));
  }
  if (object.data_) {
    jo("data", ToJson(object.data_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::msg_decryptWithProof &object) {
  auto jo = jv.enter_object();
  jo("@type", "msg.decryptWithProof");
  jo("proof", ToJson(JsonBytes{object.proof_}));
  if (object.data_) {
    jo("data", ToJson(object.data_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::onLiteServerQueryError &object) {
  auto jo = jv.enter_object();
  jo("@type", "onLiteServerQueryError");
  jo("id", ToJson(JsonInt64{object.id_}));
  if (object.error_) {
    jo("error", ToJson(object.error_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::onLiteServerQueryResult &object) {
  auto jo = jv.enter_object();
  jo("@type", "onLiteServerQueryResult");
  jo("id", ToJson(JsonInt64{object.id_}));
  jo("bytes", ToJson(JsonBytes{object.bytes_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::options_setConfig &object) {
  auto jo = jv.enter_object();
  jo("@type", "options.setConfig");
  if (object.config_) {
    jo("config", ToJson(object.config_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::options_validateConfig &object) {
  auto jo = jv.enter_object();
  jo("@type", "options.validateConfig");
  if (object.config_) {
    jo("config", ToJson(object.config_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::packAccountAddress &object) {
  auto jo = jv.enter_object();
  jo("@type", "packAccountAddress");
  if (object.account_address_) {
    jo("account_address", ToJson(object.account_address_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::pchan_packPromise &object) {
  auto jo = jv.enter_object();
  jo("@type", "pchan.packPromise");
  if (object.promise_) {
    jo("promise", ToJson(object.promise_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::pchan_signPromise &object) {
  auto jo = jv.enter_object();
  jo("@type", "pchan.signPromise");
  if (object.input_key_) {
    jo("input_key", ToJson(object.input_key_));
  }
  if (object.promise_) {
    jo("promise", ToJson(object.promise_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::pchan_unpackPromise &object) {
  auto jo = jv.enter_object();
  jo("@type", "pchan.unpackPromise");
  jo("data", ToJson(JsonBytes{object.data_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::pchan_validatePromise &object) {
  auto jo = jv.enter_object();
  jo("@type", "pchan.validatePromise");
  jo("public_key", ToJson(JsonBytes{object.public_key_}));
  if (object.promise_) {
    jo("promise", ToJson(object.promise_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::query_estimateFees &object) {
  auto jo = jv.enter_object();
  jo("@type", "query.estimateFees");
  jo("id", ToJson(object.id_));
  jo("ignore_chksig", ToJson(JsonBool{object.ignore_chksig_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::query_forget &object) {
  auto jo = jv.enter_object();
  jo("@type", "query.forget");
  jo("id", ToJson(object.id_));
}
void to_json(JsonValueScope &jv, const tonlib_api::query_getInfo &object) {
  auto jo = jv.enter_object();
  jo("@type", "query.getInfo");
  jo("id", ToJson(object.id_));
}
void to_json(JsonValueScope &jv, const tonlib_api::query_send &object) {
  auto jo = jv.enter_object();
  jo("@type", "query.send");
  jo("id", ToJson(object.id_));
}
void to_json(JsonValueScope &jv, const tonlib_api::raw_createAndSendMessage &object) {
  auto jo = jv.enter_object();
  jo("@type", "raw.createAndSendMessage");
  if (object.destination_) {
    jo("destination", ToJson(object.destination_));
  }
  jo("initial_account_state", ToJson(JsonBytes{object.initial_account_state_}));
  jo("data", ToJson(JsonBytes{object.data_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::raw_createQuery &object) {
  auto jo = jv.enter_object();
  jo("@type", "raw.createQuery");
  if (object.destination_) {
    jo("destination", ToJson(object.destination_));
  }
  jo("init_code", ToJson(JsonBytes{object.init_code_}));
  jo("init_data", ToJson(JsonBytes{object.init_data_}));
  jo("body", ToJson(JsonBytes{object.body_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::raw_getAccountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "raw.getAccountState");
  if (object.account_address_) {
    jo("account_address", ToJson(object.account_address_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::raw_getAccountStateByTransaction &object) {
  auto jo = jv.enter_object();
  jo("@type", "raw.getAccountStateByTransaction");
  if (object.account_address_) {
    jo("account_address", ToJson(object.account_address_));
  }
  if (object.transaction_id_) {
    jo("transaction_id", ToJson(object.transaction_id_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::raw_getTransactions &object) {
  auto jo = jv.enter_object();
  jo("@type", "raw.getTransactions");
  if (object.private_key_) {
    jo("private_key", ToJson(object.private_key_));
  }
  if (object.account_address_) {
    jo("account_address", ToJson(object.account_address_));
  }
  if (object.from_transaction_id_) {
    jo("from_transaction_id", ToJson(object.from_transaction_id_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::raw_getTransactionsV2 &object) {
  auto jo = jv.enter_object();
  jo("@type", "raw.getTransactionsV2");
  if (object.private_key_) {
    jo("private_key", ToJson(object.private_key_));
  }
  if (object.account_address_) {
    jo("account_address", ToJson(object.account_address_));
  }
  if (object.from_transaction_id_) {
    jo("from_transaction_id", ToJson(object.from_transaction_id_));
  }
  if (object.count_) {
    jo("count", ToJson(object.count_));
  }
  jo("try_decode_messages", ToJson(JsonBool{object.try_decode_messages_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::raw_sendMessage &object) {
  auto jo = jv.enter_object();
  jo("@type", "raw.sendMessage");
  jo("body", ToJson(JsonBytes{object.body_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::raw_sendMessageReturnHash &object) {
  auto jo = jv.enter_object();
  jo("@type", "raw.sendMessageReturnHash");
  jo("body", ToJson(JsonBytes{object.body_}));
}
void to_json(JsonValueScope &jv, const tonlib_api::runTests &object) {
  auto jo = jv.enter_object();
  jo("@type", "runTests");
  jo("dir", ToJson(object.dir_));
}
void to_json(JsonValueScope &jv, const tonlib_api::setLogStream &object) {
  auto jo = jv.enter_object();
  jo("@type", "setLogStream");
  if (object.log_stream_) {
    jo("log_stream", ToJson(object.log_stream_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::setLogTagVerbosityLevel &object) {
  auto jo = jv.enter_object();
  jo("@type", "setLogTagVerbosityLevel");
  jo("tag", ToJson(object.tag_));
  jo("new_verbosity_level", ToJson(object.new_verbosity_level_));
}
void to_json(JsonValueScope &jv, const tonlib_api::setLogVerbosityLevel &object) {
  auto jo = jv.enter_object();
  jo("@type", "setLogVerbosityLevel");
  jo("new_verbosity_level", ToJson(object.new_verbosity_level_));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_forget &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.forget");
  jo("id", ToJson(object.id_));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_getCode &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.getCode");
  jo("id", ToJson(object.id_));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_getData &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.getData");
  jo("id", ToJson(object.id_));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_getLibraries &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.getLibraries");
  jo("library_list", ToJson(object.library_list_));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_getLibrariesExt &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.getLibrariesExt");
  jo("list", ToJson(object.list_));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_getRawFullAccountState &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.getRawFullAccountState");
  jo("id", ToJson(object.id_));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_getState &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.getState");
  jo("id", ToJson(object.id_));
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_load &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.load");
  if (object.account_address_) {
    jo("account_address", ToJson(object.account_address_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_loadByTransaction &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.loadByTransaction");
  if (object.account_address_) {
    jo("account_address", ToJson(object.account_address_));
  }
  if (object.transaction_id_) {
    jo("transaction_id", ToJson(object.transaction_id_));
  }
}
void to_json(JsonValueScope &jv, const tonlib_api::smc_runGetMethod &object) {
  auto jo = jv.enter_object();
  jo("@type", "smc.runGetMethod");
  jo("id", ToJson(object.id_));
  if (object.method_) {
    jo("method", ToJson(object.method_));
  }
  jo("stack", ToJson(object.stack_));
}
void to_json(JsonValueScope &jv, const tonlib_api::sync &object) {
  auto jo = jv.enter_object();
  jo("@type", "sync");
}
void to_json(JsonValueScope &jv, const tonlib_api::unpackAccountAddress &object) {
  auto jo = jv.enter_object();
  jo("@type", "unpackAccountAddress");
  jo("account_address", ToJson(object.account_address_));
}
void to_json(JsonValueScope &jv, const tonlib_api::withBlock &object) {
  auto jo = jv.enter_object();
  jo("@type", "withBlock");
  if (object.id_) {
    jo("id", ToJson(object.id_));
  }
  jo("function", ToJson(object.function_));
}
}  // namespace tonlib_api
}  // namespace ton
