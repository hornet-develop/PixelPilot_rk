#include "running_average.hpp"

#include <algorithm>
#include <cassert>

RunningAverage::RunningAverage(uint window_size_ms) : window_size_(window_size_ms) {
    assert(window_size_ms > 0);
}

void RunningAverage::add(long value, Clock::time_point timestamp) {
    samples_.push_back({timestamp, value});
    const auto cutoff = timestamp - window_size_;
    while (!samples_.empty() && samples_.front().timestamp < cutoff) {
        samples_.pop_front();
    }
}

void RunningAverage::clear() {
    samples_.clear();
}

Stats RunningAverage::statsOverLastMs(uint last_ms) const {
    const auto window = std::chrono::milliseconds(last_ms);
    assert(window <= window_size_);

    const auto now = Clock::now();
    const auto cutoff = now - window;

    Stats stats;
    for (auto it = samples_.rbegin(); it != samples_.rend(); ++it) {
        if (it->timestamp < cutoff) {
            break;
        }
        if (stats.count == 0) {
            stats.min = it->value;
            stats.max = it->value;
        } else {
            stats.min = std::min(stats.min, it->value);
            stats.max = std::max(stats.max, it->value);
        }
        stats.sum += it->value;
        ++stats.count;
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