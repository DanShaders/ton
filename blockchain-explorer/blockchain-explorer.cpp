/*
    This file is part of TON Blockchain source code.

    TON Blockchain is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    TON Blockchain is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License

    In addition, as a special exception, the copyright holders give permission
    to link the code of portions of this program with the OpenSSL library.
    You must obey the GNU General Public License in all respects for all
    of the code used other than OpenSSL. If you modify file(s) with this
    exception, you may extend this exception to your version of the file(s),
    but you are not obligated to do so. If you do not wish to do so, delete this
    exception statement from your version. If you delete this exception statement
    from all source files in the program, then also delete it here.
    along with TON Blockchain.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2017-2020 Telegram Systems LLP
*/
#include "adnl/adnl-ext-client.h"
#include "adnl/utils.hpp"
#include "auto/tl/lite_api.h"
#include "auto/tl/ton_api_json.h"
#include "block/block-auto.h"
#include "block/block-db.h"
#include "block/block.h"
#include "block/mc-config.h"
#include "http/http-server.h"
#include "lite-client/ext-client.h"
#include "td/utils/OptionParser.h"
#include "td/utils/Random.h"
#include "td/utils/Time.h"
#include "td/utils/crypto.h"
#include "td/utils/filesystem.h"
#include "td/utils/format.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/signals.h"
#include "td/utils/port/user.h"
#include "tl-utils/lite-utils.hpp"
#include "ton/lite-tl.hpp"
#include "ton/ton-tl.hpp"
#include "vm/boc.h"
#include "vm/cellops.h"
#include "vm/cells/MerkleProof.h"
#include "vm/vm.h"

#include "blockchain-explorer-http.hpp"
#include "blockchain-explorer-query.hpp"
#include "blockchain-explorer.hpp"

#if TD_DARWIN || TD_LINUX
#include <fcntl.h>
#include <unistd.h>
#endif
#include <iostream>
#include <sstream>

namespace ton::be {

namespace {

std::string urldecode(td::Slice from, bool decode_plus_sign_as_space) {
  size_t to_i = 0;

  td::BufferSlice x{from.size()};
  auto to = x.as_slice();

  for (size_t from_i = 0, n = from.size(); from_i < n; from_i++) {
    if (from[from_i] == '%' && from_i + 2 < n) {
      int high = td::hex_to_int(from[from_i + 1]);
      int low = td::hex_to_int(from[from_i + 2]);
      if (high < 16 && low < 16) {
        to[to_i++] = static_cast<char>(high * 16 + low);
        from_i += 2;
        continue;
      }
    }
    to[to_i++] = decode_plus_sign_as_space && from[from_i] == '+' ? ' ' : from[from_i];
  }

  return to.truncate(to_i).str();
}

std::map<std::string, std::string> parse_query_string(td::Slice qs) {
  std::map<std::string, std::string> opts;
  size_t i = 0;
  while (i < qs.size()) {
    size_t eq = i;
    while (eq < qs.size() && qs[eq] != '=' && qs[eq] != '&') {
      ++eq;
    }
    size_t amp = eq;
    while (amp < qs.size() && qs[amp] != '&') {
      ++amp;
    }
    if (eq < qs.size() && qs[eq] == '=' && eq > i && amp > eq + 1) {
      auto key = urldecode(qs.substr(i, eq - i), /*decode_plus_sign_as_space=*/true);
      auto value = urldecode(qs.substr(eq + 1, amp - eq - 1), /*decode_plus_sign_as_space=*/true);
      if (!key.empty() && !value.empty()) {
        opts.emplace(std::move(key), std::move(value));
      }
    }
    i = amp + 1;
  }
  return opts;
}

}  // namespace

class CoreActor : public CoreActorInterface {
 private:
  std::string global_config_ = "ton-global.config";

  td::actor::ActorOwn<liteclient::ExtClient> client_;

  td::uint32 http_port_ = 80;
  td::actor::ActorOwn<http::HttpServer> http_server_;

  td::IPAddress remote_addr_;
  ton::PublicKey remote_public_key_;

  bool hide_ips_ = false;

  td::unique_ptr<liteclient::ExtClient::Callback> make_callback() {
    class Callback : public liteclient::ExtClient::Callback {
     public:
      Callback(td::actor::ActorId<CoreActor> id) : id_(std::move(id)) {
      }

     private:
      td::actor::ActorId<CoreActor> id_;
    };

    return td::make_unique<Callback>(actor_id(this));
  }

  std::shared_ptr<RemoteNodeStatus> new_result_;
  td::int32 attempt_ = 0;
  td::int32 waiting_ = 0;

