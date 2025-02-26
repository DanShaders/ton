#!/usr/bin/env python3

import re
import sys

# Hardcoded field names from ValidatorState
field_names = [
    'error_ctx_',
    'virt_roots_',
    'shard_',

     'verbosity',
     'pending',
     'id_',
     'prev_blocks',
     'prev_states',
     'block_data',
     'collated_data',
     'main_promise',
     'after_merge_',
     'after_split_',
     'before_split_',
     'want_split_',
     'want_merge_',
     'is_key_block_',
     'update_shard_cc_',
     'prev_key_block_exists_',
     'debug_checks_',
     'outq_cleanup_partial_',
     'prev_key_seqno_',
     'stage_',
     'shard_pfx_',
     'shard_pfx_len_',
     'created_by_',
     'prev_state_root_',
     'state_usage_tree_',
     'mc_state_',
     'mc_state_root_',
     'mc_blkid_',
     'mc_seqno_',
     'block_root_',
     'collated_roots_',
     'top_shard_descr_dict_',
     'extra_collated_data_',
     'have_extra_collated_data_', # ok
     'recover_create_msg_',
     'mint_msg_',
     'config_',
     'old_shard_conf_', # ok
     'new_shard_conf_',
     'wc_info_',
     'old_mparams_',
     'accept_msgs_',
     'min_shard_ref_mc_seqno_',
     'max_shard_lt_',
     'global_id_', # ok
     'vert_seqno_',
     'ihr_enabled_',
     'create_stats_enabled_',
      'prev_key_block_seqno_',
      'prev_key_block_',
      'prev_key_block_lt_',
      'block_limits_',

     'block_limit_status_',
     'total_gas_used_',
     'total_special_gas_used_',
     'start_lt_',
     'end_lt_',
     'now_',

     'rand_seed_',
     'storage_prices_',
     'storage_phase_cfg_',
     'compute_phase_cfg_',

     'action_phase_cfg_',
     'masterchain_create_fee_',
     'basechain_create_fee_',
     'neighbors_',

     'aux_mc_states_',
     'ps_',
     'ns_',
     'processed_upto_updated_',
     'sibling_out_msg_queue_',
     'sibling_processed_upto_',
     'block_create_count_',
     'block_create_total_',
     'in_msg_dict_',
     'out_msg_dict_',
     'account_blocks_dict_',
     'value_flow_',
     'import_created_',
     'transaction_fees_',
     'total_burned_',
     'fees_burned_',
     'import_fees_',

     'proc_lt_',
     'claimed_proc_lt_',
     'min_enq_lt_',
     'proc_hash_',
     'claimed_proc_hash_',
     'min_enq_hash_',
     'msg_proc_lt_',
     'msg_emitted_lt_',
     'removed_dispatch_queue_messages_',
     'new_dispatch_queue_messages_',
     'account_expected_defer_all_messages_',
     'old_out_msg_queue_size_',
     'out_msg_queue_size_known_',
     'have_out_msg_queue_size_in_state_',
     'msg_metadata_enabled_',
     'deferring_messages_enabled_',
     'store_out_msg_queue_size_',
     'processed_account_dispatch_queues_',
     'have_unprocessed_account_dispatch_queue_',
     'result_state_update_'
]

def find_function_bodies(content):
    """Find all function bodies in the content."""
    # Match function bodies: anything between { and } with proper nesting
    bodies = []
    current_pos = content.find('{') + 1
    while True:
        # Find opening brace
        start = content.find('{', current_pos)
        if start == -1:
            break

        # Count braces to handle nesting
        brace_count = 1
        pos = start + 1
        while brace_count > 0 and pos < len(content):
            if content[pos] == '{':
                brace_count += 1
            elif content[pos] == '}':
                brace_count -= 1
            pos += 1

        if brace_count == 0:
            # Found a complete function body
            bodies.append((start, pos))
            current_pos = pos
        else:
            current_pos = start + 1

    return bodies

def process_file(input_file):
    with open(input_file, 'r') as f:
        content = f.read()

    # Find all function bodies
    function_bodies = find_function_bodies(content)

    # Process each function body
    offset = 0  # Track position changes from replacements
    total_replacements = 0

    for start, end in function_bodies:
        # Adjust positions based on previous replacements
        adj_start = start + offset
        adj_end = end + offset

        body = content[adj_start:adj_end]

        # Process each field name in the function body
        for field in field_names:
            # Look for field with a preceding space, newline, or opening parenthesis
            pattern = r'(?<=[\s\n(!\-+{\*&])' + re.escape(field) + r'\b'
            matches = list(re.finditer(pattern, body))

            # Process matches in reverse to maintain correct positions
            for match in reversed(matches):
                pos = match.start()

                # Skip if already prefixed
                prefix_check = body[max(0, pos-7):pos].strip()
                if prefix_check.endswith('state_.'):
                    continue

                # Skip if in a comment
                line_start = body.rfind('\n', 0, pos) + 1
                line = body[line_start:body.find('\n', pos)]
                if '//' in line[:pos-line_start]:
                    continue

                # Replace the field
                replacement = 'state_.' + field
                body = body[:pos] + replacement + body[pos + len(field):]
                total_replacements += 1

                # Get line number for reporting
                line_num = content.count('\n', 0, adj_start + pos) + 1
                print(f"Line {line_num}: Replacing {field} with {replacement}")
                print(f"Context: {line.strip()}")

        # Update the content with modified function body
        content = content[:adj_start] + body + content[adj_end:]
        offset += len(body) - (end - start)

    print(f"\nWriting {input_file} with {total_replacements} total replacements...")
    with open(input_file, 'w') as f:
        f.write(content)

def main():
    inc_file = 'solution/ContestValidator.cpp'
    print(f"Processing {inc_file}...")
    process_file(inc_file)
    print("\nRefactoring complete!")

if __name__ == '__main__':
    main()
