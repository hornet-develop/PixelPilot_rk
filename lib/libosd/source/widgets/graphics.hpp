#ifndef OSD_WIDGETS_GRAPHICS_HPP
#define OSD_WIDGETS_GRAPHICS_HPP

#include "base.hpp"
#include "helpers/running_average.hpp"
#include "text.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
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
    std::string shorten(long num);
    std::vector<double> select_stats(std::vector<Stats> stats);

    uint width_;
    uint height_;
    uint window_ms_;
    uint num_buckets_;

    StatsField stats_field_ = STATS_SUM;
    RunningAverage stats_;

    TextWidget max_label_;
    TextWidget min_label_;
};

class ExternalSurfaceWidget : public Widget {
  public:
    ExternalSurfaceWidget(int pos_x, int pos_y, std::string shm_name, uint refresh_frequency_ms);
    ~ExternalSurfaceWidget() override;

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;

  private:
    void init_shm(cairo_t *cr);

    SharedMemoryRegion *shm_region = nullptr;
    int32_t last_surface_index = -1;
    cairo_surface_t *shm_surfaces[SHM_BUFFERS_COUNT] = {};
    size_t shm_size = 0;
    unsigned char *shm_data = nullptr;
    std::string shm_name;
    uint refresh_frequency_ms_;
};

#endif