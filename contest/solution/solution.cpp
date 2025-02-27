#include "solution.hpp"

#include <memory>
#include <utility>

#include "vm/boc.h"
#include "block-auto.h"
#include "contest-validate-query.hpp"
#include "common/refcnt.hpp"
#include "actor/ActorOwn.h"
#include "actor/actor.h"
#include "utils/Status.h"
#include "utils/buffer.h"
#include "ton/ton-types.h"
#include "vm/cells/Cell.h"

void run_contest_solution(ton::BlockIdExt block_id, td::BufferSlice block_data, td::BufferSlice colldated_data,
                          td::Promise<td::BufferSlice> promise) {
  td::actor::create_actor<solution::ContestValidateQuery>(
      "validate", block_id, std::move(block_data), std::move(colldated_data), std::move(promise)).release();
}
