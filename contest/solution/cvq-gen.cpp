#include "contest-validate-query.hpp"

namespace solution {

using namespace ton;
using namespace ton::validator;

using namespace std::literals::string_literals;





void ContestValidateQuery::generated_root() {
  // <{generated_dag_root
  check_in_msg_descr();
  check_out_msg_descr();
  check_transactions();
  check_new_state();
  // generated_dag_root}/>
}





}

