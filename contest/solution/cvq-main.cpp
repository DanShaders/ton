#include "contest-validate-query.hpp"

#include <string>


namespace solution {

using namespace ton;
using namespace ton::validator;
using namespace std::literals::string_literals;

/**
 * Constructs a ContestValidateQuery object.
 *
 * @param block_id Id of the block
 * @param block_data Block data, but without state update
 * @param collated_data Collated data (proofs of shard states)
 * @param promise The Promise to return the serialized state update to
 */
ContestValidateQuery::ContestValidateQuery(BlockIdExt block_id, td::BufferSlice block_data,
                                           td::BufferSlice collated_data, td::Promise<td::BufferSlice> promise)
    : shard_(block_id.shard_full())
    , id_(block_id)
    , block_data(std::move(block_data))
    , collated_data(std::move(collated_data))
    , main_promise(std::move(promise))
    , shard_pfx_(shard_.shard)
    , shard_pfx_len_(ton::shard_prefix_length(shard_)) {

  testIndex = ++globalTestIndex;
  // msg_proc_lt_.reserve(100000); // !TEMP_BAD_THREAD

  //<%assigned%>: shard_
  //<%assigned%>: workchain (available as computable from shard_)
  //<%assigned%>: id_
  //<%assigned%>: block_data
  //<%assigned%>: collated_data
  //<%assigned%>: main_promise
  //<%assigned%>: shard_pfx_
  //<%assigned%>: shard_pfx_len_
  //<%assigned%>: max_shard_lt_
}


/**
 * Starts the validation process.
 *
 * This function performs various checks on the validation parameters and the block candidate.
 * Then the function also sends requests to the ValidatorManager to fetch blocks and shard stated.
 */
void ContestValidateQuery::start_up() {
  LOG(INFO) << "validate query for " << id_.to_str() << " started";
  rand_seed_.set_zero();

  // if (main_thread_id == std::this_thread::get_id()) {
  //   LOG(ERROR) << "testIndex #" << testIndex << ": ---------------------------------- thread_ids equal: " << render_thread_id(main_thread_id);
  // }
  // LOG(ERROR) << "Test index #" << testIndex << ": Stored main_thread_id: " << render_thread_id(main_thread_id)
  //            << ", current thread id: " << render_thread_id(std::this_thread::get_id()); // !TEMP_THREAD
  main_thread_id = std::this_thread::get_id();
  doesItCreateNewInstancePerTest++;
  // LOG(ERROR) << "start_up: doesItCreateNewInstancePerTest = " << doesItCreateNewInstancePerTest;
  ORIGINAL_CHECK(doesItCreateNewInstancePerTest == 1);



  // ------------------- CHECKS -------------------
  try {
    SoftRejectIf(ShardIdFull(id_) != shard_);
    // PSTRING() << "block candidate belongs to shard " << ShardIdFull(id_).to_str()
    //           << " different from current shard " << shard_.to_str()
    SoftRejectIfWithComment(workchain() != ton::basechainId, "only basechain is supported");
    SoftRejectIfWithComment(!shard_.is_valid_ext(), "requested to validate a block for an invalid shard");
    td::uint64 x = td::lower_bit64(shard_.shard);
    SoftRejectIfWithComment(x < 8, "a shard cannot be split more than 60 times");
    // 3. unpack block candidate (while necessary data is being loaded)
    unpack_block_candidate(); // reject_query("error unpacking block candidate");

    SoftRejectIfWithComment(prev_blocks.size() > 2, "cannot have more than two previous blocks");
    SoftRejectIfWithComment(!prev_blocks.size(), "must have one or two previous blocks to generate a next block");

    if (prev_blocks.size() == 2) {
      SoftRejectIfWithComment(
        !(shard_is_parent(shard_, ShardIdFull(prev_blocks[0])) &&
          shard_is_parent(shard_, ShardIdFull(prev_blocks[1])) && prev_blocks[0].id.shard < prev_blocks[1].id.shard),
          "the two previous blocks for a merge operation are not siblings or are not children of current shard"
      );
      for (const auto& blk : prev_blocks) {
        SoftRejectIfWithComment(!blk.id.seqno, "previous blocks for a block merge operation must have non-zero seqno");
      }
      // soft_reject_query("merging shards is not implemented yet");
      // return;
    } else {
      ORIGINAL_CHECK(prev_blocks.size() == 1);
      // creating next block
      SoftRejectIfWithComment(!ShardIdFull(prev_blocks[0]).is_valid_ext(), "previous block does not have a valid id");
      if (ShardIdFull(prev_blocks[0]) != shard_) {
        SoftRejectIfWithComment(!shard_is_parent(ShardIdFull(prev_blocks[0]), shard_),
          "previous block does not belong to the shard we are generating a new block for"
        );
      }
      // if (_after_split_) {
      //   // _soft_reject_query("splitting shards not implemented yet");
      //   // return;
      // }
    }


    // 4. load state(s) corresponding to previous block(s)
    prev_states.resize(prev_blocks.size());
    for (int i = 0; (unsigned)i < prev_blocks.size(); i++) {
      // 4.1. load state
      LOG(DEBUG) << "sending wait_block_state() query #" << i << " for " << prev_blocks[i].to_str() << " to Manager";
      after_get_shard_state(i, fetch_block_state(prev_blocks[i]));
    }
    // 5. request masterchain state referred to in the block
    after_get_mc_state(fetch_block_state(mc_blkid_));


    // MAIN VALIDATOR SEQUENCE (invokes other methods in a suitable order).
    // (previously: try_validate())

    LOG(INFO) << "try_validate stage 0";
    PropDest2(state_usage_tree_, prev_state_root_, compute_prev_state()); // fatal_error(-666, "cannot compute previous state"); return;
    request_neighbor_queues(); // fatal_error("cannot request neighbor output queues"); return;
    unpack_prev_state(prev_state_root_); // fatal_error("cannot unpack previous state"); return;



    // _init_next_state(); // fatal_error("cannot unpack previous state"); return;
    // _check_utime_lt(); // reject_query("creation utime/lt of the new block is invalid"); return;
    // _prepare_out_msg_queue_size(); // reject_query("cannot request out msg queue size"); return;

    // The following check was moved to unpack_block_candidate() just after block_root_ becomes available
    // having non-function code here hinders DAGging the entire thing
    // LOG(INFO) << "try_validate stage 1";
    // LOG(INFO) << "running automated validity checks for block candidate " << id_.to_str();
    // if (!block::gen::t_BlockRelaxed.validate_ref(10000000, block_root_)) {
    //   reject_throw("block "s + id_.to_str() + " failed to pass automated validity checks"); return;
    // }
    
    // _fix_all_processed_upto(); // fatal_error("cannot adjust all ProcessedUpto of neighbor and previous blocks"); return;
    // _add_trivial_neighbor(); // fatal_error("cannot add previous block as a trivial neighbor"); return;
    // _unpack_block_data(); // reject_query("cannot unpack block data: " + error);
    // _precheck_account_transactions(); // reject_query("invalid collection of account transactions in ShardAccountBlocks"); return;
    // _build_new_message_queue(); // reject_query("cannot build a new message queue"); return;
    // _precheck_message_queue_update(); // reject_query("invalid OutMsgQueue update"); return;
    // _unpack_dispatch_queue_update(); // reject_query("invalid DispatchQueue update"); return;
    // _unpack_dispatch_queue_update_after();
    // _check_in_msg_descr(); // reject_query("invalid InMsgDescr"); return;
    // _check_out_msg_descr(); // reject_query("invalid OutMsgDescr"); return;
    // _check_dispatch_queue_update(); // reject_query("invalid OutMsgDescr"); return;
    // _check_processed_upto(); // reject_query("invalid ProcessedInfo"); return;
    // _check_in_queue(); // reject_query("cannot check inbound message queues"); return;
    // _check_transactions(); // // LOG(ERROR) << "Test index #" << testIndex << ": another reject_query here"; reject_query("invalid collection of account transactions in ShardAccountBlocks"); return;
    // _postcheck_account_updates(); // reject_query("invalid AccountState update"); return;
    // _check_message_processing_order(); // reject_query("some messages have been processed by transactions in incorrect order"); return;
    // _check_new_state(); // reject_query("the header of the new shardchain state is invalid"); return;  
    // _postcheck_value_flow(); // reject_query("new ValueFlow is invalid"); return;
    // _build_state_update(state_usage_tree_, prev_state_root_); // reject_query("cannot build state update"); return;
    // _finish_query();

    // {
    //   try {
    //     MyThreader tempThr;
    //     tempThr.launch([this] { throw "Zhuk"; });
    //     tempThr.launch([this] { sleep(5); LOG(ERROR) << "Log after long wait and throw"; throw "Luk"; });
    //     tempThr.waitForAll();

    //     // async([] { sleep(5); throw "Vuun"; }).get();
    //   } catch (std::string error) {
    //     LOG(ERROR) << "Caught: " << error;
    //   } catch (std::string& error) {
    //     LOG(ERROR) << "Caught: " << error << ", but by reference";
    //   } catch (char* error) {
    //     LOG(ERROR) << "Caught: " << error << ", but as a char*";
    //   } catch (const char* error) {
    //     LOG(ERROR) << "Caught: " << error << ", but as a CONST char*";
    //   } catch (...) {
    //     LOG(ERROR) << "Caught something from tempThr";
    //   }
    // }

    generated_root();

    // sleep(1);

    finish_query();
  } catch (std::string error) {
    top_level_reject_query(error); return;
  } catch (vm::VmError& err) {
    top_level_fatal_error(-666, err.get_msg()); return;
  } catch (vm::VmVirtError& err) {
    top_level_reject_query(err.get_msg()); return;
  } catch (...) {
    // It shouldn't happen from what I understand, but better to put it just in case
    LOG(ERROR) << "Caught some unknown exception";
    top_level_fatal_error(-555, "Caught some unknown exception"); return;
  }
}



}

