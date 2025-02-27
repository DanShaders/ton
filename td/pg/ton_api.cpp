#include "ton_api.h"

#include "tl_object_parse.h"
#include "tl_object_store.h"
#include "td/utils/int_types.h"
#include "crypto/common/bitstring.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/tl_storers.h"

namespace ton {
namespace ton_api {

std::string to_string(const BaseObject &value) {
  td::TlStorerToString storer;
  value.store(storer, "");
  return storer.move_as_str();
}

object_ptr<Object> Object::fetch(td::TlParser &p) {
#define FAIL(error) p.set_error(error); return nullptr;
  int constructor = p.fetch_int();
  switch (constructor) {
    case pk_unenc::ID:
      return pk_unenc::fetch(p);
    case pk_ed25519::ID:
      return pk_ed25519::fetch(p);
    case pk_aes::ID:
      return pk_aes::fetch(p);
    case pk_overlay::ID:
      return pk_overlay::fetch(p);
    case pub_unenc::ID:
      return pub_unenc::fetch(p);
    case pub_ed25519::ID:
      return pub_ed25519::fetch(p);
    case pub_aes::ID:
      return pub_aes::fetch(p);
    case pub_overlay::ID:
      return pub_overlay::fetch(p);
    case contest_test::ID:
      return contest_test::fetch(p);
    case tonNode_blockId::ID:
      return tonNode_blockId::fetch(p);
    case tonNode_blockIdExt::ID:
      return tonNode_blockIdExt::fetch(p);
    case tonNode_shardId::ID:
      return tonNode_shardId::fetch(p);
    case tonNode_zeroStateIdExt::ID:
      return tonNode_zeroStateIdExt::fetch(p);
    default:
      FAIL(PSTRING() << "Unknown constructor found " << td::format::as_hex(constructor));
  }
#undef FAIL
}

object_ptr<Function> Function::fetch(td::TlParser &p) {
#define FAIL(error) p.set_error(error); return nullptr;
  int constructor = p.fetch_int();
  switch (constructor) {
    default:
      FAIL(PSTRING() << "Unknown constructor found " << td::format::as_hex(constructor));
  }
#undef FAIL
}

object_ptr<PrivateKey> PrivateKey::fetch(td::TlParser &p) {
#define FAIL(error) p.set_error(error); return nullptr;
  int constructor = p.fetch_int();
  switch (constructor) {
    case pk_unenc::ID:
      return pk_unenc::fetch(p);
    case pk_ed25519::ID:
      return pk_ed25519::fetch(p);
    case pk_aes::ID:
      return pk_aes::fetch(p);
    case pk_overlay::ID:
      return pk_overlay::fetch(p);
    default:
      FAIL(PSTRING() << "Unknown constructor found " << td::format::as_hex(constructor));
  }
#undef FAIL
}

pk_unenc::pk_unenc()
  : data_()
{}

pk_unenc::pk_unenc(td::BufferSlice &&data_)
  : data_(std::move(data_))
{}

const std::int32_t pk_unenc::ID;

object_ptr<PrivateKey> pk_unenc::fetch(td::TlParser &p) {
  return make_object<pk_unenc>(p);
}

pk_unenc::pk_unenc(td::TlParser &p)
#define FAIL(error) p.set_error(error)
  : data_(TlFetchBytes<td::BufferSlice>::parse(p))
#undef FAIL
{}

void pk_unenc::store(td::TlStorerCalcLength &s) const {
  (void)sizeof(s);
  TlStoreString::store(data_, s);
}

void pk_unenc::store(td::TlStorerUnsafe &s) const {
  (void)sizeof(s);
  TlStoreString::store(data_, s);
}

void pk_unenc::store(td::TlStorerToString &s, const char *field_name) const {
  if (!LOG_IS_STRIPPED(ERROR)) {
    s.store_class_begin(field_name, "pk_unenc");
    s.store_bytes_field("data", data_);
    s.store_class_end();
  }
}

pk_ed25519::pk_ed25519()
  : key_()
{}

pk_ed25519::pk_ed25519(td::Bits256 const &key_)
  : key_(key_)
{}

const std::int32_t pk_ed25519::ID;

object_ptr<PrivateKey> pk_ed25519::fetch(td::TlParser &p) {
  return make_object<pk_ed25519>(p);
}

pk_ed25519::pk_ed25519(td::TlParser &p)
#define FAIL(error) p.set_error(error)
  : key_(TlFetchInt256::parse(p))
#undef FAIL
{}

void pk_ed25519::store(td::TlStorerCalcLength &s) const {
  (void)sizeof(s);
  TlStoreBinary::store(key_, s);
}

void pk_ed25519::store(td::TlStorerUnsafe &s) const {
  (void)sizeof(s);
  TlStoreBinary::store(key_, s);
}

void pk_ed25519::store(td::TlStorerToString &s, const char *field_name) const {
  if (!LOG_IS_STRIPPED(ERROR)) {
    s.store_class_begin(field_name, "pk_ed25519");
    s.store_field("key", key_);
    s.store_class_end();
  }
}

pk_aes::pk_aes()
  : key_()
{}

pk_aes::pk_aes(td::Bits256 const &key_)
  : key_(key_)
{}

const std::int32_t pk_aes::ID;

object_ptr<PrivateKey> pk_aes::fetch(td::TlParser &p) {
  return make_object<pk_aes>(p);
}

pk_aes::pk_aes(td::TlParser &p)
#define FAIL(error) p.set_error(error)
  : key_(TlFetchInt256::parse(p))
#undef FAIL
{}

void pk_aes::store(td::TlStorerCalcLength &s) const {
  (void)sizeof(s);
  TlStoreBinary::store(key_, s);
}

void pk_aes::store(td::TlStorerUnsafe &s) const {
  (void)sizeof(s);
  TlStoreBinary::store(key_, s);
}

void pk_aes::store(td::TlStorerToString &s, const char *field_name) const {
  if (!LOG_IS_STRIPPED(ERROR)) {
    s.store_class_begin(field_name, "pk_aes");
    s.store_field("key", key_);
    s.store_class_end();
  }
}

pk_overlay::pk_overlay()
  : name_()
{}

pk_overlay::pk_overlay(td::BufferSlice &&name_)
  : name_(std::move(name_))
{}

const std::int32_t pk_overlay::ID;

object_ptr<PrivateKey> pk_overlay::fetch(td::TlParser &p) {
  return make_object<pk_overlay>(p);
}

pk_overlay::pk_overlay(td::TlParser &p)
#define FAIL(error) p.set_error(error)
  : name_(TlFetchBytes<td::BufferSlice>::parse(p))
#undef FAIL
{}

void pk_overlay::store(td::TlStorerCalcLength &s) const {
  (void)sizeof(s);
  TlStoreString::store(name_, s);
}

void pk_overlay::store(td::TlStorerUnsafe &s) const {
  (void)sizeof(s);
  TlStoreString::store(name_, s);
}

void pk_overlay::store(td::TlStorerToString &s, const char *field_name) const {
  if (!LOG_IS_STRIPPED(ERROR)) {
    s.store_class_begin(field_name, "pk_overlay");
    s.store_bytes_field("name", name_);
    s.store_class_end();
  }
}

object_ptr<PublicKey> PublicKey::fetch(td::TlParser &p) {
#define FAIL(error) p.set_error(error); return nullptr;
  int constructor = p.fetch_int();
  switch (constructor) {
    case pub_unenc::ID:
      return pub_unenc::fetch(p);
    case pub_ed25519::ID:
      return pub_ed25519::fetch(p);
    case pub_aes::ID:
      return pub_aes::fetch(p);
    case pub_overlay::ID:
      return pub_overlay::fetch(p);
    default:
      FAIL(PSTRING() << "Unknown constructor found " << td::format::as_hex(constructor));
  }
#undef FAIL
}

pub_unenc::pub_unenc()
  : data_()
{}

pub_unenc::pub_unenc(td::BufferSlice &&data_)
  : data_(std::move(data_))
{}

const std::int32_t pub_unenc::ID;

object_ptr<PublicKey> pub_unenc::fetch(td::TlParser &p) {
  return make_object<pub_unenc>(p);
}

pub_unenc::pub_unenc(td::TlParser &p)
#define FAIL(error) p.set_error(error)
  : data_(TlFetchBytes<td::BufferSlice>::parse(p))
#undef FAIL
{}

void pub_unenc::store(td::TlStorerCalcLength &s) const {
  (void)sizeof(s);
  TlStoreString::store(data_, s);
}

void pub_unenc::store(td::TlStorerUnsafe &s) const {
  (void)sizeof(s);
  TlStoreString::store(data_, s);
}

void pub_unenc::store(td::TlStorerToString &s, const char *field_name) const {
  if (!LOG_IS_STRIPPED(ERROR)) {
    s.store_class_begin(field_name, "pub_unenc");
    s.store_bytes_field("data", data_);
    s.store_class_end();
  }
}

pub_ed25519::pub_ed25519()
  : key_()
{}

pub_ed25519::pub_ed25519(td::Bits256 const &key_)
  : key_(key_)
{}

const std::int32_t pub_ed25519::ID;

object_ptr<PublicKey> pub_ed25519::fetch(td::TlParser &p) {
  return make_object<pub_ed25519>(p);
}

pub_ed25519::pub_ed25519(td::TlParser &p)
#define FAIL(error) p.set_error(error)
  : key_(TlFetchInt256::parse(p))
#undef FAIL
{}

void pub_ed25519::store(td::TlStorerCalcLength &s) const {
  (void)sizeof(s);
  TlStoreBinary::store(key_, s);
}

void pub_ed25519::store(td::TlStorerUnsafe &s) const {
  (void)sizeof(s);
  TlStoreBinary::store(key_, s);
}

void pub_ed25519::store(td::TlStorerToString &s, const char *field_name) const {
  if (!LOG_IS_STRIPPED(ERROR)) {
    s.store_class_begin(field_name, "pub_ed25519");
    s.store_field("key", key_);
    s.store_class_end();
  }
}

pub_aes::pub_aes()
  : key_()
{}

pub_aes::pub_aes(td::Bits256 const &key_)
  : key_(key_)
{}

const std::int32_t pub_aes::ID;

object_ptr<PublicKey> pub_aes::fetch(td::TlParser &p) {
  return make_object<pub_aes>(p);
}

pub_aes::pub_aes(td::TlParser &p)
#define FAIL(error) p.set_error(error)
  : key_(TlFetchInt256::parse(p))
#undef FAIL
{}

void pub_aes::store(td::TlStorerCalcLength &s) const {
  (void)sizeof(s);
  TlStoreBinary::store(key_, s);
}

void pub_aes::store(td::TlStorerUnsafe &s) const {
  (void)sizeof(s);
  TlStoreBinary::store(key_, s);
}

void pub_aes::store(td::TlStorerToString &s, const char *field_name) const {
  if (!LOG_IS_STRIPPED(ERROR)) {
    s.store_class_begin(field_name, "pub_aes");
    s.store_field("key", key_);
    s.store_class_end();
  }
}

pub_overlay::pub_overlay()
  : name_()
{}

pub_overlay::pub_overlay(td::BufferSlice &&name_)
  : name_(std::move(name_))
{}

const std::int32_t pub_overlay::ID;

object_ptr<PublicKey> pub_overlay::fetch(td::TlParser &p) {
  return make_object<pub_overlay>(p);
}

pub_overlay::pub_overlay(td::TlParser &p)
#define FAIL(error) p.set_error(error)
  : name_(TlFetchBytes<td::BufferSlice>::parse(p))
#undef FAIL
{}

void pub_overlay::store(td::TlStorerCalcLength &s) const {
  (void)sizeof(s);
  TlStoreString::store(name_, s);
}

void pub_overlay::store(td::TlStorerUnsafe &s) const {
  (void)sizeof(s);
  TlStoreString::store(name_, s);
}

void pub_overlay::store(td::TlStorerToString &s, const char *field_name) const {
  if (!LOG_IS_STRIPPED(ERROR)) {
    s.store_class_begin(field_name, "pub_overlay");
    s.store_bytes_field("name", name_);
    s.store_class_end();
  }
}

contest_test::contest_test()
  : block_id_()
  , block_data_()
  , collated_data_()
  , valid_()
{}

contest_test::contest_test(object_ptr<tonNode_blockIdExt> &&block_id_, td::BufferSlice &&block_data_, td::BufferSlice &&collated_data_, bool valid_)
  : block_id_(std::move(block_id_))
  , block_data_(std::move(block_data_))
  , collated_data_(std::move(collated_data_))
  , valid_(valid_)
{}

const std::int32_t contest_test::ID;

object_ptr<contest_test> contest_test::fetch(td::TlParser &p) {
  return make_object<contest_test>(p);
}

contest_test::contest_test(td::TlParser &p)
#define FAIL(error) p.set_error(error)
  : block_id_(TlFetchObject<tonNode_blockIdExt>::parse(p))
  , block_data_(TlFetchBytes<td::BufferSlice>::parse(p))
  , collated_data_(TlFetchBytes<td::BufferSlice>::parse(p))
  , valid_(TlFetchBool::parse(p))
#undef FAIL
{}

void contest_test::store(td::TlStorerCalcLength &s) const {
  (void)sizeof(s);
  TlStoreObject::store(block_id_, s);
  TlStoreString::store(block_data_, s);
  TlStoreString::store(collated_data_, s);
  TlStoreBool::store(valid_, s);
}

void contest_test::store(td::TlStorerUnsafe &s) const {
  (void)sizeof(s);
  TlStoreObject::store(block_id_, s);
  TlStoreString::store(block_data_, s);
  TlStoreString::store(collated_data_, s);
  TlStoreBool::store(valid_, s);
}

void contest_test::store(td::TlStorerToString &s, const char *field_name) const {
  if (!LOG_IS_STRIPPED(ERROR)) {
    s.store_class_begin(field_name, "contest_test");
    if (block_id_ == nullptr) { s.store_field("block_id", "null"); } else { block_id_->store(s, "block_id"); }
    s.store_bytes_field("block_data", block_data_);
    s.store_bytes_field("collated_data", collated_data_);
    s.store_field("valid", valid_);
    s.store_class_end();
  }
}

tonNode_blockId::tonNode_blockId()
  : workchain_()
  , shard_()
  , seqno_()
{}

tonNode_blockId::tonNode_blockId(std::int32_t workchain_, std::int64_t shard_, std::int32_t seqno_)
  : workchain_(workchain_)
  , shard_(shard_)
  , seqno_(seqno_)
{}

const std::int32_t tonNode_blockId::ID;

object_ptr<tonNode_blockId> tonNode_blockId::fetch(td::TlParser &p) {
  return make_object<tonNode_blockId>(p);
}

tonNode_blockId::tonNode_blockId(td::TlParser &p)
#define FAIL(error) p.set_error(error)
  : workchain_(TlFetchInt::parse(p))
  , shard_(TlFetchLong::parse(p))
  , seqno_(TlFetchInt::parse(p))
#undef FAIL
{}

void tonNode_blockId::store(td::TlStorerCalcLength &s) const {
  (void)sizeof(s);
  TlStoreBinary::store(workchain_, s);
  TlStoreBinary::store(shard_, s);
  TlStoreBinary::store(seqno_, s);
}

void tonNode_blockId::store(td::TlStorerUnsafe &s) const {
  (void)sizeof(s);
  TlStoreBinary::store(workchain_, s);
  TlStoreBinary::store(shard_, s);
  TlStoreBinary::store(seqno_, s);
}

void tonNode_blockId::store(td::TlStorerToString &s, const char *field_name) const {
  if (!LOG_IS_STRIPPED(ERROR)) {
    s.store_class_begin(field_name, "tonNode_blockId");
    s.store_field("workchain", workchain_);
    s.store_field("shard", shard_);
    s.store_field("seqno", seqno_);
    s.store_class_end();
  }
}

tonNode_blockIdExt::tonNode_blockIdExt()
  : workchain_()
  , shard_()
  , seqno_()
  , root_hash_()
  , file_hash_()
{}

tonNode_blockIdExt::tonNode_blockIdExt(std::int32_t workchain_, std::int64_t shard_, std::int32_t seqno_, td::Bits256 const &root_hash_, td::Bits256 const &file_hash_)
  : workchain_(workchain_)
  , shard_(shard_)
  , seqno_(seqno_)
  , root_hash_(root_hash_)
  , file_hash_(file_hash_)
{}

const std::int32_t tonNode_blockIdExt::ID;

object_ptr<tonNode_blockIdExt> tonNode_blockIdExt::fetch(td::TlParser &p) {
  return make_object<tonNode_blockIdExt>(p);
}

tonNode_blockIdExt::tonNode_blockIdExt(td::TlParser &p)
#define FAIL(error) p.set_error(error)
  : workchain_(TlFetchInt::parse(p))
  , shard_(TlFetchLong::parse(p))
  , seqno_(TlFetchInt::parse(p))
  , root_hash_(TlFetchInt256::parse(p))
  , file_hash_(TlFetchInt256::parse(p))
#undef FAIL
{}

void tonNode_blockIdExt::store(td::TlStorerCalcLength &s) const {
  (void)sizeof(s);
  TlStoreBinary::store(workchain_, s);
  TlStoreBinary::store(shard_, s);
  TlStoreBinary::store(seqno_, s);
  TlStoreBinary::store(root_hash_, s);
  TlStoreBinary::store(file_hash_, s);
}

void tonNode_blockIdExt::store(td::TlStorerUnsafe &s) const {
  (void)sizeof(s);
  TlStoreBinary::store(workchain_, s);
  TlStoreBinary::store(shard_, s);
  TlStoreBinary::store(seqno_, s);
  TlStoreBinary::store(root_hash_, s);
  TlStoreBinary::store(file_hash_, s);
}

void tonNode_blockIdExt::store(td::TlStorerToString &s, const char *field_name) const {
  if (!LOG_IS_STRIPPED(ERROR)) {
    s.store_class_begin(field_name, "tonNode_blockIdExt");
    s.store_field("workchain", workchain_);
    s.store_field("shard", shard_);
    s.store_field("seqno", seqno_);
    s.store_field("root_hash", root_hash_);
    s.store_field("file_hash", file_hash_);
    s.store_class_end();
  }
}

tonNode_shardId::tonNode_shardId()
  : workchain_()
  , shard_()
{}

tonNode_shardId::tonNode_shardId(std::int32_t workchain_, std::int64_t shard_)
  : workchain_(workchain_)
  , shard_(shard_)
{}

const std::int32_t tonNode_shardId::ID;

object_ptr<tonNode_shardId> tonNode_shardId::fetch(td::TlParser &p) {
  return make_object<tonNode_shardId>(p);
}

tonNode_shardId::tonNode_shardId(td::TlParser &p)
#define FAIL(error) p.set_error(error)
  : workchain_(TlFetchInt::parse(p))
  , shard_(TlFetchLong::parse(p))
#undef FAIL
{}

void tonNode_shardId::store(td::TlStorerCalcLength &s) const {
  (void)sizeof(s);
  TlStoreBinary::store(workchain_, s);
  TlStoreBinary::store(shard_, s);
}

void tonNode_shardId::store(td::TlStorerUnsafe &s) const {
  (void)sizeof(s);
  TlStoreBinary::store(workchain_, s);
  TlStoreBinary::store(shard_, s);
}

void tonNode_shardId::store(td::TlStorerToString &s, const char *field_name) const {
  if (!LOG_IS_STRIPPED(ERROR)) {
    s.store_class_begin(field_name, "tonNode_shardId");
    s.store_field("workchain", workchain_);
    s.store_field("shard", shard_);
    s.store_class_end();
  }
}

tonNode_zeroStateIdExt::tonNode_zeroStateIdExt()
  : workchain_()
  , root_hash_()
  , file_hash_()
{}

tonNode_zeroStateIdExt::tonNode_zeroStateIdExt(std::int32_t workchain_, td::Bits256 const &root_hash_, td::Bits256 const &file_hash_)
  : workchain_(workchain_)
  , root_hash_(root_hash_)
  , file_hash_(file_hash_)
{}

const std::int32_t tonNode_zeroStateIdExt::ID;

object_ptr<tonNode_zeroStateIdExt> tonNode_zeroStateIdExt::fetch(td::TlParser &p) {
  return make_object<tonNode_zeroStateIdExt>(p);
}

tonNode_zeroStateIdExt::tonNode_zeroStateIdExt(td::TlParser &p)
#define FAIL(error) p.set_error(error)
  : workchain_(TlFetchInt::parse(p))
  , root_hash_(TlFetchInt256::parse(p))
  , file_hash_(TlFetchInt256::parse(p))
#undef FAIL
{}

void tonNode_zeroStateIdExt::store(td::TlStorerCalcLength &s) const {
  (void)sizeof(s);
  TlStoreBinary::store(workchain_, s);
  TlStoreBinary::store(root_hash_, s);
  TlStoreBinary::store(file_hash_, s);
}

void tonNode_zeroStateIdExt::store(td::TlStorerUnsafe &s) const {
  (void)sizeof(s);
  TlStoreBinary::store(workchain_, s);
  TlStoreBinary::store(root_hash_, s);
  TlStoreBinary::store(file_hash_, s);
}

void tonNode_zeroStateIdExt::store(td::TlStorerToString &s, const char *field_name) const {
  if (!LOG_IS_STRIPPED(ERROR)) {
    s.store_class_begin(field_name, "tonNode_zeroStateIdExt");
    s.store_field("workchain", workchain_);
    s.store_field("root_hash", root_hash_);
    s.store_field("file_hash", file_hash_);
    s.store_class_end();
  }
}
}  // namespace ton_api
}  // namespace ton
