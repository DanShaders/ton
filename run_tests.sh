#!/bin/bash -e

D=`realpath ./test_data/tests`
echo "$D"

run_grader() {
  taskset -c 0-0 build/contest/grader/contest-grader --threads 1 --tests $D
}

N=10

for i in `seq $N`; do
  rm -f log$i
done

for i in `seq $N`; do
  run_grader 2>&1 | grep CPU | tail -n 1 | cut -d ' ' -f 8 >log$i
done

sum=0
for i in `seq $N`; do
  num=$(cat "log$i")
  sum=$(bc <<< "$sum + $num")
done

A=$(bc -l <<< "1000 * $sum/$N")

echo "average = $A"


