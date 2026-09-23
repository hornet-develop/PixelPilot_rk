#ifndef OSD_HELPERS_RUNNING_AVERAGE_HPP
#define OSD_HELPERS_RUNNING_AVERAGE_HPP

#include <chrono>
#include <deque>
#include <sys/types.h>

struct Stats {
    long min = 0;
    long max = 0;
    double average = 0.0;
    long sum = 0;
    uint count = 0;
};

class RunningAverage {
  public:
    using Clock = std::chrono::steady_clock;
    using Timestamp = Clock::time_point;

    explicit RunningAverage(uint window_size_ms);

    void add(long value, Timestamp timestamp);
    void clear();

    double ratePerSecondOverLastMs(uint last_ms) const;
    Stats statsOverLastMs(uint last_ms) const;

  private:
    struct Sample {
        Timestamp timestamp;
        long value;
    };

    const std::chrono::milliseconds window_size_;
    std::deque<Sample> samples_;
};

#endif
