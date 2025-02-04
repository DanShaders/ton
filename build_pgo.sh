#!/bin/bash -e

rm -rf build
mkdir build

PGO_DIR=`realpath pgo_dir`
rm -rf $PGO_DIR
mkdir -p $PGO_DIR

CXX_FLAGS="-fprofile-generate=$PGO_DIR -Wno-unused-command-line-argument --rtlib=compiler-rt -Wno-unused-command-line-argument -lunwind"

cd build

cmake -GNinja -DCMAKE_CXX_FLAGS="$CXX_FLAGS" -DCMAKE_C_FLAGS="$CXX_FLAGS" \
  -DCMAKE_BUILD_TYPE=Release -DPORTABLE=1 -DTON_ARCH= -DTON_USE_JEMALLOC=ON ..

ninja -j130 contest-grader

cd ..

./run_tests.sh

ls "$PGO_DIR"

LLVM_PROFDATA=/usr/lib/llvm-14/bin/llvm-profdata

(cd $PGO_DIR && $LLVM_PROFDATA merge -output=default.profdata *.profraw)


CXX_FLAGS="-fprofile-use=$PGO_DIR -Wno-unused-command-line-argument --rtlib=compiler-rt -Wno-unused-command-line-argument -lunwind"

cd build
rm -rf CMakeCache.txt

cmake -GNinja -DCMAKE_CXX_FLAGS="$CXX_FLAGS" -DCMAKE_C_FLAGS="$CXX_FLAGS" \
  -DCMAKE_BUILD_TYPE=Release -DPORTABLE=1 -DTON_ARCH= -DTON_USE_JEMALLOC=ON ..

ninja -j130 contest-grader

cd ..

./run_tests.sh
