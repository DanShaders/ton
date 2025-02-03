
#pragma once

/// \file Thread Local Policy: ref counting.

namespace td {
namespace tl_policies {
namespace ref_cnt {

class Policy {
 private:
  Policy() = default;

  static bool& instance() {
    static TD_THREAD_LOCAL bool use_ts_cnt = true;
    return use_ts_cnt;
  }

 public:
  static bool& get() {
    return instance();
  }

  static void set(bool use_ts_cnt) {
    instance() = use_ts_cnt;
  }
};

struct PolicyHolder {
  explicit PolicyHolder(bool use_ts_cnt = false) {
    Policy::set(use_ts_cnt);
  }
  ~PolicyHolder() {
    Policy::set(prev_);
  }

 private:
  bool prev_ = Policy::get();
};

}  // namespace ref_cnt
}  // namespace tl_policies
}  // namespace td
