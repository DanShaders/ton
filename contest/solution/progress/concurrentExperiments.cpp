

void test_concurrency_timing() {
  // {
  //   int sum = 0;
  //   for (int i = 0; i < 1000000000; i++) {
  //     for (int j = 0; j < 10; j++)
  //       sum += i + j;
  //   }
  //   LOG(ERROR) << sum;
  // }
  // -1792831488
  // Total time (only passed valid tests): 79.62190
  // Total CPU time (only passed valid tests): 78.97618


  // {
  //   atomic<int> sum = 0;
  //   auto f = [&sum](int l, int r) {
  //     for (int i = l; i < r; i++) {
  //       for (int j = 0; j < 10; j++)
  //         sum += i + j;
  //     }
  //   };
  //   auto a1 = async(std::launch::async, f,         0,  500000000);
  //   auto a2 = async(std::launch::async, f, 500000000, 1000000000);
  //   a1.get();
  //   a2.get();
  //   LOG(ERROR) << sum;
  // }
  // -1792831488
  // WoW! This version is incredibly slow! I only waited for 3 executions:
  // [ 1][t 1][2025-02-01 17:52:49.684061197][cvq-main.cpp:92][!validate]    -1792831488
  //   1  invalid-001.bin 84.55976 169.48874  OK     block is INVALID
  // [ 1][t 1][2025-02-01 17:54:14.291416946][cvq-main.cpp:92][!validate]    -1792831488
  //   2  invalid-002.bin 84.59675 168.66060  OK     block is INVALID
  // [ 1][t 1][2025-02-01 17:55:41.209873916][cvq-main.cpp:92][!validate]    -1792831488
  //   3  invalid-003.bin 86.90477 173.70245  OK     block is INVALID


  // {
  //   auto f = [](int l, int r) {
  //     int partSum = 0;
  //     for (int i = l; i < r; i++) {
  //       for (int j = 0; j < 10; j++)
  //         partSum += i + j;
  //     }
  //     return partSum;
  //   };
  //   auto a1 = async(std::launch::async, f,         0,  500000000);
  //   auto a2 = async(std::launch::async, f, 500000000, 1000000000);
  //   int sum = a1.get() + a2.get();
  //   LOG(ERROR) << sum;
  // }
  // -1792831488
  // Total time (only passed valid tests): 43.27562
  // Total CPU time (only passed valid tests): 76.10379


  {
    auto f = [](int l, int r) {
      int partSum = 0;
      for (int i = l; i < r; i++) {
        for (int j = 0; j < 10; j++)
          partSum += i + j;
      }
      return partSum;
    };
    vector<future<int>> futures;
    for (int i = 0; i < 10; i++)
      futures.push_back(async(std::launch::async, f, i * 100000000, (i + 1) * 100000000));
    int sum = 0;
    for (auto& fut : futures) {
      sum += fut.get();
    }
    LOG(ERROR) << sum;
  }
  // -1792831488
  // Total time (only passed valid tests): 15.53189
  // Total CPU time (only passed valid tests): 72.56326
}

