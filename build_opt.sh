#!/bin/bash -e

rm -rf build
mkdir build

cd build

cmake -GNinja \
  -DCMAKE_BUILD_TYPE=Release -DPORTABLE=1 -DTON_ARCH= -DTON_USE_JEMALLOC=ON ..

ninja -j130 contest-grader

cd ..

./run_tests.sh

