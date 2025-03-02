#pragma once
#include "actor/PromiseFuture.h"
#include "ton/ton-types.h"

namespace td {
class BufferSlice;
}  // namespace td
namespace ton {
struct BlockIdExt;
}  // namespace ton

void run_contest_solution(ton::BlockIdExt block_id, td::BufferSlice block_data, td::BufferSlice colldated_data,
                          td::Promise<td::BufferSlice> promise);
