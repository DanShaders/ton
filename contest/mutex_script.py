#!/usr/bin/env python3

import re
import sys


# Hardcoded field names from ValidatorState
field_names = [
  "state_.start_lt_",
  "state_.storage_prices_",
  "state_.storage_phase_cfg_",
  # "account_expected_defer_all_messages_",
  "state_.in_msg_dict_",
  "state_.out_msg_dict_",
  "state_.recover_create_msg_",
  "state_.mint_msg_",
  "state_.msg_proc_lt_",
  "state_.ps_",
  "state_.ns_",
  "state_.total_burned_",
  "state_.total_gas_used_",
  "state_.total_special_gas_used_",
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

                # Skip if in a comment
                line_start = body.rfind('\n', 0, pos) + 1
                line = body[line_start:body.find('\n', pos)]
                if '//' in line[:pos-line_start]:
                    continue

                # Replace the field
                replacement = field + '.lock()->'
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
    inc_file = 'solution/AccountCheckActor.hpp'
    print(f"Processing {inc_file}...")
    process_file(inc_file)
    print("\nRefactoring complete!")

if __name__ == '__main__':
    main()
