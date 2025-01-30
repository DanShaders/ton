const annotatedProperties: string[] = [
	// ContestValidateQuery::ContestValidateQuery
	'shard_',
	'id_',
	'block_data',
	'collated_data',
	'main_promise',
	'shard_pfx_',
	'shard_pfx_len_',



	'global_id_', 
	'vert_seqno_',
	'start_lt_',
	'end_lt_',
	'now_',
	'before_split_',


	'prev_state_root_',
	'state_usage_tree_',

	'extra_collated_data_',

	'mc_state_',
	'mc_state_root_',

	'mc_blkid_',


	'ps_',

	'ns_.id_',
	'ns_.global_id_',
	'ns_.utime_',
	'ns_.lt_',
	'ns_.mc_blk_ref_',
	'ns_.vert_seqno_',
	'ns_.before_split_',

	'ns_.processed_upto_',

	'ns_.min_ref_mc_seqno_',
	'ns_.overload_history_',
	'ns_.underload_history_',

	'ns_.account_dict_',

	'ns_.total_balance_',
	'ns_.total_validator_fees_',
	'ns_.out_msg_queue_',
	'ns_.dispatch_queue_',
	'ns_.out_msg_queue_size_',


	'result_state_update_',

	// Added for: postcheck_value_flow()
	'transaction_fees_',
	'total_burned_',
	'fees_burned_', // Inited with 0, never changed
	'value_flow_',
	'import_fees_',



	'after_merge_',
	'after_split_',
	'mc_seqno_',
	'min_shard_ref_mc_seqno_', // Inited with 0, never changed
	'aux_mc_states_',
];

export default annotatedProperties;
