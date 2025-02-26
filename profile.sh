set -x;

BUILD_DIR="build_default"

cd ../$BUILD_DIR
cmake --build . --target contest-grader -j32
cd ..
LD_PRELOAD=/lib/libc6-prof/x86_64-linux-gnu/libc.so.6 $BUILD_DIR/contest/grader/contest-grader --threads 8 --tests tests & 
/usr/lib/linux-tools/5.15.0-130-generic/perf record -F 99 -g -p $(pgrep -n contest-grader) -- sleep 120;
pkill -f contest-grader
rm -rf prev_perf.folded
mv perf.folded prev_perf.folded
/usr/lib/linux-tools/5.15.0-130-generic/perf script --no-inline | tee profile_log.txt | sed 's/#0:cpu#./kek/' | FlameGraph/stackcollapse-perf.pl >perf.folded
FlameGraph/flamegraph.pl < perf.folded > profile.svg
FlameGraph/difffolded.pl prev_perf.folded perf.folded | FlameGraph/flamegraph.pl > diff_profile.svg