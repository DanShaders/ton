#include "contest-validate-query.hpp"

namespace solution {

using namespace ton;
using namespace ton::validator;

using namespace std::literals::string_literals;





void ContestValidateQuery::generated_root() {
  // <{generated_dag_root
  my_threader.launch([this] { unpack_dispatch_queue_update_after(); });
  my_threader.launch([this] { check_out_msg_descr(); });
  my_threader.launch([this] { check_transactions(); });
  my_threader.launch([this] { check_new_state(); });
  // generated_dag_root}/>

  my_threader.waitForAll();
}





}

