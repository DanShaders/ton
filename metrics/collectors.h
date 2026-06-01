/*
 * Copyright (c) 2026, TON CORE TECHNOLOGIES CO. L.L.C
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "td/utils/Badge.h"
#include "td/utils/check.h"
#include "td/utils/int_types.h"
#include "td/utils/logging.h"

#include "types.h"

namespace ton::metrics {

template <size_t n>
struct FixedString {
  char data[n]{};

  consteval FixedString(const char (&s)[n]) {
    std::copy_n(s, n, data);
  }

  constexpr std::string_view view() const {
    return {data, n - 1};
  }
};

// ===== Labels =====
template <typename E, size_t n>
struct LabelEnum {  // contiguous, zero-based enum
  static constexpr size_t size = n;

  std::string_view key;
  std::array<std::string_view, n> names;

  constexpr size_t index(E v) const {
    return static_cast<size_t>(v);
  }

  constexpr std::string_view name_at(size_t i) const {
    return names[i];
  }
};

template <typename V, size_t n>
struct LabelValues {  // explicit {value, name} list (e.g. workchain 0/-1)
  static constexpr size_t size = n;

  std::string_view key;
  std::array<std::pair<V, std::string_view>, n> entries;

  constexpr size_t index(V v) const {
    for (size_t i = 0; i < n; ++i) {
      if (entries[i].first == v) {
        return i;
      }
    }
    return n;
  }

  constexpr std::string_view name_at(size_t i) const {
    return entries[i].second;
  }
};

template <typename L>
concept BoundedLabel = requires(L v) { ton_metric_label(v); };

template <BoundedLabel L>
using LabelDomainOf = decltype(ton_metric_label(std::declval<L>()));

template <BoundedLabel L>
inline constexpr auto label_domain = ton_metric_label(L{});

#define TON_METRIC_LABEL_ENUMERATOR_(name) name,
#define TON_METRIC_LABEL_COUNT_(name) +1
#define TON_METRIC_LABEL_NAME_(name) ::std::string_view{#name},
#define TON_METRIC_LABEL_FIELD_(name) T name{};
#define TON_METRIC_LABEL_MOVE_(name) ::std::move(name),

// Defines a contiguous, zero-based label enum plus:
//   - ton_metric_label(Type): the LabelEnum descriptor (key + enumerator names),
//   - Type##Axis<T>: an aggregate with one `T` member per enumerator, and ton_metric_axis(Type, ...),
//     which together let Labeled<Inner, Type, ...> be built from nested designated initializers
//     (e.g. Labeled<Foo, Direction, Kind>{{.in = {.message = a, .query = b}}}).
#define TON_METRIC_DEFINE_LABEL(Type, KeyStr, LIST)                                                    \
  enum class Type : ::size_t { LIST(TON_METRIC_LABEL_ENUMERATOR_) };                                   \
  [[maybe_unused]] inline constexpr ::ton::metrics::LabelEnum<Type, (0 LIST(TON_METRIC_LABEL_COUNT_))> \
  ton_metric_label(Type) {                                                                             \
    return {::std::string_view{KeyStr}, {{LIST(TON_METRIC_LABEL_NAME_)}}};                             \
  }                                                                                                    \
  template <class T>                                                                                   \
  struct Type##Axis {                                                                                  \
    LIST(TON_METRIC_LABEL_FIELD_)                                                                       \
    ::std::array<T, (0 LIST(TON_METRIC_LABEL_COUNT_))> as_array() && {                                 \
      return {{LIST(TON_METRIC_LABEL_MOVE_)}};                                                          \
    }                                                                                                  \
  };                                                                                                   \
  template <class T>                                                                                   \
  Type##Axis<T> ton_metric_axis(Type, ::ton::metrics::AxisTag<T>);

// Carries the (inner) cell type to the ADL ton_metric_axis hook above.
template <class T>
struct AxisTag {};

// LabelAxisT<Inner, L0, L1, ...> = <L0>Axis<<L1>Axis<...<Inner>>>: the nested designated-init
// aggregate accepted by Labeled's converting constructor (innermost leaf is Inner itself).
template <class Inner, BoundedLabel... Labels>
struct LabelAxis {
  using type = Inner;
};
template <class Inner, BoundedLabel L0, BoundedLabel... Ls>
struct LabelAxis<Inner, L0, Ls...> {
  using type = decltype(ton_metric_axis(L0{}, AxisTag<typename LabelAxis<Inner, Ls...>::type>{}));
};
template <class Inner, BoundedLabel... Labels>
using LabelAxisT = typename LabelAxis<Inner, Labels...>::type;

// Flattens a nested axis aggregate into a row-major cell block (first label most significant), exactly
// matching Labeled::flat_index.
template <class Inner, BoundedLabel... Labels>
struct LabelScatter {  // leaf: no labels left, the axis node is the cell value itself
  static void run(Inner *out, Inner &&value) {
    *out = std::move(value);
  }
};
template <class Inner, BoundedLabel L0, BoundedLabel... Ls>
struct LabelScatter<Inner, L0, Ls...> {
  static constexpr size_t block = (size_t{1} * ... * LabelDomainOf<Ls>::size);
  template <class Axis>
  static void run(Inner *out, Axis &&axis) {
    auto cells = std::move(axis).as_array();
    for (size_t i = 0; i < LabelDomainOf<L0>::size; ++i) {
      LabelScatter<Inner, Ls...>::run(out + i * block, std::move(cells[i]));
    }
  }
};

// ===== Sink / Context =====
class Sink {
 public:
  void open_family(std::string name, std::string_view type, std::string_view sample_suffix) {
    if (pos_ == families_.size()) {
      families_.push_back(MetricFamily{
          .name = std::move(name),
          .type = std::string(type),
          .help = std::nullopt,
          .metrics = {},
      });
    } else {
      CHECK(families_[pos_].name == name && families_[pos_].type == type);
    }
    current_ = pos_++;
    sample_suffix_ = sample_suffix;
  }

  void push_sample(LabelSet labels, double value) {
    families_[current_].metrics.push_back(Metric{
        .suffix = std::string(sample_suffix_),
        .label_set = std::move(labels),
        .samples = {Sample{.label_set = {}, .value = value}},
    });
  }

  size_t mark() const {
    return pos_;
  }

  void rewind(size_t m) {
    pos_ = m;
  }

  MetricSet build() && {
    return MetricSet{.families = std::move(families_)};
  }

 private:
  std::vector<MetricFamily> families_;
  std::string_view sample_suffix_;
  size_t pos_ = 0;
  size_t current_ = 0;
};

struct LabelStack {
  const LabelStack *parent = nullptr;
  std::string_view key;
  std::string_view value;
};

struct NameStack {
  const NameStack *parent = nullptr;
  std::string_view segment;
};

template <typename T>
concept Named = requires {
  { T::name() } -> std::convertible_to<std::string_view>;
};

class Context;

template <typename T>
concept Collectable =
    requires(const T node, Context ctx, td::Badge<Context> badge) { node.collect(std::move(badge), ctx); };

class ContextWithLabelLink;
class ContextWithNameLink;

class Context {
 public:
  explicit Context(Sink &sink) : sink_(sink) {
  }

  template <Collectable Node>
  void collect(const Node &node) const {
    if constexpr (Named<Node>) {
      NameStack link{.parent = names_, .segment = Node::name()};
      node.collect(td::Badge<Context>{}, Context{sink_, labels_, &link});
    } else {
      node.collect(td::Badge<Context>{}, *this);
    }
  }

  ContextWithLabelLink with_label(std::string_view key, std::string_view value) const;
  ContextWithNameLink with_name(std::string_view segment) const;

  size_t mark() const {
    return sink_.mark();
  }

  void rewind(size_t m) const {
    sink_.rewind(m);
  }

  void open_family(std::string_view type, std::string_view sample_suffix = "") const {
    std::string name;
    append_name(name, names_);
    sink_.open_family(std::move(name), type, sample_suffix);
  }

  void push(double value) const {
    sink_.push_sample(materialize_labels(), value);
  }

 private:
  friend class ContextWithLabelLink;
  friend class ContextWithNameLink;

  Context(Sink &sink, const LabelStack *labels, const NameStack *names) : sink_(sink), labels_(labels), names_(names) {
  }

  static void append_name(std::string &out, const NameStack *p) {
    if (p == nullptr) {
      return;
    }
    append_name(out, p->parent);  // outermost segment first
    if (!out.empty()) {
      out += '_';
    }
    out += p->segment;
  }

  LabelSet materialize_labels() const {
    LabelSet ls;
    for (const LabelStack *p = labels_; p != nullptr; p = p->parent) {
      ls.labels.push_back(Label{.key = std::string(p->key), .val = std::string(p->value)});
    }
    std::reverse(ls.labels.begin(), ls.labels.end());  // outermost label first
    return ls;
  }

  Sink &sink_;
  const LabelStack *labels_ = nullptr;
  const NameStack *names_ = nullptr;
};

class ContextWithNameLink : public Context {
 private:
  friend class Context;

  ContextWithNameLink(Sink &sink, const LabelStack *labels, const NameStack *names, std::string_view segment)
      : Context(sink, labels, &link), link{.parent = names, .segment = segment} {
  }

  NameStack link;
};

inline ContextWithNameLink Context::with_name(std::string_view segment) const {
  return ContextWithNameLink{sink_, labels_, names_, segment};
}

class ContextWithLabelLink : public Context {
 private:
  friend class Context;

  ContextWithLabelLink(Sink &sink, const LabelStack *labels, const NameStack *names, std::string_view key,
                       std::string_view value)
      : Context(sink, &link, names), link{.parent = labels, .key = key, .value = value} {
  }

  LabelStack link;
};

inline ContextWithLabelLink Context::with_label(std::string_view key, std::string_view value) const {
  return ContextWithLabelLink{sink_, labels_, names_, key, value};
}

// ===== Wrappers =====
template <Collectable Inner, BoundedLabel... Labels>
class Labeled {
  static constexpr size_t dims = sizeof...(Labels);
  static_assert(dims >= 1, "Labeled needs at least one label");
  static constexpr size_t cells = (size_t{1} * ... * LabelDomainOf<Labels>::size);

 public:
  Labeled() = default;

  // Construct from nested designated initializers, one brace level per label:
  //   Labeled<Foo, Direction, Kind>{{.in = {.message = a, .query = b}}}
  // Omitted cells are value-initialized. Templated with a defaulted axis type so it is instantiated
  // only when actually used, keeping Labeled usable with label kinds that provide no axis aggregate.
  template <class Axis = LabelAxisT<Inner, Labels...>>
  Labeled(Axis axis) {
    LabelScatter<Inner, Labels...>::run(cells_.data(), std::move(axis));
  }

  Inner &at(Labels... vs) & {
    return cells_[flat_index(vs...)];
  }

  const Inner &at(Labels... vs) const & {
    return cells_[flat_index(vs...)];
  }

  void collect(td::Badge<Context>, Context ctx) const {
    size_t start = ctx.mark();  // where this group's families begin
    for (size_t flat = 0; flat < cells; ++flat) {
      ctx.rewind(start);  // each cell re-emits the same family sequence into the same slots
      collect_cell<0>(ctx, unflatten(flat), flat);
    }
  }

  // Cell-wise merge — used to aggregate counter trees scraped from multiple actors.
  Labeled &operator+=(const Labeled &other) {
    for (size_t i = 0; i < cells; ++i) {
      cells_[i] += other.cells_[i];
    }
    return *this;
  }

 private:
  template <size_t D>
  void collect_cell(const Context &ctx, const std::array<size_t, dims> &idx, size_t flat) const {
    if constexpr (D == dims) {
      ctx.collect(cells_[flat]);
    } else {
      using L = std::tuple_element_t<D, std::tuple<Labels...>>;
      auto labeled = ctx.with_label(label_domain<L>.key, label_domain<L>.name_at(idx[D]));
      collect_cell<D + 1>(labeled, idx, flat);
    }
  }

  static size_t flat_index(Labels... vs) {
    size_t idx = 0;
    ((idx = idx * LabelDomainOf<Labels>::size + label_domain<Labels>.index(vs)), ...);
    return idx;
  }

  static std::array<size_t, dims> unflatten(size_t flat) {
    constexpr std::array<size_t, dims> sizes{LabelDomainOf<Labels>::size...};
    std::array<size_t, dims> idx{};
    for (size_t d = dims; d-- > 0;) {  // row-major
      idx[d] = flat % sizes[d];
      flat /= sizes[d];
    }
    return idx;
  }

  std::array<Inner, cells> cells_{};
};

// DynLabel: like Labeled, but for an open/dynamic value set — one inner per value
// seen at runtime, stored in a hash map and rendered with `PSTRING() << value`.
// `Key` is the label key; `Value` must be hashable and streamable.
template <FixedString Key, typename Value, Collectable Inner>
class DynLabel {
 public:
  Inner &at(const Value &value) & {
    return values_[value];
  }

  void collect(td::Badge<Context>, Context ctx) const {
    size_t start = ctx.mark();
    for (const auto &[value, inner] : values_) {
      ctx.rewind(start);
      ctx.with_label(Key.view(), PSTRING() << value).collect(inner);
    }
  }

 private:
  std::unordered_map<Value, Inner> values_;
};

// Prefixed: prepend a name segment to ANY inner node's families.
template <FixedString Name, Collectable Inner>
class Prefixed {
 public:
  Prefixed() = default;

  Prefixed(const Inner &inner) : inner_(inner) {
  }

  Inner *operator->() & {
    return &inner_;
  }

  const Inner *operator->() const & {
    return &inner_;
  }

  Inner &operator*() & {
    return inner_;
  }

  const Inner &operator*() const & {
    return inner_;
  }

  static constexpr std::string_view name() {
    return Name.view();
  }

  void collect(td::Badge<Context>, Context ctx) const {
    ctx.collect(inner_);
  }

  Prefixed &operator+=(const Prefixed &other) {
    inner_ += other.inner_;
    return *this;
  }

 private:
  Inner inner_;
};

// ===== Leaf metric nodes — own name + type; plain value storage =====
template <FixedString Name>
class Counter {
 public:
  Counter() = default;
  Counter(td::uint64 value) : value_(value) {
  }

  void inc(td::uint64 delta = 1) {
    value_ += delta;
  }

  td::uint64 value() const {
    return value_;
  }

  static constexpr std::string_view name() {
    return Name.view();
  }

  void collect(td::Badge<Context>, Context ctx) const {
    ctx.open_family("counter", "total");
    ctx.push(static_cast<double>(value_));
  }

  Counter &operator+=(const Counter &other) {
    value_ += other.value_;
    return *this;
  }

 private:
  td::uint64 value_ = 0;
};

template <FixedString Name, typename T = double>
class Gauge {
 public:
  Gauge() = default;
  Gauge(T value) : value_(value) {
  }

  void set(T value) {
    value_ = value;
  }

  void add(T delta) {
    value_ += delta;
  }

  T value() const {
    return value_;
  }

  static constexpr std::string_view name() {
    return Name.view();
  }

  void collect(td::Badge<Context>, Context ctx) const {
    ctx.open_family("gauge");
    ctx.push(static_cast<double>(value_));
  }

 private:
  T value_ = {};
};

}  // namespace ton::metrics
