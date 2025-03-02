#pragma once

#include "TlObject.h"

#include "utils/int_types.h"

#include <string>
#include "utils/buffer.h"
#include "crypto/common/bitstring.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace td {
class TlStorerCalcLength;
}  // namespace td
namespace td {
class TlStorerUnsafe;
}  // namespace td
namespace td {
class TlStorerToString;
}  // namespace td
namespace td {
class TlParser;
}  // namespace td

namespace ton {
namespace ton_api{
using BaseObject = ::ton::TlObject;

template <class Type>
using object_ptr = ::ton::tl_object_ptr<Type>;

template <class Type, class... Args>
object_ptr<Type> make_object(Args &&... args) {
  return object_ptr<Type>(new Type(std::forward<Args>(args)...));
}

template <class ToType, class FromType>
object_ptr<ToType> move_object_as(FromType &&from) {
  return object_ptr<ToType>(static_cast<ToType *>(from.release()));
}

std::string to_string(const BaseObject &value);

template <class T>
std::string to_string(const object_ptr<T> &value) {
  if (value == nullptr) {
    return "null";
  }

  return to_string(*value);
}

class PrivateKey;

class PublicKey;

class contest_test;

class tonNode_blockId;

class tonNode_blockIdExt;

class tonNode_shardId;

class tonNode_zeroStateIdExt;

class Object;

class Object: public TlObject {
 public:

  static object_ptr<Object> fetch(td::TlParser &p);
};

class Function: public TlObject {
 public:

  static object_ptr<Function> fetch(td::TlParser &p);
};

class PrivateKey: public Object {
 public:

  static object_ptr<PrivateKey> fetch(td::TlParser &p);
};

class pk_unenc final : public PrivateKey {
 public:
  td::BufferSlice data_;

  pk_unenc();

  explicit pk_unenc(td::BufferSlice &&data_);

  static const std::int32_t ID = -1311007952;
  std::int32_t get_id() const final {
    return ID;
  }

  static object_ptr<PrivateKey> fetch(td::TlParser &p);

  explicit pk_unenc(td::TlParser &p);

  void store(td::TlStorerCalcLength &s) const final;

  void store(td::TlStorerUnsafe &s) const final;

  void store(td::TlStorerToString &s, const char *field_name) const final;
};

class pk_ed25519 final : public PrivateKey {
 public:
  td::Bits256 key_;

  pk_ed25519();

  explicit pk_ed25519(td::Bits256 const &key_);

  static const std::int32_t ID = 1231561495;
  std::int32_t get_id() const final {
    return ID;
  }

  static object_ptr<PrivateKey> fetch(td::TlParser &p);

  explicit pk_ed25519(td::TlParser &p);

  void store(td::TlStorerCalcLength &s) const final;

  void store(td::TlStorerUnsafe &s) const final;

  void store(td::TlStorerToString &s, const char *field_name) const final;
};

class pk_aes final : public PrivateKey {
 public:
  td::Bits256 key_;

  pk_aes();

  explicit pk_aes(td::Bits256 const &key_);

  static const std::int32_t ID = -1511501513;
  std::int32_t get_id() const final {
    return ID;
  }

  static object_ptr<PrivateKey> fetch(td::TlParser &p);

  explicit pk_aes(td::TlParser &p);

  void store(td::TlStorerCalcLength &s) const final;

  void store(td::TlStorerUnsafe &s) const final;

  void store(td::TlStorerToString &s, const char *field_name) const final;
};

class pk_overlay final : public PrivateKey {
 public:
  td::BufferSlice name_;

  pk_overlay();

  explicit pk_overlay(td::BufferSlice &&name_);

  static const std::int32_t ID = 933623387;
  std::int32_t get_id() const final {
    return ID;
  }

  static object_ptr<PrivateKey> fetch(td::TlParser &p);

  explicit pk_overlay(td::TlParser &p);

  void store(td::TlStorerCalcLength &s) const final;

  void store(td::TlStorerUnsafe &s) const final;

  void store(td::TlStorerToString &s, const char *field_name) const final;
};

class PublicKey: public Object {
 public:

  static object_ptr<PublicKey> fetch(td::TlParser &p);
};

class pub_unenc final : public PublicKey {
 public:
  td::BufferSlice data_;

  pub_unenc();

  explicit pub_unenc(td::BufferSlice &&data_);

  static const std::int32_t ID = -1239464694;
  std::int32_t get_id() const final {
    return ID;
  }

  static object_ptr<PublicKey> fetch(td::TlParser &p);

  explicit pub_unenc(td::TlParser &p);

  void store(td::TlStorerCalcLength &s) const final;

  void store(td::TlStorerUnsafe &s) const final;

  void store(td::TlStorerToString &s, const char *field_name) const final;
};

class pub_ed25519 final : public PublicKey {
 public:
  td::Bits256 key_;

  pub_ed25519();

  explicit pub_ed25519(td::Bits256 const &key_);

  static const std::int32_t ID = 1209251014;
  std::int32_t get_id() const final {
    return ID;
  }

  static object_ptr<PublicKey> fetch(td::TlParser &p);

  explicit pub_ed25519(td::TlParser &p);

  void store(td::TlStorerCalcLength &s) const final;

