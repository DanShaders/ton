/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "td/utils/StringBuilder.h"
#include "td/utils/buffer.h"
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

// =============================================================================
// events_equal: structural equality for bus events used by tests.
//
// We avoid `operator==` on the events themselves to keep test-only equality
// semantics out of production headers (many event fields are td::Ref<...> or
// virtual bases without value equality). Tests call events_equal(a, b)
// directly; the harness uses it from expect_events.
//
// Default behaviour:
//   - Types with field_descriptors recurse field-by-field.
//   - Types with operator== use ==.
//   - td::BufferSlice compares by content.
//   - std::optional / std::variant / std::vector / std::unique_ptr / td::Ref
//     recurse into the contained value.
//   - Ref / unique_ptr to types without value-equality fall back to pointer
//     identity. (Tests that need value comparison provide an events_equal_tag
//     overload in the value type's namespace; ADL picks it up.)
//
// Customization point: define `bool events_equal_tag(const T&, const T&)` in
// T's namespace. The unqualified call inside td::actor::events_equal finds it
// via ADL and overrides the default.
// =============================================================================

template <typename T>
bool events_equal(const T& a, const T& b);

namespace detail {

template <typename T>
concept HasFieldDescriptors = requires { T::template field_descriptors<T>(); };

template <typename T>
concept HasEqualityOperator = requires(const T& x) { { x == x } -> std::convertible_to<bool>; };

// Type-trait to detect td::Ref<T>-like types without including refcnt.hpp here.
// Anything exposing is_null() + get() + operator* qualifies. (td::Ref does;
// std::shared_ptr/unique_ptr handled by their own overloads.)
template <typename T>
concept IsRefLike = requires(const T& r) {
  { r.is_null() } -> std::convertible_to<bool>;
  r.get();
  *r;
};

// Forward declaration so the variants below can call it recursively.
template <typename T>
bool events_equal_dispatch(const T& a, const T& b);

inline bool events_equal_default(const td::BufferSlice& a, const td::BufferSlice& b) {
  return a.as_slice() == b.as_slice();
}

template <typename T>
bool events_equal_default(const std::optional<T>& a, const std::optional<T>& b) {
  if (a.has_value() != b.has_value()) {
    return false;
  }
  if (!a.has_value()) {
    return true;
  }
  return events_equal_dispatch(*a, *b);
}

template <typename... Ts>
bool events_equal_default(const std::variant<Ts...>& a, const std::variant<Ts...>& b) {
  if (a.index() != b.index()) {
    return false;
  }
  return std::visit(
      [&]<typename U>(const U& va) -> bool { return events_equal_dispatch(va, std::get<U>(b)); }, a);
}

template <typename T>
bool events_equal_default(const std::vector<T>& a, const std::vector<T>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (!events_equal_dispatch(a[i], b[i])) {
      return false;
    }
  }
  return true;
}

template <typename T>
concept HasValueEquality =
    HasFieldDescriptors<T> || HasEqualityOperator<T> || requires(const T& x) { events_equal_tag(x, x); };

template <typename T>
bool events_equal_default(const std::unique_ptr<T>& a, const std::unique_ptr<T>& b) {
  if (static_cast<bool>(a) != static_cast<bool>(b)) {
    return false;
  }
  if (!a) {
    return true;
  }
  if constexpr (HasValueEquality<T>) {
    return events_equal_dispatch(*a, *b);
  } else {
    return a.get() == b.get();
  }
}

template <IsRefLike R>
bool events_equal_default(const R& a, const R& b) {
  if (a.is_null() != b.is_null()) {
    return false;
  }
  if (a.is_null()) {
    return true;
  }
  using Pointee = std::remove_cvref_t<decltype(*a)>;
  if constexpr (HasValueEquality<Pointee>) {
    return events_equal_dispatch(*a, *b);
  } else {
    return a.get() == b.get();
  }
}

template <typename T>
bool events_equal_default(const T& a, const T& b) {
  if constexpr (HasFieldDescriptors<T>) {
    constexpr auto descriptors = T::template field_descriptors<T>();
    return [&]<size_t... Is>(std::index_sequence<Is...>) {
      return (events_equal_dispatch(a.*std::get<Is>(descriptors).pointer, b.*std::get<Is>(descriptors).pointer) && ...);
    }(std::make_index_sequence<std::tuple_size_v<decltype(descriptors)>>{});
  } else if constexpr (HasEqualityOperator<T>) {
    return a == b;
  } else {
    static_assert(sizeof(T) == 0,
                  "events_equal: type has no operator==, no field_descriptors, and no events_equal_tag overload");
    return false;
  }
}

template <typename T>
bool events_equal_dispatch(const T& a, const T& b) {
  if constexpr (requires { events_equal_tag(a, b); }) {
    return events_equal_tag(a, b);
  } else {
    return events_equal_default(a, b);
  }
}

}  // namespace detail

template <typename T>
bool events_equal(const T& a, const T& b) {
  return detail::events_equal_dispatch(a, b);
}

}  // namespace td::actor
