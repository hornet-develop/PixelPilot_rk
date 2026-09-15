#include "running_average.hpp"

#include <algorithm>
#include <cassert>

RunningAverage::RunningAverage(uint window_size_ms, uint bucket_size_ms)
    : window_size_(window_size_ms), bucket_size_(bucket_size_ms) {
    assert(window_size_ms > 0);
    assert(bucket_size_ms > 0);
    assert(window_size_ms >= bucket_size_ms);
}

void RunningAverage::add(long value) {
    const auto now = Clock::now();
    while (!buckets_.empty() && now - buckets_.front().timestamp > window_size_) {
        buckets_.pop_front();
    }
    if (!buckets_.empty() && now - buckets_.back().timestamp < bucket_size_) {
        auto &bucket = buckets_.back();
        bucket.sum += value;
        ++bucket.count;
        bucket.min = std::min(bucket.min, value);
        bucket.max = std::max(bucket.max, value);
    } else {
        buckets_.emplace_back(now, value);
    }
}

Stats RunningAverage::statsOverLastMs(uint last_ms) const {
    const auto window = std::chrono::milliseconds(last_ms);
    assert(window <= window_size_);

    const auto now = Clock::now();

    Stats stats;
    for (auto it = buckets_.rbegin(); it != buckets_.rend(); ++it) {
        if (now - it->timestamp > window) {
            break;
        }
        if (stats.count == 0) {
            stats.min = it->min;
            stats.max = it->max;
        } else {
            stats.min = std::min(stats.min, it->min);
            stats.max = std::max(stats.max, it->max);
        }
        stats.sum += it->sum;
        stats.count += it->count;
    }
    if (stats.count > 0) {
        stats.average = static_cast<double>(stats.sum) / stats.count;
    }
    return stats;
}

double RunningAverage::ratePerSecondOverLastMs(uint last_ms) const {
    if (last_ms == 0) {
        return 0.0;
    }
    const Stats stats = statsOverLastMs(last_ms);
    const double seconds = static_cast<double>(last_ms) / 1000.0;
    return static_cast<double>(stats.sum) / seconds;
}

std::vector<Stats> RunningAverage::bucketStats() const {
    const auto now = Clock::now();

    std::vector<Stats> result;
    result.reserve(buckets_.size());
    for (const auto &bucket : buckets_) {
        if (now - bucket.timestamp > window_size_) {
            continue;
        }
        result.push_back(Stats{
            bucket.min,
            bucket.max,
            static_cast<double>(bucket.sum) / bucket.count,
            bucket.sum,
            bucket.count,
        });
    }
    return result;
}