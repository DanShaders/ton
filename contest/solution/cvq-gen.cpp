#include "contest-validate-query.hpp"

namespace solution {

using namespace ton;
using namespace ton::validator;

using namespace std::literals::string_literals;





void ContestValidateQuery::generated_root() {
	// <{generated_root
	postcheck_account_updates();
	check_message_processing_order();
	check_new_state();
	postcheck_value_flow();
	// generated_root}/>
}





}

