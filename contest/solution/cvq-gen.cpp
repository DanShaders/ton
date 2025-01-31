#include "contest-validate-query.hpp"

namespace solution {

using namespace ton;
using namespace ton::validator;

using namespace std::literals::string_literals;





void ContestValidateQuery::generated_root() {
  // <{generated_dag_root
  precheck_account_transactions();
  build_new_message_queue();
  check_new_state();
  // generated_dag_root}/>
}





}

