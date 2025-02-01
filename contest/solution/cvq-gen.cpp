#include "contest-validate-query.hpp"

namespace solution {

using namespace ton;
using namespace ton::validator;

using namespace std::literals::string_literals;





void ContestValidateQuery::generated_root() {
  // <{generated_dag_root
  my_threader.launchAndProfile("compute_prev_state", [this] { compute_prev_state(); });
  my_threader.launchAndProfile("request_neighbor_queues", [this] { request_neighbor_queues(); });
  my_threader.launchAndProfile("init_next_state", [this] { init_next_state(); });
  // generated_dag_root}/>

  my_threader.waitForAll();
}





}

