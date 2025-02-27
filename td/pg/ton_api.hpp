#pragma once

#include "ton_api.h"

namespace ton {
namespace ton_api {

/**
 * Calls specified function object with the specified object downcasted to the most-derived type.
 * \param[in] obj Object to pass as an argument to the function object.
 * \param[in] func Function object to which the object will be passed.
 * \returns whether function object call has happened. Should always return true for correct parameters.
 */
template <class T>
bool downcast_call(Object &obj, const T &func) {
  switch (obj.get_id()) {
    case pk_unenc::ID:
      func(static_cast<pk_unenc &>(obj));
      return true;
    case pk_ed25519::ID:
      func(static_cast<pk_ed25519 &>(obj));
      return true;
    case pk_aes::ID:
      func(static_cast<pk_aes &>(obj));
      return true;
    case pk_overlay::ID:
      func(static_cast<pk_overlay &>(obj));
      return true;
    case pub_unenc::ID:
      func(static_cast<pub_unenc &>(obj));
      return true;
    case pub_ed25519::ID:
      func(static_cast<pub_ed25519 &>(obj));
      return true;
    case pub_aes::ID:
      func(static_cast<pub_aes &>(obj));
      return true;
    case pub_overlay::ID:
      func(static_cast<pub_overlay &>(obj));
      return true;
    case contest_test::ID:
      func(static_cast<contest_test &>(obj));
      return true;
    case tonNode_blockId::ID:
      func(static_cast<tonNode_blockId &>(obj));
      return true;
    case tonNode_blockIdExt::ID:
      func(static_cast<tonNode_blockIdExt &>(obj));
      return true;
    case tonNode_shardId::ID:
      func(static_cast<tonNode_shardId &>(obj));
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
switch (obj.get_id()) {    case pk_unenc::ID:
      func(create_tl_object<pk_unenc>());
      return true;
    case pk_ed25519::ID:
      func(create_tl_object<pk_ed25519>());
      return true;
    case pk_aes::ID:
      func(create_tl_object<pk_aes>());
      return true;
    case pk_overlay::ID:
      func(create_tl_object<pk_overlay>());
      return true;
    case pub_unenc::ID:
      func(create_tl_object<pub_unenc>());
      return true;
    case pub_ed25519::ID:
      func(create_tl_object<pub_ed25519>());
      return true;
    case pub_aes::ID:
      func(create_tl_object<pub_aes>());
      return true;
    case pub_overlay::ID:
      func(create_tl_object<pub_overlay>());
      return true;
    case contest_test::ID:
      func(create_tl_object<contest_test>());
      return true;
    case tonNode_blockId::ID:
      func(create_tl_object<tonNode_blockId>());
      return true;
    case tonNode_blockIdExt::ID:
      func(create_tl_object<tonNode_blockIdExt>());
      return true;
    case tonNode_shardId::ID:
      func(create_tl_object<tonNode_shardId>());
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
switch (obj.get_id()) {    default:
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
bool downcast_call(PrivateKey &obj, const T &func) {
  switch (obj.get_id()) {
    case pk_unenc::ID:
      func(static_cast<pk_unenc &>(obj));
      return true;
    case pk_ed25519::ID:
      func(static_cast<pk_ed25519 &>(obj));
      return true;
    case pk_aes::ID:
      func(static_cast<pk_aes &>(obj));
      return true;
    case pk_overlay::ID:
      func(static_cast<pk_overlay &>(obj));
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
bool downcast_construct(PrivateKey &obj, const T &func) {
switch (obj.get_id()) {    case pk_unenc::ID:
      func(create_tl_object<pk_unenc>());
      return true;
    case pk_ed25519::ID:
      func(create_tl_object<pk_ed25519>());
      return true;
    case pk_aes::ID:
      func(create_tl_object<pk_aes>());
      return true;
    case pk_overlay::ID:
      func(create_tl_object<pk_overlay>());
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
bool downcast_call(PublicKey &obj, const T &func) {
  switch (obj.get_id()) {
    case pub_unenc::ID:
      func(static_cast<pub_unenc &>(obj));
      return true;
    case pub_ed25519::ID:
      func(static_cast<pub_ed25519 &>(obj));
      return true;
    case pub_aes::ID:
      func(static_cast<pub_aes &>(obj));
      return true;
    case pub_overlay::ID:
      func(static_cast<pub_overlay &>(obj));
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
bool downcast_construct(PublicKey &obj, const T &func) {
switch (obj.get_id()) {    case pub_unenc::ID:
      func(create_tl_object<pub_unenc>());
      return true;
    case pub_ed25519::ID:
      func(create_tl_object<pub_ed25519>());
      return true;
    case pub_aes::ID:
      func(create_tl_object<pub_aes>());
      return true;
    case pub_overlay::ID:
      func(create_tl_object<pub_overlay>());
      return true;
    default:
      return false;
  }
}

}  // namespace ton_api
}  // namespace ton 
