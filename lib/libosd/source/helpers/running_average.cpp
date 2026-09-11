#include "running_average.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <limits>

RunningAverage::RunningAverage(int window_size_ms, int bucket_size_ms)
    : window_size(window_size_ms), bucket_size(bucket_size_ms), sum(0), count(0) {
    assert(window_size_ms >= bucket_size_ms);
}

long RunningAverage::add(long value) {
    auto now = std::chrono::steady_clock::now();
    auto current_time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    // Remove outdated buckets
    while (!buckets.empty() && (current_time - buckets.front().timestamp > window_size)) {
        sum -= buckets.front().sum;
        count -= buckets.front().count;
        buckets.pop_front();
    }

    // Add the value to the current bucket
    if (!buckets.empty() && (current_time - buckets.back().timestamp < bucket_size)) {
        buckets.back().sum += value;
        buckets.back().count += 1;
        buckets.back().min_value = std::min(buckets.back().min_value, value);
        buckets.back().max_value = std::max(buckets.back().max_value, value);
    } else {
        buckets.emplace_back(current_time, value);
    }

    // Update the running sum and count
    sum += value;
    count++;

    return count > 0 ? sum / count : 0;
}

double RunningAverage::average_over_last_ms(uint last_ms) const {
    long min = std::numeric_limits<long>::max();
    long max = std::numeric_limits<long>::min();
    long last_sum;
    int last_count;
    calculate_stats_in_window(last_ms, last_sum, last_count, min, max);

    return last_count > 0 ? static_cast<double>(last_sum) / last_count : 0.0;
}

double RunningAverage::rate_per_second_over_last_ms(uint last_ms) const {
    long min = std::numeric_limits<long>::max();
    long max = std::numeric_limits<long>::min();
    long last_sum;
    int last_count;
    calculate_stats_in_window(last_ms, last_sum, last_count, min, max);

    double elapsed_seconds = static_cast<double>(last_ms) / 1000.0;
    return elapsed_seconds > 0 ? static_cast<double>(last_sum) / elapsed_seconds : 0.0;
}

void RunningAverage::get_stats_over_last_ms(uint last_ms, long &min, long &max, double &average) const {
    long last_sum;
    int last_count;

    min = std::numeric_limits<long>::max();
    max = std::numeric_limits<long>::min();

    calculate_stats_in_window(last_ms, last_sum, last_count, min, max);

    average = last_count > 0 ? static_cast<double>(last_sum) / last_count : 0.0;
}

// New method to return Stats struct with sum and count
Stats RunningAverage::get_stats_over_last_ms_result(uint last_ms) const {
    long min = std::numeric_limits<long>::max();
    long max = std::numeric_limits<long>::min();
    long last_sum = 0;
    int last_count = 0;

    calculate_stats_in_window(last_ms, last_sum, last_count, min, max);

    double average = last_count > 0 ? static_cast<double>(last_sum) / last_count : 0.0;
    return Stats(min, max, average, last_sum, last_count);
}

std::vector<long> RunningAverage::get_bucket_sums() const {
    std::vector<long> sums;
    sums.reserve(buckets.size());
    for (const auto &bucket : buckets) {
        sums.push_back(bucket.sum);
    }
    return sums;
}

std::vector<Stats> RunningAverage::get_bucket_stats() const {
    std::vector<Stats> stats;
    stats.reserve(buckets.size());
    for (const auto &bucket : buckets) {
        double average = bucket.count > 0 ? static_cast<double>(bucket.sum) / bucket.count : 0.0;
        stats.push_back(Stats(bucket.min_value, bucket.max_value, average, bucket.sum, bucket.count));
    }
    return stats;
}

void RunningAverage::calculate_stats_in_window(uint last_ms, long &sum_out, int &count_out, long &min_out,
                                               long &max_out) const {
    auto now = std::chrono::steady_clock::now();
    auto current_time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    sum_out = 0;
    count_out = 0;

    for (auto it = buckets.rbegin(); it != buckets.rend(); ++it) {
        if (current_time - it->timestamp <= last_ms) {
            sum_out += it->sum;
            count_out += it->count;
            min_out = std::min(min_out, it->min_value);
            max_out = std::max(max_out, it->max_value);
        } else {
            break; // Exit loop once we're outside the time window
        }
    }
}
