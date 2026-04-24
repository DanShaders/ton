#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "td/utils/Slice.h"
#include "td/utils/int_types.h"

#include "metrics-types.h"

namespace ton::metrics {

// Per-TL-constructor traffic counter. Use one instance per (layer, kind) pair (e.g. rldp send/query/...).
//
// Keys are TL constructor magics. Overlay wrappers (overlay.query / overlay.message and their
// WithExtra variants) are transparently looked through: the inner object's magic is used so the
// outer wrapper never collapses every query into a single "overlay.query" bucket. Unknown
// magics — including wire garbage and short payloads — funnel into `unknown` so cardinality
// stays bounded by the schema size.
struct TlTrafficBucket {
  struct Counter {
    td::uint64 bytes = 0;
    td::uint64 msgs = 0;
  };
  std::map<td::int32, Counter> by_magic;
  Counter unknown;

  // Increment counters for `data` (a serialized TL object). Handles overlay-wrapped queries and
  // messages by decoding the inner magic; see class comment.
  void account(td::Slice data);

  // Same accounting as account() but with the magic already extracted.  Use this when the
  // accounting happens in a different actor from the one that owns the BufferSlice — pass
  // (magic, size) through send_closure rather than copying the whole payload.  Pass magic == 0
  // (or any other unknown value) to force the unknown bucket.
  void account_with_magic(td::int32 magic, td::uint64 size);
};

// Append rows to `<base>_bytes_by_tl_total` and `<base>_messages_by_tl_total` families in `set`,
// each row labeled `extra_labels` plus `tl=<schema_name|"unknown">`. If a family with the target
// name already exists in `set`, rows are appended to it; otherwise the family is created (and
// `type=counter` + the provided help are set on first creation).
void render_tl_bucket(MetricSet &set, const std::string &base, const TlTrafficBucket &bucket, LabelSet extra_labels,
                      std::optional<std::string> bytes_help = std::nullopt,
                      std::optional<std::string> messages_help = std::nullopt);

}  // namespace ton::metrics
