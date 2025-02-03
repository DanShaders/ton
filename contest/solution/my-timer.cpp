#include "my-timer.hpp"



namespace solution {


RealTimer::RealTimer(string name, bool store_timings): store_timings(store_timings) {
  totalTime = 0s;
  startTimes.resize(MaxGrabs);
  endTimes.resize(MaxGrabs);
  // startTimes[1] = high_resolution_clock::now();
  // endTimes[10] = high_resolution_clock::now();
}

void RealTimer::writeToFile(const string& filename, int testIndex) {
  ofstream file(filename, std::ios::app);

  file << "'" << testIndex << "' : {" << endl;
  file << "\t'count': " << timer_grab_count << "," << endl;
  file << "\t'total': " << duration_cast<microseconds>(totalTime).count() << "," << endl;

  if (store_timings) {
    file << "\t'times': {" << endl;
    for (int i = 0; i < timer_grab_count; i++) {
      file << "\t\t"
        << "'" << i << "': { "
        << "'start': " << duration_cast<microseconds>(startTimes[i].time_since_epoch()).count()
        << ", 'end': " << duration_cast<microseconds>(endTimes[i].time_since_epoch()).count()
        << " }," << endl;
    }
    file << "\t}," << endl;
  }
  file << "}," << endl;
  file.flush();
}


void RealTimer::record_timing(timepoint start, timepoint end, int grab_id) {
  auto duration = end - start;

  if (store_timings) {
    // LOG(ERROR) << grab_id << " " << start.time_since_epoch().count() << " " << end.time_since_epoch().count();
    startTimes[grab_id] = start;
    endTimes[grab_id] = end;
    // LOG(ERROR) << "after";
  }

  lock_guard _(mtx);
  totalTime += duration;
}




RealTimerGrab::RealTimerGrab(RealTimer& timer): timer(timer) {
  // if (timer.store_timings) {
    my_index = timer.timer_grab_count++;
    if (my_index >= 900) {
      LOG(ERROR) << "Too many timer grabs";
      throw "Too many timer grabs";
    }
  // }
  startTime = high_resolution_clock::now();
}
void RealTimerGrab::stop() {
  stopped = true;
  auto endTime = high_resolution_clock::now();
  timer.record_timing(startTime, endTime, my_index);
}
RealTimerGrab::~RealTimerGrab() {
  if (!stopped)
    stop();
}


}


