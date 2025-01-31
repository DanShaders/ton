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


	// Added for: check_message_processing_order()
	'msg_proc_lt_',
	'in_msg_emitted_lt_',
	'out_msg_emitted_lt_',


	// Added for: postcheck_account_updates()
	'account_blocks_dict_',


	// Added for: check_transactions()
	'verbosity', // Inited with 0, never changed
	'workchain',
	'ns_mutex_',
	'config_',
	'old_shard_conf_',
	'new_shard_conf_',
	'block_limits_',
	'compute_phase_cfg_',
	'total_gas_used_',
	'total_special_gas_used_',
	'storage_phase_cfg_',
	'action_phase_cfg_',
	'in_msg_dict_',
	'out_msg_dict_',
	'msg_proc_lt_mutex_',
	'account_expected_defer_all_messages_',
	'msg_metadata_enabled_',
	'deferring_messages_enabled_',
	'store_out_msg_queue_size_', // Was not mentioned, but was assigned next to the previous two
	'recover_create_msg_', // Never assigned
	'mint_msg_', // Never assigned


	// Added for: check_in_queue()
	'neighbors_',
	'claimed_proc_lt_',
	'claimed_proc_hash_',


	// Added for: check_processed_upto()
	'processed_upto_updated_',
	'proc_lt_',
	'proc_hash_',
	'min_enq_lt_',
	'min_enq_hash_',
];

export default annotatedProperties;