  void store(td::TlStorerUnsafe &s) const final;

  void store(td::TlStorerToString &s, const char *field_name) const final;
};

class pub_aes final : public PublicKey {
 public:
  td::Bits256 key_;

  pub_aes();

  explicit pub_aes(td::Bits256 const &key_);

  static const std::int32_t ID = 767339988;
  std::int32_t get_id() const final {
    return ID;
  }

  static object_ptr<PublicKey> fetch(td::TlParser &p);

  explicit pub_aes(td::TlParser &p);

  void store(td::TlStorerCalcLength &s) const final;

  void store(td::TlStorerUnsafe &s) const final;

  void store(td::TlStorerToString &s, const char *field_name) const final;
};

class pub_overlay final : public PublicKey {
 public:
  td::BufferSlice name_;

  pub_overlay();

  explicit pub_overlay(td::BufferSlice &&name_);

  static const std::int32_t ID = 884622795;
  std::int32_t get_id() const final {
    return ID;
  }

  static object_ptr<PublicKey> fetch(td::TlParser &p);

  explicit pub_overlay(td::TlParser &p);

  void store(td::TlStorerCalcLength &s) const final;

  void store(td::TlStorerUnsafe &s) const final;

  void store(td::TlStorerToString &s, const char *field_name) const final;
};

class contest_test final : public Object {
 public:
  object_ptr<tonNode_blockIdExt> block_id_;
  td::BufferSlice block_data_;
  td::BufferSlice collated_data_;
  bool valid_;

  contest_test();

  contest_test(object_ptr<tonNode_blockIdExt> &&block_id_, td::BufferSlice &&block_data_, td::BufferSlice &&collated_data_, bool valid_);

  static const std::int32_t ID = 935309609;
  std::int32_t get_id() const final {
    return ID;
  }

  static object_ptr<contest_test> fetch(td::TlParser &p);

  explicit contest_test(td::TlParser &p);

  void store(td::TlStorerCalcLength &s) const final;

  void store(td::TlStorerUnsafe &s) const final;

  void store(td::TlStorerToString &s, const char *field_name) const final;
};

class tonNode_blockId final : public Object {
 public:
  std::int32_t workchain_;
  std::int64_t shard_;
  std::int32_t seqno_;

  tonNode_blockId();

  tonNode_blockId(std::int32_t workchain_, std::int64_t shard_, std::int32_t seqno_);

  static const std::int32_t ID = -1211256473;
  std::int32_t get_id() const final {
    return ID;
  }

  static object_ptr<tonNode_blockId> fetch(td::TlParser &p);

  explicit tonNode_blockId(td::TlParser &p);

  void store(td::TlStorerCalcLength &s) const final;

  void store(td::TlStorerUnsafe &s) const final;

  void store(td::TlStorerToString &s, const char *field_name) const final;
};

class tonNode_blockIdExt final : public Object {
 public:
  std::int32_t workchain_;
  std::int64_t shard_;
  std::int32_t seqno_;
  td::Bits256 root_hash_;
  td::Bits256 file_hash_;

  tonNode_blockIdExt();

  tonNode_blockIdExt(std::int32_t workchain_, std::int64_t shard_, std::int32_t seqno_, td::Bits256 const &root_hash_, td::Bits256 const &file_hash_);

  static const std::int32_t ID = 1733487480;
  std::int32_t get_id() const final {
    return ID;
  }

  static object_ptr<tonNode_blockIdExt> fetch(td::TlParser &p);

  explicit tonNode_blockIdExt(td::TlParser &p);

  void store(td::TlStorerCalcLength &s) const final;

  void store(td::TlStorerUnsafe &s) const final;

  void store(td::TlStorerToString &s, const char *field_name) const final;
};

class tonNode_shardId final : public Object {
 public:
  std::int32_t workchain_;
  std::int64_t shard_;

  tonNode_shardId();

  tonNode_shardId(std::int32_t workchain_, std::int64_t shard_);

  static const std::int32_t ID = 687474807;
  std::int32_t get_id() const final {
    return ID;
  }

  static object_ptr<tonNode_shardId> fetch(td::TlParser &p);

  explicit tonNode_shardId(td::TlParser &p);

  void store(td::TlStorerCalcLength &s) const final;

  void store(td::TlStorerUnsafe &s) const final;

  void store(td::TlStorerToString &s, const char *field_name) const final;
};

class tonNode_zeroStateIdExt final : public Object {
 public:
  std::int32_t workchain_;
  td::Bits256 root_hash_;
  td::Bits256 file_hash_;

  tonNode_zeroStateIdExt();

  tonNode_zeroStateIdExt(std::int32_t workchain_, td::Bits256 const &root_hash_, td::Bits256 const &file_hash_);

  static const std::int32_t ID = 494024110;
  std::int32_t get_id() const final {
    return ID;
  }

  static object_ptr<tonNode_zeroStateIdExt> fetch(td::TlParser &p);

  explicit tonNode_zeroStateIdExt(td::TlParser &p);

  void store(td::TlStorerCalcLength &s) const final;

  void store(td::TlStorerUnsafe &s) const final;

  void store(td::TlStorerToString &s, const char *field_name) const final;
};

}  // namespace ton_api
}  // namespace ton
