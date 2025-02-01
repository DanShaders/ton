#include "contest-validate-query.hpp"

namespace solution {

using namespace ton;
using namespace ton::validator;

using namespace std::literals::string_literals;





void ContestValidateQuery::generated_root() {
  // <{generated_dag_root
  my_threader.launch([this] { add_trivial_neighbor(); });
  my_threader.launch([this] { unpack_block_data(); });
  // generated_dag_root}/>

  my_threader.waitForAll();
}





}

