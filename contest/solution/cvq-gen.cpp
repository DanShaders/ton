#include "contest-validate-query.hpp"

namespace solution {

using namespace ton;
using namespace ton::validator;

using namespace std::literals::string_literals;





void ContestValidateQuery::generated_root() {
  // <{generated_dag_root
  my_threader.launch([this] { check_utime_lt(); });
  my_threader.launch([this] { prepare_out_msg_queue_size(); });
  my_threader.launch([this] { fix_all_processed_upto(); });
  my_threader.launch([this] { unpack_block_data(); });
  // generated_dag_root}/>

  my_threader.waitForAll();
}





}

