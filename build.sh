set -xe

# export ASAN_OPTIONS="verify_asan_link_order=0,halt_on_error=0"
# cd ..
# # rm -rf build_asan
# mkdir -p build_asan
# cd build_asan 
# # cmake ../ton -DTON_USE_ASAN=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=/usr/bin/clang++-14
# cmake ../ton -DTON_USE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug


# cd ..
# # rm -rf build_tsan
# mkdir -p build_tsan
# cd build_tsan
# cmake ../ton -DTON_USE_TSAN=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=/usr/bin/clang++-14

cd ..
# rm -rf build_default
mkdir -p build_default
cd build_default
cmake ../ton -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=/usr/bin/clang++-14


# cd ..
# # rm -rf build_debug
# mkdir -p build_debug
# cd build_debug
# cmake ../ton -DCMAKE_BUILD_TYPE=Debug


# cmake ../ton -DCMAKE_BUILD_TYPE=Debug
cmake --build . --target contest-grader -j32
contest/grader/contest-grader --threads 8 --tests ../tests