  size_t n_servers_ = 0;

  void run_queries();
  void got_servers_ready(td::int32 attempt, std::vector<bool> ready);
  void send_ping(td::uint32 idx);
  void got_ping_result(td::uint32 idx, td::int32 attempt, td::Result<td::BufferSlice> data);

  void add_result() {
    if (new_result_) {
      auto ts = static_cast<td::int32>(new_result_->ts_.at_unix());
      results_.emplace(ts, std::move(new_result_));
    }
  }

  void alarm() override {
    auto t = static_cast<td::int32>(td::Clocks::system() / 60);
    if (t <= attempt_) {
      alarm_timestamp() = td::Timestamp::at_unix((attempt_ + 1) * 60);
      return;
    }
    if (waiting_ > 0 && new_result_) {
      add_result();
    }
    attempt_ = t;
    run_queries();
    alarm_timestamp() = td::Timestamp::at_unix((attempt_ + 1) * 60);
  }

 public:
  std::mutex queue_mutex_;
  std::mutex res_mutex_;
  std::map<td::int32, std::shared_ptr<RemoteNodeStatus>> results_;
  std::vector<std::string> addrs_;
  inline static CoreActor* instance_ = nullptr;
  td::actor::ActorId<CoreActor> self_id_;

  void set_global_config(std::string str) {
    global_config_ = str;
  }
  void set_http_port(td::uint32 port) {
    http_port_ = port;
  }
  void set_remote_addr(td::IPAddress addr) {
    remote_addr_ = addr;
  }
  void set_remote_public_key(td::BufferSlice file_name) {
    auto R = [&]() -> td::Result<ton::PublicKey> {
      TRY_RESULT_PREFIX(conf_data, td::read_file(file_name.as_slice().str()), "failed to read: ");
      return ton::PublicKey::import(conf_data.as_slice());
    }();

    if (R.is_error()) {
      LOG(FATAL) << "bad server public key: " << R.move_as_error();
    }
    remote_public_key_ = R.move_as_ok();
  }
  void set_hide_ips(bool value) {
    hide_ips_ = value;
  }

  void send_lite_query(td::BufferSlice query, td::Promise<td::BufferSlice> promise) override;
  void get_last_result(td::Promise<std::shared_ptr<RemoteNodeStatus>> promise) override {
  }
  void get_results(td::uint32 max, td::Promise<RemoteNodeStatusList> promise) override {
    RemoteNodeStatusList r;
    r.addrs = hide_ips_ ? std::vector<std::string>{addrs_.size()} : addrs_;
    auto it = results_.rbegin();
    while (it != results_.rend() && r.results.size() < max) {
      r.results.push_back(it->second);
      it++;
    }
    promise.set_value(std::move(r));
  }

  void start_up() override {
    instance_ = this;
    auto t = td::Clocks::system();
    attempt_ = static_cast<td::int32>(t / 60);
    auto next_t = (attempt_ + 1) * 60;
    alarm_timestamp() = td::Timestamp::at_unix(next_t);
    self_id_ = actor_id(this);
  }
  void tear_down() override {
    http_server_.reset();
  }

  CoreActor() {
  }

  void dispatch(std::string path, std::map<std::string, std::string> opts, http::ResponsePromise promise) {
    auto pos = path.rfind('/');
    std::string prefix;
    std::string command;
    if (pos == std::string::npos) {
      prefix = "";
      command = std::move(path);
    } else {
      prefix = path.substr(0, pos + 1);
      command = path.substr(pos + 1);
    }

    if (command == "status") {
      td::actor::create_actor<HttpQueryStatus>("blockinfo", opts, prefix, std::move(promise)).release();
    } else if (command == "block") {
      td::actor::create_actor<HttpQueryBlockInfo>("blockinfo", opts, prefix, std::move(promise)).release();
    } else if (command == "search") {
      if (opts.count("roothash") + opts.count("filehash") > 0) {
        td::actor::create_actor<HttpQueryBlockInfo>("blockinfo", opts, prefix, std::move(promise)).release();
      } else {
        td::actor::create_actor<HttpQueryBlockSearch>("blocksearch", opts, prefix, std::move(promise)).release();
      }
    } else if (command == "last" || command.empty()) {
      td::actor::create_actor<HttpQueryViewLastBlock>("", opts, prefix, std::move(promise)).release();
    } else if (command == "download") {
      td::actor::create_actor<HttpQueryBlockData>("downloadblock", opts, prefix, std::move(promise)).release();
    } else if (command == "viewblock") {
      td::actor::create_actor<HttpQueryBlockView>("viewblock", opts, prefix, std::move(promise)).release();
    } else if (command == "account") {
      td::actor::create_actor<HttpQueryViewAccount>("viewaccount", opts, prefix, std::move(promise)).release();
    } else if (command == "transaction") {
      td::actor::create_actor<HttpQueryViewTransaction>("viewtransaction", opts, prefix, std::move(promise)).release();
    } else if (command == "transaction2") {
      td::actor::create_actor<HttpQueryViewTransaction2>("viewtransaction2", opts, prefix, std::move(promise))
          .release();
    } else if (command == "config") {
      td::actor::create_actor<HttpQueryConfig>("getconfig", opts, prefix, std::move(promise)).release();
    } else if (command == "send") {
      td::actor::create_actor<HttpQuerySend>("send", opts, prefix, std::move(promise)).release();
    } else if (command == "sendform") {
      td::actor::create_actor<HttpQuerySendForm>("sendform", opts, prefix, std::move(promise)).release();
    } else if (command == "runmethod") {
      td::actor::create_actor<HttpQueryRunMethod>("runmethod", opts, prefix, std::move(promise)).release();
    } else {
      http::answer_error(http::status_not_found, "", std::move(promise));
    }
  }

