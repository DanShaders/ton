#include "my-threader.hpp"


namespace solution {



MyThreader::MyThreader() {
  futures.resize(MaxFuturesHeld);
  names.resize(MaxFuturesHeld);
  startTimes.resize(MaxFuturesHeld);
  endTimes.resize(MaxFuturesHeld);
}




// template<typename Callable>
void MyThreader::launch(LaunchFunction callable) {
  // callable();

  int fi = futuresCount++;
  futures[fi] = async(
      std::launch::async,
      callable
  );

  // f.get();
}


void MyThreader::launchAndProfile(string name, LaunchFunction callable) {
  int fi = futuresCount++;
  names[fi] = name;
  futures[fi] = async(
      std::launch::async,
      [this, fi, callable] {
        startTimes[fi] = high_resolution_clock::now();
        callable();
        endTimes[fi] = high_resolution_clock::now();
      }
  );
}


void MyThreader::waitForAll() {
  try {
    for (int i = 0; i < futuresCount; i++) {
      futures[i].get();
    }
    // Even when futures get added, futuresCount will increase accordingly
    // and the loop will continue until it runs out of all the futures
  } catch (...) {
    // throw caught exception
    throw;
  }
}


void MyThreader::writeProfileToFile(const string& filename, int testIndex) {
  ofstream file(filename, std::ios::app);

  // file << "export const timings = {" << endl;
  file << "'" << testIndex << "' : {" << endl;

  file << "\t'times': {" << endl;
  for (int i = 0; i < futuresCount; i++) {
    file << "\t\t"
      << "'" << names[i] << "': { "
      << "start: " << duration_cast<microseconds>(startTimes[i].time_since_epoch()).count()
      << ", end: " << duration_cast<microseconds>(endTimes[i].time_since_epoch()).count()
      << " }," << endl;
  }
  file << "\t}," << endl;

  file << "}," << endl;
  // file << "};" << endl;
  file.flush();
}


};

