#include "graphics.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

// -----------------------------------------------------------------------------
// BoxWidget
// -----------------------------------------------------------------------------

BoxWidget::BoxWidget(int pos_x, int pos_y, uint width, uint height, CairoColor color)
    : Widget(pos_x, pos_y), width_(width), height_(height), color_(color) {
    setSize(width_, height_);
}

void BoxWidget::measure(cairo_t *) {
    setSize(width_, height_);
}

void BoxWidget::draw(cairo_t *cr) {
    auto [x, y] = xy(cr);
    cairo_set_source_rgba(cr, color_.r, color_.g, color_.b, color_.a);
    cairo_rectangle(cr, x, y, width_, height_);
    cairo_fill(cr);
}

// -----------------------------------------------------------------------------
// BarChartWidget
// -----------------------------------------------------------------------------

BarChartWidget::BarChartWidget(int pos_x, int pos_y, uint width, uint height, uint window_s, uint num_buckets,
                               StatsField stats_field)
    : Widget(pos_x, pos_y, 1), width_(width), height_(height), window_ms_(window_s * 1000), num_buckets_(num_buckets),
      stats_field_(stats_field), stats_(window_s * 1000, window_s * 1000 / num_buckets), max_label_(0, 0, ""),
      min_label_(0, 0, "") {
    setSize(width_, height_);
}

void BarChartWidget::measure(cairo_t *) {
    setSize(width_, height_);
}

void BarChartWidget::draw(cairo_t *cr) {
    auto [x, y] = xy(cr);
    // box
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.4);
    cairo_rectangle(cr, x, y, width_, height_);
    cairo_fill(cr);

    std::vector<Stats> all_stats = stats_.get_bucket_stats();
    if (all_stats.size() < 3) {
        SPDLOG_DEBUG("Can't draw bar chart - too few values");
        return;
    }
    all_stats.pop_back(); // drop last bucket, because it is usually still not full
    std::vector<double> stats = select_stats(all_stats);
    double min = *std::min_element(stats.begin(), stats.end());
    double max = *std::max_element(stats.begin(), stats.end());

    // legend
    max_label_.setText(shorten(max));
    min_label_.setText(shorten(min));
    max_label_.drawAt(cr, x + 2, y + 15);
    min_label_.drawAt(cr, x + 2, y + height_);

    // bars
    cairo_set_source_rgba(cr, 200.0, 200.0, 200.0, 0.8);

    double scale = max - min;
    SPDLOG_TRACE("Scale: {}, min {}, max {}", scale, min, max);
    uint legend_w = 65;
    uint chart_w = width_ - legend_w;

    uint bar_pad = 4;
    uint bar_w = (chart_w - (bar_pad * num_buckets_)) / num_buckets_;
    uint bar_x = x + legend_w;
    SPDLOG_TRACE("chart_w {} bar_w {}, bar_x {}", chart_w, bar_w, bar_x);

    for (auto val : stats) {
        double normalized = val - min;
        double bar_h = -1.0 * (normalized * (height_ - 10)) / scale;
        // h -> max-min
        // ? -> normalized
        SPDLOG_TRACE("val {}, cairo_rectangle(cr, {}, {}, {}, {})", val, bar_x, y + height_, bar_w, bar_h);
        cairo_rectangle(cr, bar_x, y + height_, bar_w, bar_h - 2);
        cairo_fill(cr);
        bar_x += bar_pad + bar_w;
    }
}

void BarChartWidget::setFact(uint idx, Fact fact) {
    assert(idx == 0);
    switch (fact.getType()) {
        case Fact::T_INT:
            stats_.add(fact.getIntValue());
            break;
        case Fact::T_UINT:
            stats_.add(static_cast<long>(fact.getUintValue()));
            break;
    }
}

std::string BarChartWidget::shorten(long num) {
    double value = num;
    std::string suffix;

    if (num >= 1'000'000'000) {
        value = num / 1'000'000'000.0;
        suffix = "G";
    } else if (num >= 1'000'000) {
        value = num / 1'000'000.0;
        suffix = "M";
    } else if (num >= 1'000) {
        value = num / 1'000.0;
        suffix = "K";
    } else {
        suffix = ""; // No suffix needed
    }

    // Format to 3 significant digits
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3 - static_cast<int>(std::log10(value) + 1)) << value;
    return oss.str() + " " + suffix;
}