  class PostBodyReader : public http::HttpPayload::Callback {
   public:
    PostBodyReader(td::actor::ActorId<CoreActor> core, std::shared_ptr<http::HttpPayload> payload, std::string path,
                   std::map<std::string, std::string> opts, http::ResponsePromise promise)
        : core_(std::move(core))
        , payload_(std::move(payload))
        , path_(std::move(path))
        , opts_(std::move(opts))
        , promise_(std::move(promise)) {
    }

    static void attach(td::actor::ActorId<CoreActor> core, std::shared_ptr<http::HttpPayload> payload, std::string path,
                       std::map<std::string, std::string> opts, http::ResponsePromise promise) {
      auto cb = std::make_unique<PostBodyReader>(std::move(core), payload, std::move(path), std::move(opts),
                                                 std::move(promise));
      auto* raw = payload.get();
      raw->add_callback(std::move(cb));
      raw->run_callbacks();
    }

    void run(size_t ready_bytes) override {
      if (errored_) {
        return;
      }
      auto chunk = payload_->get_slice(ready_bytes);
      payload_->slice_gc();
      if (buffer_.size() + chunk.size() > max_post_size) {
        errored_ = true;
        http::answer_error(http::status_payload_too_large, "", std::move(promise_));
        return;
      }
      buffer_.append(chunk.as_slice().begin(), chunk.size());
    }

    void completed() override {
      if (errored_) {
        return;
      }
      for (auto& kv : parse_query_string(buffer_)) {
        opts_[kv.first] = std::move(kv.second);
      }
      td::actor::send_closure(core_, &CoreActor::dispatch, std::move(path_), std::move(opts_), std::move(promise_));
    }

   private:
    td::actor::ActorId<CoreActor> core_;
    std::shared_ptr<http::HttpPayload> payload_;
    std::string path_;
    std::map<std::string, std::string> opts_;
    http::ResponsePromise promise_;
    std::string buffer_;
    bool errored_ = false;
  };

  class HttpServerCallback : public http::HttpServer::Callback {
   public:
    explicit HttpServerCallback(td::actor::ActorId<CoreActor> core) : core_(std::move(core)) {
    }

    void receive_request(std::unique_ptr<http::HttpRequest> request, std::shared_ptr<http::HttpPayload> payload,
                         http::ResponsePromise promise) override {
      const auto& method = request->method();
      bool is_post = (method == "POST");
      if (!is_post && method != "GET") {
        http::answer_error(http::status_method_not_allowed, "", std::move(promise));
        return;
      }

      auto url = request->url();
      auto qpos = url.find('?');
      std::string path = (qpos == std::string::npos) ? url : url.substr(0, qpos);
      td::Slice qs = (qpos == std::string::npos) ? td::Slice{} : td::Slice{url}.substr(qpos + 1);
      auto opts = parse_query_string(qs);

      if (is_post) {
        PostBodyReader::attach(core_, std::move(payload), std::move(path), std::move(opts), std::move(promise));
      } else {
        td::actor::send_closure(core_, &CoreActor::dispatch, std::move(path), std::move(opts), std::move(promise));
      }
    }

   private:
    td::actor::ActorId<CoreActor> core_;
  };

