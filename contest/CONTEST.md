TON Block Validation Challenge
Overview
In the TON blockchain, validators are responsible for maintaining the network by generating and validating blocks.
Effective validation of new blocks is crucial for the performance of the network. Your task in this contest is to optimize the algorithm for block validation.

The TON Blockchain consists of multiple shards, each containing a subset of accounts. All states of these accounts, along with queues of pending messages and various metadata, are stored in the shard's shard state.

A block in TON is a fundamental data structure that represents a transition between the previous and the next shard state. It consists of:
1. A list of transactions.
2. A list of messages created or processed in the block.
3. Metadata, such as a timestamp and identifiers of previous blocks.
4. Merkle update of the shard state, which represents the difference between the previous and the next shard states.

To verify the validity of the block, validators take the previous shard state and perform multiple checks, such as:
1. Verifying metadata.
2. Executing all transactions in the block and generating the updated account states.
3. Checking the list of messages, confirming that messages from the queues and newly generated messages are processed in the correct order.
4. Verifying the Merkle update, checking that the new shard state reflects all changes in account states and message queues.

Task description
In this task, you will be given the block and the collated data. Collated data contains Merkle proofs of all required shard states, therefore eliminating the need to have the full shard states. The block will be given without the Merkle update.
In addition to validating the block, your task is to reconstruct the new shard state and output the Merkle update.

You are given the reference implementation based on the original block validation algorithm in TON.
Your task is to optimize the implementation. It should be consistent with the reference algorithm and will be scored based on execution time.

You will have full access to the main TON repository, meaning that you can use and modify existing code for block validation.

Technical details
Solution format
Clone the ton repository and checkout the branch:

git clone --recursive https://github.com/ton-blockchain/ton.git
cd ton
git checkout validation-contest
Your solution will be the contest/solution/contest-solution CMake target.
The entry point of the solution is run_contest_solution in contest/solution/solution.cpp. It takes:
1. block_id - id of the block (shard, seqno and hashes).
2. block_data - serialized block data, but without state update (it is replaced with an empty cell).
3. collated_data - serialized collated data (Merkle proofs of shard states).
4. promise - the Promise object to return the result to.
4a) If the block is valid, the solution should return the serialized Merkle update.
4b) If the block is invalid, the solution should return any td::Status::Error.

The reference solution is implemented in contest/solution/contest-validate-query.cpp. It is based on the original validation algorithm (see validator/impl/validate-query.cpp), but with the following differences:
1. It uses shard states from the collated data instead of the database.
2. It not only validates the block, but also reconstructs the Merkle update.
2a) Block hashes are not checked, because the block is modified (no Merkle update).
2b) Obviously, the new shard state is not checked, because it is not given.
2c) “Overload”/“underload” flags and “processed upto” for the new state are given in the collated data because they cannot be reliably reconstructed.
3. Only basechain blocks (workchain=0) are supported. No masterchain blocks will be given in this contest.
4. Outbound queue size is expected to be present in the previous shard state, capStoreOutMsgQueueSize flag is expected to be set.

You can use and modify any files in the repository.
To submit your solution, create a git patch of the repository and submit it to the testing system. Please include a brief description of the optimizations you made.

Multi-threading
The original implementation of the validation algorithm runs in one thread.
Your solution, however, can create multiple threads to run validation in parallel.
The number of available CPU cores in the testing environment will be 8.

Note: the reference implementation uses vm::CellUsageTree to build the Merkle update. CellUsageTree is not thread-safe. Solving this issue is a part of the task if you choose to implement a multi-threaded solution.

Scoring
Your solution will be executed on a set of tests, which contains both valid and invalid blocks.
The solution passes the test if it correctly answers whether the block is valid and (if valid) outputs the correct Merkle update.

For each test, the real running time and the total CPU time for all threads is measured.
The final result consists of:
1. The number of passed tests.
2. The sum of the real time for all passed tests where the block is valid.
3. The sum of the CPU time for all passed tests where the block is valid.

There will be two leaderboards: one for the total real time, one for the total CPU time.
1. In each leaderboard the participants are ranked by the number of passed tests.
2. Participants with an equal number of passed tests are ranked by total time.

Note: tests with invalid blocks do not affect the total time. However, each test will have a time limit of 5 seconds.

Local run
Download and extract archive validation-contest-tests.zip.

Build and run your solution:

sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build zlib1g-dev libsecp256k1-dev libmicrohttpd-dev libsodium-dev liblz4-dev autoconf libtool libssl-dev

mkdir build && cd build
cmake ../ton -DCMAKE_BUILD_TYPE=Release
cmake --build . --target contest-grader
./contest/grader/contest-grader --threads 8 --tests <tests-dir>
The grader outputs real and CPU time for all tests.
