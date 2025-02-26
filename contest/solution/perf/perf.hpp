#pragma once

#include "perf_flags.hpp"

#include <cstdint>
#include <array>
#include <vector>
#include <fstream>
#include <memory>
#include <cxxabi.h>
#include <chrono>

#ifdef MEASURE_PERF

namespace perf {

inline uint64_t get_tsc() {
    #ifdef _MSC_VER
    return __rdtsc();
    #else
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
    #endif
}

template<size_t FirstBufferSize = 5, size_t LastBufferSize = 1000>
struct EventMetrics {
    std::array<uint64_t, FirstBufferSize> first_samples{};  // First BufferSize samples
    std::array<uint64_t, LastBufferSize> last_samples{};   // Ring buffer for last BufferSize samples
    size_t first_samples_count = 0;  // Number of samples in first_samples
    size_t last_samples_idx = 0;     // Current position in last_samples ring buffer
    bool first_samples_filled = false;  // Flag indicating if first_samples is full
    uint64_t count = 0;          // Total number of measurements
};

struct BaseEventTracker {
    virtual void reset() = 0;
    virtual void write_samples_to_file(std::ofstream& file) = 0;
    virtual ~BaseEventTracker() = default;
};

template<typename Tag, size_t FirstBufferSize = 5, size_t LastBufferSize = 1000>
struct EventTracker : public BaseEventTracker {
    static EventMetrics<FirstBufferSize, LastBufferSize> metrics;
    
    static uint64_t start() {
        return get_tsc();
    }
    
    static void stop(uint64_t start_time) {
        uint64_t duration = get_tsc() - start_time;
        
        if (!metrics.first_samples_filled) {
            metrics.first_samples[metrics.first_samples_count] = duration;
            metrics.first_samples_count++;
            if (metrics.first_samples_count == FirstBufferSize) {
                metrics.first_samples_filled = true;
            }
        } else {
            metrics.last_samples[metrics.last_samples_idx] = duration;
            metrics.last_samples_idx = (metrics.last_samples_idx + 1) % LastBufferSize;
        }
        metrics.count++;
    }
    
    void reset() override {
        metrics = EventMetrics<FirstBufferSize, LastBufferSize>();
    }
    
    static uint64_t get_count() { return metrics.count; }
    
    void write_samples_to_file(std::ofstream& file) override {
        if (metrics.count > 0) {
            std::string event_name = demangle(typeid(Tag).name());
            const std::string prefix = "perf::PerfEvent_";
            if (event_name.substr(0, prefix.length()) == prefix) {
                event_name = event_name.substr(prefix.length());
            }
            auto samples = get_all_samples();
            
            constexpr uint64_t tsc_freq = 6'200'000'000;  // 6.2GHz TSC frequency from /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq
            for (const auto& [sample, is_first] : samples) {
                file << event_name << ","
                     << sample << ","
                     << (sample * 1'000'000 / tsc_freq) << ","  // Convert TSC to microseconds
                     << (is_first ? "LAST" : "LAST") << "\n";
            }
        }
    }
    
    static std::vector<std::pair<uint64_t, bool>> get_all_samples() {
        std::vector<std::pair<uint64_t, bool>> samples;
        samples.reserve(FirstBufferSize + LastBufferSize);
        
        size_t first_count = metrics.first_samples_filled ? FirstBufferSize : metrics.first_samples_count;
        for (size_t i = 0; i < first_count; ++i) {
            samples.emplace_back(metrics.first_samples[i], true);
        }
        
        if (metrics.first_samples_filled) {
            if (metrics.count < FirstBufferSize + LastBufferSize) {
                for (size_t i = 0; i < metrics.last_samples_idx; ++i) {
                    samples.emplace_back(metrics.last_samples[i], false);
                }
            } else {
                for (size_t i = metrics.last_samples_idx; i < LastBufferSize; ++i) {
                    samples.emplace_back(metrics.last_samples[i], false);
                }
                for (size_t i = 0; i < metrics.last_samples_idx; ++i) {
                    samples.emplace_back(metrics.last_samples[i], false);
                }
            }
        }
        
        return samples;
    }

