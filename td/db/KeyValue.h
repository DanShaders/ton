/*
    This file is part of TON Blockchain Library.

    TON Blockchain Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    TON Blockchain Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with TON Blockchain Library.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2017-2020 Telegram Systems LLP
*/
#pragma once
#include "utils/Status.h"
#include "utils/Time.h"
#include "utils/logging.h"
#include <functional>
namespace td {
class KeyValueReader {
 public:
  virtual ~KeyValueReader() = default;
  enum class GetStatus : int32 { Ok, NotFound };

  virtual Result<GetStatus> get(Slice key, std::string &value) = 0;
  virtual Result<size_t> count(Slice prefix) = 0;
  virtual Status for_each(std::function<Status(Slice, Slice)> f) {
    return Status::Error("for_each is not supported");
  }
  virtual Status for_each_in_range (Slice begin, Slice end, std::function<Status(Slice, Slice)> f) {
    return td::Status::Error("foreach_range is not supported");
  }
};

class KeyValue : public KeyValueReader {
 public:
  virtual Status set(Slice key, Slice value) = 0;
  virtual Status erase(Slice key) = 0;

  virtual Status begin_write_batch() = 0;
  virtual Status commit_write_batch() = 0;
  virtual Status abort_write_batch() = 0;

  virtual Status begin_transaction() = 0;
  virtual Status commit_transaction() = 0;
  virtual Status abort_transaction() = 0;
  // Desctructor will abort transaction

  virtual std::unique_ptr<KeyValueReader> snapshot() = 0;

  virtual std::string stats() const {
    return "";
  }
  virtual Status flush() {
    return Status::OK();
  }
};

}  // namespace td