  void run() {
    std::vector<liteclient::LiteServerConfig> servers;
    if (remote_public_key_.empty()) {
      auto G = td::read_file(global_config_).move_as_ok();
      auto gc_j = td::json_decode(G.as_slice()).move_as_ok();
      ton::ton_api::liteclient_config_global gc;
      ton::ton_api::from_json(gc, gc_j.get_object()).ensure();
      auto r_servers = liteclient::LiteServerConfig::parse_global_config(gc);
      r_servers.ensure();
      servers = r_servers.move_as_ok();
      for (const auto& serv : servers) {
        addrs_.push_back(serv.hostname);
      }
    } else {
      if (!remote_addr_.is_valid()) {
        LOG(FATAL) << "remote addr not set";
      }
      servers.push_back(liteclient::LiteServerConfig{ton::adnl::AdnlNodeIdFull{remote_public_key_}, remote_addr_});
      addrs_.push_back(servers.back().hostname);
    }
    n_servers_ = servers.size();
    client_ = liteclient::ExtClient::create(std::move(servers), make_callback(), true);
    http_server_ = http::HttpServer::create(static_cast<td::uint16>(http_port_),
                                            std::make_shared<HttpServerCallback>(actor_id(this)));
  }
};

void CoreActor::run_queries() {
  waiting_ = 0;
  new_result_ = std::make_shared<RemoteNodeStatus>(n_servers_, td::Timestamp::at_unix(attempt_ * 60));
  td::actor::send_closure(client_, &liteclient::ExtClient::get_servers_status,
                          [SelfId = actor_id(this), attempt = attempt_](td::Result<std::vector<bool>> R) {
                            R.ensure();
                            td::actor::send_closure(SelfId, &CoreActor::got_servers_ready, attempt, R.move_as_ok());
                          });
}

void CoreActor::got_servers_ready(td::int32 attempt, std::vector<bool> ready) {
  if (attempt != attempt_) {
    return;
  }
  CHECK(ready.size() == n_servers_);
  for (td::uint32 i = 0; i < n_servers_; i++) {
    if (ready[i]) {
      send_ping(i);
    }
  }
  CHECK(waiting_ >= 0);
  if (waiting_ == 0) {
    add_result();
  }
}

void CoreActor::send_ping(td::uint32 idx) {
  waiting_++;
  auto query = ton::create_tl_object<ton::lite_api::liteServer_getMasterchainInfo>();
  auto q = ton::create_tl_object<ton::lite_api::liteServer_query>(serialize_tl_object(query, true));

  auto P =
      td::PromiseCreator::lambda([SelfId = actor_id(this), idx, attempt = attempt_](td::Result<td::BufferSlice> R) {
        td::actor::send_closure(SelfId, &CoreActor::got_ping_result, idx, attempt, std::move(R));
      });
  td::actor::send_closure(client_, &liteclient::ExtClient::send_query_to_server, "query", serialize_tl_object(q, true),
                          idx, td::Timestamp::in(10.0), std::move(P));
}

void CoreActor::got_ping_result(td::uint32 idx, td::int32 attempt, td::Result<td::BufferSlice> R) {
  if (attempt != attempt_) {
    return;
  }
  if (R.is_error()) {
    waiting_--;
    if (waiting_ == 0) {
      add_result();
    }
    return;
  }
  auto data = R.move_as_ok();
  {
    auto F = ton::fetch_tl_object<ton::lite_api::liteServer_error>(data.clone(), true);
    if (F.is_ok()) {
      auto f = F.move_as_ok();
      auto err = td::Status::Error(f->code_, f->message_);
      waiting_--;
      if (waiting_ == 0) {
        add_result();
      }
      return;
    }
  }
  auto F = ton::fetch_tl_object<ton::lite_api::liteServer_masterchainInfo>(std::move(data), true);
  if (F.is_error()) {
    waiting_--;
    if (waiting_ == 0) {
      add_result();
    }
    return;
  }
  auto f = F.move_as_ok();
  new_result_->values_[idx] = ton::create_block_id(f->last_);
  waiting_--;
  CHECK(waiting_ >= 0);
  if (waiting_ == 0) {
    add_result();
  }
}

void CoreActor::send_lite_query(td::BufferSlice query, td::Promise<td::BufferSlice> promise) {
  auto P = td::PromiseCreator::lambda([promise = std::move(promise)](td::Result<td::BufferSlice> R) mutable {
    if (R.is_error()) {
      promise.set_error(R.move_as_error());
      return;
    }
    auto B = R.move_as_ok();
    {
      auto F = ton::fetch_tl_object<ton::lite_api::liteServer_error>(B.clone(), true);
      if (F.is_ok()) {
        auto f = F.move_as_ok();
        promise.set_error(td::Status::Error(f->code_, f->message_));
        return;
      }
    }
    promise.set_value(std::move(B));
  });
  auto q = ton::create_tl_object<ton::lite_api::liteServer_query>(std::move(query));
  td::actor::send_closure(client_, &liteclient::ExtClient::send_query, "query", serialize_tl_object(q, true),
                          td::Timestamp::in(10.0), std::move(P));
}

td::actor::ActorId<CoreActorInterface> CoreActorInterface::instance_actor_id() {
  auto instance = CoreActor::instance_;
  CHECK(instance);
  return instance->self_id_;
}

}  // namespace ton::be

