#pragma once

#include <vector>
#include <string>
#include <functional>
#include <future>
#include <chrono>
#include <fstream>

#include "various.hpp"
#include "settings.hpp"


namespace solution {

using namespace std::chrono_literals;

using std::mutex;
using std::lock_guard;
using std::async;
using std::future;
using std::vector;
using std::string;
using std::atomic;
using std::ofstream;
// using std::ios;
using std::endl;
using std::chrono::microseconds;
using std::chrono::nanoseconds;
using std::chrono::duration_cast;
using std::chrono::high_resolution_clock;
using std::chrono::_V2::system_clock;



using timepoint = system_clock::time_point;


#ifdef ENABLE_PROFILING
  #define MyTimer RealTimer
  #define TimerGrab RealTimerGrab
#else
  #define MyTimer FakeTimer
  #define TimerGrab FakeTimerGrab
#endif


class RealTimer {
 public:
  RealTimer(string name, bool store_timings);
  void writeToFile(const string& filename, int testIndex);


 private:
  void record_timing(timepoint start, timepoint end, int grab_id);

  bool store_timings;
  atomic<int> timer_grab_count{0};

  const int MaxGrabs = 1000;
  mutex mtx;
  nanoseconds totalTime;
  vector<timepoint> startTimes;
  vector<timepoint> endTimes;

  friend class RealTimerGrab;
};


class RealTimerGrab {
 public:
  RealTimerGrab(RealTimer& timer);
  ~RealTimerGrab();
  void stop();

 private:
  bool stopped = false;
  RealTimer& timer;
  int my_index;
  timepoint startTime;
};


class FakeTimer {
 public:
  FakeTimer(string name, bool store_timings) { }
  void writeToFile(const string& filename, int testIndex) { }
};
class FakeTimerGrab {
 public:
  FakeTimerGrab(FakeTimer& timer) { }
  void stop() { }
};




}