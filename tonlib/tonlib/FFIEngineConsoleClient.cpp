#include "td/actor/coro_utils.h"

#include "EngineConsoleClient.h"
#include "FFIEngineConsoleClient.h"

namespace tonlib {

FFIEngineConsoleClient::FFIEngineConsoleClient(FFIEventLoop& loop, td::IPAddress address,
                                               ton::PublicKey server_public_key, ton::PrivateKey client_private_key)
    : loop_(loop), counter_(loop.new_actor()) {
  loop_.run_in_context([&] {
    client_ = td::actor::create_actor<EngineConsoleClient>("EngineConsoleClient", address, server_public_key,
                                                           client_private_key);
  });
}

using Query = ton::tl_object_ptr<ton::ton_api::Function>;
using Result = ton::tl_object_ptr<ton::ton_api::Object>;

td::actor::Task<Result> FFIEngineConsoleClient::request(Query query) {
  return loop_.run_in_context([&] {
    auto func = [](auto client, Query query) -> td::actor::Task<Result> {
      co_return co_await td::actor::ask(client, &EngineConsoleClient::query, std::move(query));
    };
    return func(client_.get(), std::move(query));
  });
}

}  // namespace tonlib
