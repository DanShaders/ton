/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <string>
#include <string_view>
#include <tuple>

#include "td/utils/StringBuilder.h"
#include "td/utils/type_traits.h"

namespace td::actor {

namespace detail {

template <auto Mptr>
struct Field {
  static constexpr auto pointer = Mptr;
  std::string_view name;
};

template <auto Mptr>
struct field_tag {};

}  // namespace detail

#define TD_INT_PARENS ()

#define TD_INT_EXPAND(...) TD_INT_EXPAND4(TD_INT_EXPAND4(TD_INT_EXPAND4(TD_INT_EXPAND4(__VA_ARGS__))))
#define TD_INT_EXPAND4(...) TD_INT_EXPAND3(TD_INT_EXPAND3(TD_INT_EXPAND3(TD_INT_EXPAND3(__VA_ARGS__))))
#define TD_INT_EXPAND3(...) TD_INT_EXPAND2(TD_INT_EXPAND2(TD_INT_EXPAND2(TD_INT_EXPAND2(__VA_ARGS__))))
#define TD_INT_EXPAND2(...) TD_INT_EXPAND1(TD_INT_EXPAND1(TD_INT_EXPAND1(TD_INT_EXPAND1(__VA_ARGS__))))
#define TD_INT_EXPAND1(...) __VA_ARGS__

#define TD_INT_FOR_EACH(macro, ...) __VA_OPT__(TD_INT_EXPAND(TD_INT_FOR_EACH_HELPER(macro, __VA_ARGS__)))
#define TD_INT_FOR_EACH_HELPER(macro, a, ...) \
  macro(a) __VA_OPT__(, TD_INT_FOR_EACH_AGAIN TD_INT_PARENS(macro, __VA_ARGS__))
#define TD_INT_FOR_EACH_AGAIN() TD_INT_FOR_EACH_HELPER

#define TD_INT_RUNTIME_FIELD(name) ::td::actor::detail::Field<&_self::name>(#name)
#define TON_RUNTIME_EVENT_OF(...)                                          \
  template <typename _self>                                                \
  consteval static auto field_descriptors() {                              \
    return std::tuple{TD_INT_FOR_EACH(TD_INT_RUNTIME_FIELD, __VA_ARGS__)}; \
  }

template <typename Obj>
std::string stringify_event(const Obj& obj) {
  td::StringBuilder sb;
  sb << "{";
  constexpr auto descriptors = Obj::template field_descriptors<Obj>();
  unroll<std::tuple_size_v<decltype(descriptors)>>([&](auto i) {
    if constexpr (i != 0) {
      sb << ", ";
    }
    auto descriptor = std::get<i>(descriptors);
    using FieldT = decltype(descriptor);
    constexpr auto Mptr = FieldT::pointer;
    sb << td::Slice(descriptor.name.data(), descriptor.name.size()) << "=";
    if constexpr (requires { format_field(sb, obj, detail::field_tag<Mptr>{}); }) {
      format_field(sb, obj, detail::field_tag<Mptr>{});
    } else {
      sb << obj.*Mptr;
    }
  });
  sb << "}";
  return sb.as_cslice().str();
}

}  // namespace td::actor