std::vector<double> BarChartWidget::select_stats(std::vector<Stats> stats) {
    std::vector<double> res;
    res.reserve(stats.size());
    for (auto stat : stats) {
        switch (stats_field_) {
            case STATS_MIN:
                res.push_back(static_cast<double>(stat.min));
                break;
            case STATS_MAX:
                res.push_back(static_cast<double>(stat.max));
                break;
            case STATS_SUM:
                res.push_back(static_cast<double>(stat.sum));
                break;
            case STATS_COUNT:
                res.push_back(static_cast<double>(stat.count));
                break;
            case STATS_AVG:
                res.push_back(stat.average);
                break;
        }
    }
    return res;
}

// -----------------------------------------------------------------------------
// ExternalSurfaceWidget
// -----------------------------------------------------------------------------

ExternalSurfaceWidget::ExternalSurfaceWidget(int pos_x, int pos_y, std::string shm_name, uint refresh_frequency_ms)
    : Widget(pos_x, pos_y), shm_name(shm_name), refresh_frequency_ms_(refresh_frequency_ms) {};

ExternalSurfaceWidget::~ExternalSurfaceWidget() {
    SPDLOG_INFO("Destroying shm region {}", shm_name);

    for (int i = 0; i < SHM_BUFFERS_COUNT; ++i) {
        if (shm_surfaces[i]) {
            cairo_surface_destroy(shm_surfaces[i]);
            shm_surfaces[i] = nullptr;
        }
    }
    if (shm_data) {
        munmap(shm_data, shm_size);
    }
    shm_unlink(shm_name.c_str());
}

void ExternalSurfaceWidget::init_shm(cairo_t *cr) {
    SPDLOG_INFO("Creating shm region {}", shm_name);

    cairo_surface_t *target = cairo_get_target(cr);
    int width = cairo_image_surface_get_width(target);
    int height = cairo_image_surface_get_height(target);

    const uint32_t stride = static_cast<uint32_t>(width * 4); // ARGB32
    const size_t buf_size = static_cast<size_t>(stride) * height;

    // Calculate total shared memory size
    shm_size = sizeof(SharedMemoryRegion) + (buf_size * SHM_BUFFERS_COUNT); // Metadata + 3 buffers for Image data

    // Create shared memory region
    int shm_fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Failed to create shared memory");
        return;
    }

    if (ftruncate(shm_fd, shm_size) == -1) {
        perror("Failed to set shared memory size");
        shm_unlink(shm_name.c_str());
        close(shm_fd);
        return;
    }

    // Map shared memory to process address space
    shm_region = static_cast<SharedMemoryRegion *>(mmap(0, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
    if (shm_region == MAP_FAILED) {
        shm_region = nullptr;
        perror("Failed to map shared memory");
        shm_unlink(shm_name.c_str());
        close(shm_fd);
        return;
    }

    close(shm_fd);

    // Write metadata
    shm_region->width = width;
    shm_region->height = height;
    shm_region->stride = stride;
    shm_region->refresh_rate = refresh_frequency_ms_;
    shm_region->ready_index.store(-1);
    shm_region->front_index.store(0);
    shm_region->back_index.store(1);

    unsigned char *base = shm_region->data;

    for (int i = 0; i < SHM_BUFFERS_COUNT; ++i) {
        unsigned char *buf_ptr = base + (i * buf_size);

        // Create Cairo surface for the image data
        cairo_surface_t *surf =
            cairo_image_surface_create_for_data(buf_ptr, CAIRO_FORMAT_ARGB32, width, height, stride);

        if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
            spdlog::error("Failed to create cairo surface for buffer {}", i);
            cairo_surface_destroy(surf);
            shm_surfaces[i] = nullptr;
        } else {
            shm_surfaces[i] = surf;
        }
    }
    // Store pointer for cleanup
    shm_data = reinterpret_cast<unsigned char *>(shm_region);
}

void ExternalSurfaceWidget::measure(cairo_t *cr) {
    cairo_surface_t *target = cairo_get_target(cr);
    const int width = cairo_image_surface_get_width(target);
    const int height = cairo_image_surface_get_height(target);
    setSize(width, height);
}

void ExternalSurfaceWidget::draw(cairo_t *cr) {
    if (!shm_region) {
        init_shm(cr);
    }
    if (!shm_region) {
        return;
    }

    int ready = shm_region->ready_index.exchange(-1);
    if (ready >= 0 && ready < SHM_BUFFERS_COUNT) {
        last_surface_index = ready;
        shm_region->front_index.store(ready);
    }
    if (last_surface_index != -1) {
        cairo_surface_mark_dirty(shm_surfaces[last_surface_index]);
        auto [x, y] = xy(cr);
        cairo_set_source_surface(cr, shm_surfaces[last_surface_index], x, y); // Position at (0, 0)
        cairo_paint(cr);                                                      // Paint shm_surface onto base_surface
    }
}