int main(int argc, char* argv[]) {
  using namespace ton::be;

  SET_VERBOSITY_LEVEL(verbosity_INFO);
  td::set_default_failure_signal_handler().ensure();

  td::actor::ActorOwn<CoreActor> x;

  td::OptionParser p;
  p.set_description("TON Blockchain explorer");
  p.add_checked_option('h', "help", "prints_help", [&]() {
    char b[10240];
    td::StringBuilder sb(td::MutableSlice{b, 10000});
    sb << p;
    std::cout << sb.as_cslice().c_str();
    std::exit(2);
    return td::Status::OK();
  });
  p.add_checked_option('I', "hide-ips", "hides ips from status", [&]() {
    td::actor::send_closure(x, &CoreActor::set_hide_ips, true);
    return td::Status::OK();
  });
  p.add_checked_option('u', "user", "change user", [&](td::Slice user) { return td::change_user(user.str()); });
  p.add_checked_option('C', "global-config", "file to read global config", [&](td::Slice fname) {
    td::actor::send_closure(x, &CoreActor::set_global_config, fname.str());
    return td::Status::OK();
  });
  p.add_checked_option('a', "addr", "connect to ip:port", [&](td::Slice arg) {
    td::IPAddress addr;
    TRY_STATUS(addr.init_host_port(arg.str()));
    td::actor::send_closure(x, &CoreActor::set_remote_addr, addr);
    return td::Status::OK();
  });
  p.add_checked_option('p', "pub", "remote public key", [&](td::Slice arg) {
    td::actor::send_closure(x, &CoreActor::set_remote_public_key, td::BufferSlice{arg});
    return td::Status::OK();
  });
  p.add_checked_option('v', "verbosity", "set verbosity level", [&](td::Slice arg) {
    int verbosity = td::to_integer<int>(arg);
    SET_VERBOSITY_LEVEL(VERBOSITY_NAME(FATAL) + verbosity);
    return (verbosity >= 0 && verbosity <= 9) ? td::Status::OK() : td::Status::Error("verbosity must be 0..9");
  });
  p.add_checked_option('d', "daemonize", "set SIGHUP", [&]() {
    td::set_signal_handler(td::SignalType::HangUp, [](int sig) {
#if TD_DARWIN || TD_LINUX
      close(0);
      setsid();
#endif
    }).ensure();
    return td::Status::OK();
  });
  p.add_checked_option('H', "http-port", "listen on http port", [&](td::Slice arg) {
    td::actor::send_closure(x, &CoreActor::set_http_port, td::to_integer<td::uint32>(arg));
    return td::Status::OK();
  });
  p.add_checked_option('L', "local-scripts", "use local copy of ajax/bootstrap/... JS", [&]() {
    local_scripts = true;
    return td::Status::OK();
  });
#if TD_DARWIN || TD_LINUX
  p.add_checked_option('l', "logname", "log to file", [&](td::Slice fname) {
    auto FileLog = td::FileFd::open(td::CSlice(fname.str().c_str()),
                                    td::FileFd::Flags::Create | td::FileFd::Flags::Append | td::FileFd::Flags::Write)
                       .move_as_ok();

    dup2(FileLog.get_native_fd().fd(), 1);
    dup2(FileLog.get_native_fd().fd(), 2);
    return td::Status::OK();
  });
#endif

  vm::init_vm().ensure();

  td::actor::Scheduler scheduler({2});
  scheduler.run_in_context([&] { x = td::actor::create_actor<CoreActor>("testnode"); });

  scheduler.run_in_context([&] { p.run(argc, argv).ensure(); });
  scheduler.run_in_context([&] {
    td::actor::send_closure(x, &CoreActor::run);
    x.release();
  });
  scheduler.run();

  return 0;
}
