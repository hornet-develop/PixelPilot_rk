#ifndef OSD_WIDGETS_GRAPHICS_HPP
#define OSD_WIDGETS_GRAPHICS_HPP

#include "base.hpp"
#include "text.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include <shared_surface.hpp>

class BoxWidget : public Widget {
  public:
    BoxWidget(int pos_x, int pos_y, uint width, uint height, CairoColor color);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;

  private:
    uint width_;
    uint height_;
    CairoColor color_;
};

class BarChartWidget : public Widget {
  public:
    enum StatsField { STATS_MIN, STATS_MAX, STATS_SUM, STATS_COUNT, STATS_AVG };

    BarChartWidget(int pos_x, int pos_y, uint width, uint height, uint window_s, uint num_buckets,
                   StatsField stats_field);

    void measure(cairo_t *) override;
    void draw(cairo_t *cr) override;
    void setFact(uint idx, Fact fact) override;

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

    void addValue(long value, Clock::time_point timestamp);
    void removeExpiredBuckets(Clock::time_point now);

    double selectStat(const Bucket &bucket) const;
    std::vector<double> selectStats() const;

    uint width_;
    uint height_;

    StatsField stats_field_;

    std::chrono::milliseconds window_;
    std::chrono::milliseconds bucket_size_;
    std::deque<Bucket> buckets_;

    TextWidget max_label_;
    TextWidget min_label_;
};

class ExternalSurfaceWidget : public Widget {
  public:
    ExternalSurfaceWidget(int pos_x, int pos_y, std::string shm_name, uint refresh_frequency_ms)
        : Widget(pos_x, pos_y), shm_name_(std::move(shm_name)), refresh_frequency_ms_(refresh_frequency_ms) {}

    ~ExternalSurfaceWidget() override;

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;

  private:
    bool initShm(cairo_t *cr);
    void cleanupShm();

    SharedMemoryRegion *shm_region_ = nullptr;
    int32_t last_surface_index_ = -1;
    cairo_surface_t *shm_surfaces_[SHM_BUFFERS_COUNT] = {};
    size_t shm_size_ = 0;

    std::string shm_name_;
    const uint refresh_frequency_ms_;
};

#endif