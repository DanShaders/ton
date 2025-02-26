set -e

# export CMAKE_CXX_COMPILER_ID="GNU"

ulimit -s unlimited
cd /home/evgenstf/ton/build
export CXX=g++
export CC=gcc

cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target sandbox -j 20

taskset -c 6,7 ./contest/sandbox/sandbox -t 2
