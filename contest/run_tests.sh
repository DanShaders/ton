set -e

# export CMAKE_CXX_COMPILER_ID="GNU"

ulimit -s unlimited
cd /home/evgenstf/ton/build
export CXX=g++
export CC=gcc

cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target contest-grader -j 20

taskset -c 2-10 ./contest/grader/contest-grader --threads 8 --tests /home/evgenstf/ton/contest/tests
