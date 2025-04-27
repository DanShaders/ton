#pragma once
#include "td/actor/PromiseFuture.h"
#include "ton/ton-types.h"
#include <map>
#include "vm/cells/CellHash.h"
#include "vm/cells/Cell.h"

void run_contest_solution(ton::BlockIdExt block_id, td::BufferSlice block_data, td::BufferSlice colldated_data,
                          std::map<vm::CellHash, td::Ref<vm::Cell>> const&, td::Promise<td::BufferSlice> promise);
