#!/bin/bash
set -e

# Disable TSAN since we're using Valgrind
# export TON_USE_TSAN=1

ulimit -s unlimited
cd /home/evgenstf/ton/debug_build
export CXX=g++
export CC=gcc

cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target contest-grader -j 20

# Run with Valgrind
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --verbose \
         --log-file=valgrind-out.txt \
         ./contest/grader/contest-grader --threads 1 --tests /home/evgenstf/ton/contest/tests 