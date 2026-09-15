#ifndef OSD_HELPERS_RUNNING_AVERAGE_HPP
#define OSD_HELPERS_RUNNING_AVERAGE_HPP

#include <chrono>
#include <deque>
#include <sys/types.h>
#include <vector>

struct Stats {
    long min = 0;
    long max = 0;
    double average = 0.0;
    long sum = 0;
    uint count = 0;
};

class RunningAverage {
  public:
    RunningAverage(uint window_size_ms, uint bucket_size_ms);

    void add(long value);

    double ratePerSecondOverLastMs(uint last_ms) const;
    Stats statsOverLastMs(uint last_ms) const;

    std::vector<Stats> bucketStats() const;

  private:
    using Clock = std::chrono::steady_clock;

    struct Bucket {
        Clock::time_point timestamp;
        long sum;
        uint count;
        long min;
        long max;

        Bucket(Clock::time_point time, long value) : timestamp(time), sum(value), count(1), min(value), max(value) {}
    };

    const std::chrono::milliseconds window_size_;
    const std::chrono::milliseconds bucket_size_;

    std::deque<Bucket> buckets_;
};

#endif
