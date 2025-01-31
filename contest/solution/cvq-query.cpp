#include "contest-validate-query.hpp"




namespace solution {

using namespace ton;
using namespace ton::validator;




bool ContestValidateQuery::in_main_thread() {
  return !in_multithreading || std::this_thread::get_id() == main_thread_id;
}

void ContestValidateQuery::leave_multithreading() {
  in_multithreading = false;
}

void ContestValidateQuery::enter_multithreading() {
  in_multithreading = true;
  main_thread_id = std::this_thread::get_id();
}



void ContestValidateQuery::reject_throw(std::string error, td::BufferSlice reason) {
  error = error_ctx() + error;
  throw error;
}

void ContestValidateQuery::reject_throw(std::string err_msg, td::Status error, td::BufferSlice reason) {
  error.ensure_error();
  reject_throw(err_msg + " : " + error.to_string(), std::move(reason));
}



/**
 * Aborts the validation with the given error.
 *
 * @param error The error encountered.
 */
void ContestValidateQuery::abort_query(td::Status error) {
  (void)fatal_error(std::move(error));
}

/**
 * Rejects the validation and logs an error message.
 *
 * @param error The error message to be logged.
 * @param reason The reason for rejecting the validation.
 *
 * @returns False indicating that the validation failed.
 */
bool ContestValidateQuery::reject_query(std::string error, td::BufferSlice reason) {
  error = error_ctx() + error;
  if (!in_main_thread()) { // in_multithreading
    // LOG(ERROR) << "Test index #" << testIndex << " will throw from thread: " << render_thread_id(std::this_thread::get_id()); // !TEMP_THREAD
    throw error;
    // LOG(ERROR) << "Not main thread. Main thread (" << std::hash<std::thread::id>{}(main_thread_id)
    //            << "), current thread (" << std::hash<std::thread::id>{}(std::this_thread::get_id()) << ")";
  }
  if (main_promise) {
    main_promise.set_error(td::Status::Error(error));
  }
  LOG(WARNING) << "REJECT: aborting validation of block candidate for " << shard_.to_str() << " : " << error;
  // LOG(ERROR) << "Test index #" << testIndex << " will call stop from thread: " << render_thread_id(std::this_thread::get_id()); // !TEMP_THREAD
  stop();
  return false;
}

/**
 * Rejects the validation and logs an error message.
 *
 * @param err_msg The error message to be displayed.
 * @param error The error status.
 * @param reason The reason for rejecting the query.
 *
 * @returns False indicating that the validation failed.
 */
bool ContestValidateQuery::reject_query(std::string err_msg, td::Status error, td::BufferSlice reason) {
  error.ensure_error();
  return reject_query(err_msg + " : " + error.to_string(), std::move(reason));
}

/**
 * Rejects the validation and logs an error message.
 *
 * @param error The error message to be logged.
 * @param reason The reason for rejecting the validation.
 *
 * @returns False indicating that the validation failed.
 */
bool ContestValidateQuery::soft_reject_query(std::string error, td::BufferSlice reason) {
  error = error_ctx() + error;
  LOG(WARNING) << "SOFT REJECT: aborting validation of block candidate for " << shard_.to_str() << " : " << error;
  if (main_promise) {
    main_promise.set_error(td::Status::Error(std::move(error)));
  }
  stop();
  return false;
}



void ContestValidateQuery::fatal_throw(td::Status error) {
  error.ensure_error();
  throw "aborting validation of block candidate for " + shard_.to_str() + " : " + error.to_string();
}
void ContestValidateQuery::fatal_throw(std::string err_msg, int err_code) {
  fatal_throw(td::Status::Error(err_code, error_ctx() + err_msg));
}
void ContestValidateQuery::fatal_throw(int err_code, std::string err_msg) {
  fatal_throw(td::Status::Error(err_code, error_ctx() + err_msg));
}



/**
 * Handles a fatal error during validation.
 *
 * @param error The error status.
 *
 * @returns False indicating that the validation failed.
 */
bool ContestValidateQuery::fatal_error(td::Status error) {
  // LOG(ERROR) << "fatal error: " << error.to_string(); // !TEMP_DEBUG
  error.ensure_error();
  LOG(WARNING) << "aborting validation of block candidate for " << shard_.to_str() << " : " << error.to_string();
  // if (in_multithreading) {
  //   throw error;
  //   // LOG(ERROR) << "Not main thread. Main thread (" << std::hash<std::thread::id>{}(main_thread_id)
  //   //            << "), current thread (" << std::hash<std::thread::id>{}(std::this_thread::get_id()) << ")";
  // }
  if (main_promise) {
    main_promise.set_error(std::move(error));
  }
  stop();
  return false;
}

/**
 * Handles a fatal error during validation.
 *
 * @param err_code Error code.
 * @param err_msg Error message.
 *
 * @returns False indicating that the validation failed.
 */
bool ContestValidateQuery::fatal_error(int err_code, std::string err_msg) {
  return fatal_error(td::Status::Error(err_code, error_ctx() + err_msg));
}

/**
 * Handles a fatal error during validation.
 *
 * @param err_code Error code.
 * @param err_msg Error message.
 * @param error Error status.
 *
 * @returns False indicating that the validation failed.
 */
bool ContestValidateQuery::fatal_error(int err_code, std::string err_msg, td::Status error) {
  error.ensure_error();
  return fatal_error(err_code, err_msg + " : " + error.to_string());
}

/**
 * Handles a fatal error during validation.
 *
 * @param err_msg Error message.
 * @param err_code Error code.
 *
 * @returns False indicating that the validation failed.
 */
bool ContestValidateQuery::fatal_error(std::string err_msg, int err_code) {
  return fatal_error(td::Status::Error(err_code, error_ctx() + err_msg));
}

/**
 * Finishes the query and sends the result to the promise.
 */
void ContestValidateQuery::finish_query() {
  // <{generated_atomic_zero_checks
  if (__pending_check_transactions != 0) LOG(ERROR) << "Generated atomic variable should be exactly 0, when reaching 'finish_query', but variable __pending_check_transactions ended up as: " << __pending_check_transactions;
  if (__pending_unpack_dispatch_queue_update_after != 0) LOG(ERROR) << "Generated atomic variable should be exactly 0, when reaching 'finish_query', but variable __pending_unpack_dispatch_queue_update_after ended up as: " << __pending_unpack_dispatch_queue_update_after;
  if (__pending_check_in_msg_descr != 0) LOG(ERROR) << "Generated atomic variable should be exactly 0, when reaching 'finish_query', but variable __pending_check_in_msg_descr ended up as: " << __pending_check_in_msg_descr;
  if (__pending_check_out_msg_descr != 0) LOG(ERROR) << "Generated atomic variable should be exactly 0, when reaching 'finish_query', but variable __pending_check_out_msg_descr ended up as: " << __pending_check_out_msg_descr;
  if (__pending_check_processed_upto != 0) LOG(ERROR) << "Generated atomic variable should be exactly 0, when reaching 'finish_query', but variable __pending_check_processed_upto ended up as: " << __pending_check_processed_upto;
  if (__pending_check_message_processing_order != 0) LOG(ERROR) << "Generated atomic variable should be exactly 0, when reaching 'finish_query', but variable __pending_check_message_processing_order ended up as: " << __pending_check_message_processing_order;
  if (__pending_check_dispatch_queue_update != 0) LOG(ERROR) << "Generated atomic variable should be exactly 0, when reaching 'finish_query', but variable __pending_check_dispatch_queue_update ended up as: " << __pending_check_dispatch_queue_update;
  if (__pending_check_in_queue != 0) LOG(ERROR) << "Generated atomic variable should be exactly 0, when reaching 'finish_query', but variable __pending_check_in_queue ended up as: " << __pending_check_in_queue;
  if (__pending_postcheck_account_updates != 0) LOG(ERROR) << "Generated atomic variable should be exactly 0, when reaching 'finish_query', but variable __pending_postcheck_account_updates ended up as: " << __pending_postcheck_account_updates;
  if (__pending_postcheck_value_flow != 0) LOG(ERROR) << "Generated atomic variable should be exactly 0, when reaching 'finish_query', but variable __pending_postcheck_value_flow ended up as: " << __pending_postcheck_value_flow;
  if (__pending_build_state_update != 0) LOG(ERROR) << "Generated atomic variable should be exactly 0, when reaching 'finish_query', but variable __pending_build_state_update ended up as: " << __pending_build_state_update;
  // generated_atomic_zero_checks}/>

  if (main_promise) {
    LOG(WARNING) << "validate query done";
    main_promise.set_result(std::move(result_state_update_));
  }
  stop();
}




}