    static std::string demangle(const char* name) {
        #ifdef __GNUG__
        int status = -1;
        std::unique_ptr<char, void(*)(void*)> res {
            abi::__cxa_demangle(name, nullptr, nullptr, &status),
            std::free
        };
        return (status == 0) ? res.get() : name;
        #else
        return name;
        #endif
    }
};

class EventRegistry {
public:
    static EventRegistry& instance() {
        static EventRegistry registry;
        return registry;
    }

    void register_event(BaseEventTracker* tracker) {
        trackers.push_back(tracker);
    }

    void reset_all_stats() {
        for (auto tracker : trackers) {
            tracker->reset();
        }
    }

    void flush_to_file(const std::string& path) {
        std::ofstream file;
        file.open(path, std::ios_base::app);
        
        if (!file.is_open()) {
            throw std::runtime_error("Could not open file: " + path);
        }
        
        file.seekp(0, std::ios::end);
        
        for (auto tracker : trackers) {
            tracker->write_samples_to_file(file);
        }
        
        file.close();
    }

private:
    EventRegistry() = default;
    std::vector<BaseEventTracker*> trackers;
};

inline void reset_all_stats() {
    EventRegistry::instance().reset_all_stats();
}

inline void clear_stats_file(const std::string& path) {
    std::ofstream file;
    file.open(path, std::ios_base::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + path);
    }
    file << "Event,TSC,UsDuration,Type\n";
    file.close();
}

inline void flush_to_file(const std::string& path) {
    EventRegistry::instance().flush_to_file(path);
}


template<typename Tag, size_t FirstBufferSize, size_t LastBufferSize>
EventMetrics<FirstBufferSize, LastBufferSize> EventTracker<Tag, FirstBufferSize, LastBufferSize>::metrics{};

// RAII wrapper for automatic measurement
template<typename Tag, size_t FirstBufferSize = 5, size_t LastBufferSize = 1000>
class ScopedEvent {
public:
    ScopedEvent() {
        EventTracker<Tag, FirstBufferSize, LastBufferSize>::start();
    }
    
    ~ScopedEvent() {
        EventTracker<Tag, FirstBufferSize, LastBufferSize>::stop();
    }
    
    ScopedEvent(const ScopedEvent&) = delete;
    ScopedEvent& operator=(const ScopedEvent&) = delete;
    ScopedEvent(ScopedEvent&&) = delete;
    ScopedEvent& operator=(ScopedEvent&&) = delete;
};

template<typename Tag>
class OwnedEventTimer {
public:
    OwnedEventTimer(): start_time(EventTracker<Tag>::start()) { }
    
    void stop() {
        if (!stopped_) {
            EventTracker<Tag>::stop(this->start_time);
            stopped_ = true;
        }
    }
    
    ~OwnedEventTimer() {
        if (!stopped_) {
            EventTracker<Tag>::stop(this->start_time);
        }
    }
    
    OwnedEventTimer(const OwnedEventTimer&) = delete;
    OwnedEventTimer& operator=(const OwnedEventTimer&) = delete;
    OwnedEventTimer(OwnedEventTimer&&) = delete;
    OwnedEventTimer& operator=(OwnedEventTimer&&) = delete;

private:
    bool stopped_ = false;
    uint64_t start_time;
};


} // namespace perf



#define DEFINE_PERF_EVENT(name) \
    namespace perf { \
        struct PerfEvent_##name {}; \
        using Event_##name = PerfEvent_##name; \
        static EventTracker<PerfEvent_##name>* tracker_##name = []() { \
            auto tracker = new EventTracker<PerfEvent_##name>(); \
            EventRegistry::instance().register_event(tracker); \
            return tracker; \
        }(); \
    }

#define CREATE_PERF_TIMER(name) \
    ::perf::OwnedEventTimer<perf::PerfEvent_##name>()

#else

namespace perf {

template<typename Tag>
class OwnedEventTimer {
public:
    OwnedEventTimer() = default;
    
    void stop() {
    }

    OwnedEventTimer(const OwnedEventTimer&) = delete;
    OwnedEventTimer& operator=(const OwnedEventTimer&) = delete;
    OwnedEventTimer(OwnedEventTimer&&) = delete;
    OwnedEventTimer& operator=(OwnedEventTimer&&) = delete;
};

}

#define DEFINE_PERF_EVENT(name) {}
#define CREATE_PERF_TIMER(name) {}

#endif
