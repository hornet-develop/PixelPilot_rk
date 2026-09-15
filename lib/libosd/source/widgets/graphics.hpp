#ifndef OSD_WIDGETS_GRAPHICS_HPP
#define OSD_WIDGETS_GRAPHICS_HPP

#include "base.hpp"
#include "helpers/running_average.hpp"
#include "text.hpp"

#include <cstddef>
#include <cstdint>
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
    std::vector<double> selectStats(const std::vector<Stats> &stats) const;

    uint width_;
    uint height_;

    StatsField stats_field_;
    RunningAverage stats_;

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