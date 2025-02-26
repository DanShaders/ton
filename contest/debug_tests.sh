set -e

# export CMAKE_CXX_COMPILER_ID="GNU"

export TON_USE_TSAN=1

ulimit -s unlimited
cd /home/evgenstf/ton/debug_build
export CXX=g++
export CC=gcc

cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target contest-grader -j 20

taskset -c 6 ./contest/grader/contest-grader --threads 3 --tests /home/evgenstf/ton/contest/tests
