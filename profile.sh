#!/bin/bash
cd build
LLVM_PROFILE_FILE="profile.profraw" contest/grader/contest-grader --threads 8 --tests ~/Downloads/Telegram\ Desktop/tests
llvm-profdata merge -sparse profile.profraw -o profile.profdata
llvm-cov show contest/grader/contest-grader \
  -instr-profile=profile.profdata \
  -object=contest/grader/contest-grader \
  -output-dir=coverage-report \
  -format=html

echo "report generated in build/coverage-report/index.html"