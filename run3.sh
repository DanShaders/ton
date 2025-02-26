set -x;

./build.sh >log.txt
./build.sh >>log.txt
./build.sh >>log.txt
grep 'Total' log.txt