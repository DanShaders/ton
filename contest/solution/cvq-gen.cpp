#include "contest-validate-query.hpp"

namespace solution {

using namespace ton;
using namespace ton::validator;

using namespace std::literals::string_literals;





void ContestValidateQuery::generated_root() {
  // <{generated_dag_root
  my_threader.launch([this] { request_neighbor_queues(); });
  my_threader.launch([this] { init_next_state(); });
  // generated_dag_root}/>

  my_threader.waitForAll();
}





}

