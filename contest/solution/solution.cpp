#include "solution.hpp"

#include "contest-validate-query.hpp"

void run_contest_solution(ton::BlockIdExt block_id, td::BufferSlice block_data, td::BufferSlice colldated_data,
                          std::map<vm::CellHash, td::Ref<vm::Cell>> const& repacked_cells,
                          td::Promise<td::BufferSlice> promise) {
  td::actor::create_actor<solution::ContestValidateQuery>("validate", block_id, std::move(block_data),
                                                          std::move(colldated_data), repacked_cells, std::move(promise))
      .release();
}
