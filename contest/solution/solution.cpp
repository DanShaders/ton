#include "solution.hpp"

#include "vm/boc.h"
#include "block-auto.h"
#include "contest-validate-query.hpp"

#include <future>
#include <memory>


void run_contest_solution(ton::BlockIdExt block_id, td::BufferSlice block_data, td::BufferSlice colldated_data,
                          td::Promise<td::BufferSlice> promise) {
  TRY_RESULT_PROMISE(promise, root, vm::std_boc_deserialize(block_data));
  block::gen::Block::Record rec;
  if (!block::gen::t_Block.cell_unpack(root, rec)) {
    return promise.set_error(td::Status::Error("failed to unpack block"));
  }
  TRY_RESULT_PROMISE(promise, res, vm::std_boc_serialize(rec.state_update));

  auto destruction_promise = std::make_shared<std::promise<bool>>();
  auto destruction_token = std::make_unique<solution::DestructionToken>(destruction_promise);
  auto result_promise = std::make_shared<std::promise<td::Result<td::BufferSlice>>>();
  td::Result<td::BufferSlice> result;

  {
    td::Promise<td::BufferSlice> promise2([&](td::Result<td::BufferSlice> res) {
      result_promise->set_value(std::move(res));
    });

    vm::ResetDataCellArena();
    vm::ResetCellSliceArena();
    vm::SetArenaForDataCellEnabled(true);
    vm::SetArenaForCellSliceEnabled(true);

    auto actor = td::actor::create_actor<solution::ContestValidateQuery>(
        "validate", block_id, std::move(block_data), std::move(colldated_data),
        std::move(promise2), std::move(destruction_token));

    result = result_promise->get_future().get();
  }
  destruction_promise->get_future().wait();

  vm::SetArenaForDataCellEnabled(false);
  vm::SetArenaForCellSliceEnabled(false);

  if (result.is_error()) {
    return promise.set_error(result.move_as_error());
  } else {
    return promise.set_value(result.move_as_ok());
  }
}
