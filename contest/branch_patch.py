#!/usr/bin/env python3

import sys
import re
from dataclasses import dataclass
from typing import List, Dict, Optional

@dataclass
class BranchInfo:
    line_number: int
    taken_counts: List[int]
    is_if_statement: bool = False

def parse_gcov_file(gcov_path: str) -> Dict[int, BranchInfo]:
    branch_data: Dict[int, BranchInfo] = {}
    current_line = None

    with open(gcov_path, 'r') as f:
        for line in f:
            # Parse line number
            line_match = re.match(r'\s*(\d+):\s*(\d+):', line)
            if line_match:
                current_line = int(line_match.group(2))
                continue

            # Parse branch information
            branch_match = re.match(r'branch\s+(\d+)\s+taken\s+(\d+)', line)
            if branch_match and current_line:
                branch_num = int(branch_match.group(1))
                taken_count = int(branch_match.group(2))

                if current_line not in branch_data:
                    branch_data[current_line] = BranchInfo(current_line, [])

                branch_data[current_line].taken_counts.append(taken_count)

    return branch_data

def analyze_branch_probability(branch_info: BranchInfo) -> Optional[float]:
    if len(branch_info.taken_counts) != 2:
        return None

    total = sum(branch_info.taken_counts)
    if total == 0:
        return None

    return branch_info.taken_counts[0] / total

def find_if_statements(cpp_content: str) -> Dict[int, str]:
    if_positions: Dict[int, str] = {}
    lines = cpp_content.split('\n')

    for line_num, line in enumerate(lines, 1):
        # Match if statements, excluding else if
        if_match = re.match(r'^(\s*)if\s*\((.*)\)', line)
        if if_match and 'else if' not in line:
            if_positions[line_num] = if_match.group(2)

    return if_positions

def patch_code(cpp_path: str, gcov_path: str, output_path: str):
    # Read files
    with open(cpp_path, 'r') as f:
        cpp_content = f.read()

    # Parse GCOV data
    branch_data = parse_gcov_file(gcov_path)

    # Find if statements in the code
    if_statements = find_if_statements(cpp_content)

    # Process line by line
    lines = cpp_content.split('\n')
    modified_lines = lines.copy()

    for line_num, condition in if_statements.items():
        if line_num in branch_data:
            probability = analyze_branch_probability(branch_data[line_num])

            # If probability is less than 5%, add UNLIKELY macro
            if probability is not None and probability < 0.05:
                line = lines[line_num - 1]
                modified_line = re.sub(
                    r'if\s*\((.*)\)',
                    lambda m: f'if (UNLIKELY({m.group(1)}))',
                    line
                )
                modified_lines[line_num - 1] = modified_line

    # Write modified content
    with open(output_path, 'w') as f:
        f.write('\n'.join(modified_lines))

def main():
    if len(sys.argv) != 4:
        print("Usage: script.py <cpp_file> <gcov_file> <output_file>")
        sys.exit(1)

    cpp_path = sys.argv[1]
    gcov_path = sys.argv[2]
    output_path = sys.argv[3]

    patch_code(cpp_path, gcov_path, output_path)

if __name__ == "__main__":
    main()
