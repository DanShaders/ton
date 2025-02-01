#include "contest-validate-query.hpp"

namespace solution {

using namespace ton;
using namespace ton::validator;

using namespace std::literals::string_literals;





void ContestValidateQuery::generated_root() {
  // <{generated_dag_root
  my_threader.launch([this] { precheck_account_transactions(); });
  my_threader.launch([this] { build_new_message_queue(); });
  my_threader.launch([this] { check_new_state(); });
  // generated_dag_root}/>
}





}

