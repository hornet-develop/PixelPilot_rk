#include "graphics.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <iterator>
#include <sstream>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

namespace {

std::string shorten(double number) {
    double value = number;
    const char *suffix = "";
    const double magnitude = std::abs(number);

    if (magnitude >= 1'000'000'000.0) {
        value /= 1'000'000'000.0;
        suffix = "G";
    } else if (magnitude >= 1'000'000.0) {
        value /= 1'000'000.0;
        suffix = "M";
    } else if (magnitude >= 1'000.0) {
        value /= 1'000.0;
        suffix = "K";
    }

    std::ostringstream oss;
    oss << std::setprecision(3) << value;
    if (*suffix != '\0') {
        oss << ' ' << suffix;
    }
    return oss.str();
}

} // namespace

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
    : Widget(pos_x, pos_y, 1), width_(width), height_(height), stats_field_(stats_field), window_(window_s * 1000),
      max_label_(0, 0, ""), min_label_(0, 0, "") {
    assert(window_s > 0);
    assert(num_buckets > 0);

    const uint bucket_size_ms = window_s * 1000 / num_buckets;
    assert(bucket_size_ms > 0);

    bucket_size_ = std::chrono::milliseconds(bucket_size_ms);

    setSize(width_, height_);
}

void BarChartWidget::measure(cairo_t *) {
    setSize(width_, height_);
}

void BarChartWidget::draw(cairo_t *cr) {
    constexpr int LEGEND_WIDTH = 65;
    constexpr int BAR_PADDING = 4;
    constexpr int CHART_PADDING = 10;

    removeExpiredBuckets(Clock::now());

    auto [x, y] = xy(cr);
    // box
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.4);
    cairo_rectangle(cr, x, y, width_, height_);
    cairo_fill(cr);

    if (buckets_.size() < 3) {
        SPDLOG_DEBUG("Can't draw bar chart - too few values");
        return;
    }
    const auto stats = selectStats();
    if (stats.empty()) {
        return;
    }

    const auto [min_it, max_it] = std::minmax_element(stats.begin(), stats.end());
    const double min = *min_it;
    const double max = *max_it;

    // legend
    max_label_.setText(shorten(max));
    min_label_.setText(shorten(min));
    max_label_.drawAt(cr, x + 2, y + 15);
    min_label_.drawAt(cr, x + 2, y + height_);

    const int chart_width = static_cast<int>(width_) - LEGEND_WIDTH;
    const int bar_count = static_cast<int>(stats.size());
    if (chart_width <= 0 || bar_count == 0) {
        return;
    }
    const int total_padding = BAR_PADDING * (bar_count - 1);
    if (chart_width <= total_padding) {
        return;
    }
    const int bar_width = (chart_width - total_padding) / bar_count;
    if (bar_width <= 0) {
        return;
    }
    const double scale = max - min;
    const double chart_height = std::max(0, static_cast<int>(height_) - CHART_PADDING);

    SPDLOG_TRACE("Scale: {}, min {}, max {}", scale, min, max);

    int bar_x = x + LEGEND_WIDTH;
    // bars
    cairo_set_source_rgba(cr, 200.0 / 255.0, 200.0 / 255.0, 200.0 / 255.0, 0.8);

    for (double value : stats) {
        double bar_height = 0.0;
        if (scale > 0.0) {
            const double normalized = value - min;
            bar_height = normalized * chart_height / scale;
        }
        SPDLOG_TRACE("val {}, cairo_rectangle(cr, {}, {}, {}, {})", value, bar_x, y + height_, bar_width, -bar_height);
        cairo_rectangle(cr, bar_x, y + height_, bar_width, -bar_height);
        cairo_fill(cr);
        bar_x += bar_width + BAR_PADDING;
    }
}

void BarChartWidget::setFact(uint idx, Fact fact) {
    if (idx != 0) {
        spdlog::error("BarChartWidget: invalid fact index {}", idx);
        assert(false && "BarChartWidget fact index out of range");
        return;
    }
    switch (fact.getType()) {
        case Fact::T_INT:
            addValue(fact.getIntValue(), fact.getTimestamp());
            break;
        case Fact::T_UINT:
            addValue(static_cast<long>(fact.getUintValue()), fact.getTimestamp());
            break;
        default:
            break;
    }
}

void BarChartWidget::addValue(long value, Clock::time_point timestamp) {
    removeExpiredBuckets(timestamp);
    if (buckets_.empty() || timestamp - buckets_.back().timestamp >= bucket_size_) {
        buckets_.emplace_back(timestamp, value);
        return;
    }

    auto &bucket = buckets_.back();
    bucket.sum += value;
    ++bucket.count;
    bucket.min = std::min(bucket.min, value);
    bucket.max = std::max(bucket.max, value);
}

void BarChartWidget::removeExpiredBuckets(Clock::time_point now) {
    while (!buckets_.empty() && now - buckets_.front().timestamp > window_) {
        buckets_.pop_front();
    }
}

double BarChartWidget::selectStat(const Bucket &bucket) const {
    switch (stats_field_) {
        case STATS_MIN:
            return static_cast<double>(bucket.min);
        case STATS_MAX:
            return static_cast<double>(bucket.max);
        case STATS_SUM:
            return static_cast<double>(bucket.sum);
        case STATS_COUNT:
            return static_cast<double>(bucket.count);
        case STATS_AVG:
            return static_cast<double>(bucket.sum) / bucket.count;
        default:
            spdlog::warn("BarChartWidget: invalid stats field");
            assert(false && "Invalid BarChartWidget stats field");
            return 0.0;
    }
}

std::vector<double> BarChartWidget::selectStats() const {
    std::vector<double> result;
    if (buckets_.size() < 2) {
        return result;
    }

    result.reserve(buckets_.size() - 1);
    for (auto it = buckets_.begin(); std::next(it) != buckets_.end(); ++it) {
        result.push_back(selectStat(*it));
    }
    return result;
}

// -----------------------------------------------------------------------------
// ExternalSurfaceWidget
// -----------------------------------------------------------------------------

ExternalSurfaceWidget::~ExternalSurfaceWidget() {
    cleanupShm();
}

bool ExternalSurfaceWidget::initShm(cairo_t *cr) {
    SPDLOG_INFO("Creating shm region {}", shm_name_);

    cairo_surface_t *target = cairo_get_target(cr);
    const int width = cairo_image_surface_get_width(target);
    const int height = cairo_image_surface_get_height(target);

    if (width <= 0 || height <= 0) {
        spdlog::error("Invalid shared surface size {}x{}", width, height);
        return false;
    }

    const int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    if (stride < 0) {
        spdlog::error("Failed to calculate Cairo stride for width {}", width);
        return false;
    }

    // Calculate total shared memory size
    const size_t buffer_size = static_cast<size_t>(stride) * height;
    const size_t shm_size = sizeof(SharedMemoryRegion) + buffer_size * SHM_BUFFERS_COUNT;

    // Create shared memory region
    const int shm_fd = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Failed to create shared memory");
        return false;
    }

    if (ftruncate(shm_fd, shm_size) == -1) {
        perror("Failed to set shared memory size");
        shm_unlink(shm_name_.c_str());
        close(shm_fd);
        return false;
    }

    // Map shared memory to process address space
    void *mapping = mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);

    if (mapping == MAP_FAILED) {
        perror("Failed to map shared memory");
        shm_unlink(shm_name_.c_str());
        return false;
    }

    shm_region_ = static_cast<SharedMemoryRegion *>(mapping);
    shm_size_ = shm_size;

    // Write metadata
    shm_region_->width = width;
    shm_region_->height = height;
    shm_region_->stride = static_cast<uint32_t>(stride);
    shm_region_->refresh_rate = refresh_frequency_ms_;
    shm_region_->ready_index.store(-1);
    shm_region_->front_index.store(0);
    shm_region_->back_index.store(1);

    unsigned char *base = shm_region_->data;

    for (int i = 0; i < SHM_BUFFERS_COUNT; ++i) {
        unsigned char *buffer = base + static_cast<size_t>(i) * buffer_size;

        // Create Cairo surface for the image data
        cairo_surface_t *surface =
            cairo_image_surface_create_for_data(buffer, CAIRO_FORMAT_ARGB32, width, height, stride);

        const cairo_status_t status = cairo_surface_status(surface);
        if (status != CAIRO_STATUS_SUCCESS) {
            spdlog::error("Failed to create Cairo surface for buffer {}: {}", i, cairo_status_to_string(status));
            cairo_surface_destroy(surface);
            cleanupShm();
            return false;
        }
        shm_surfaces_[i] = surface;
    }
    return true;
}

void ExternalSurfaceWidget::cleanupShm() {
    SPDLOG_INFO("Cleaning up shm region {}", shm_name_);

    for (cairo_surface_t *&surface : shm_surfaces_) {
        if (surface) {
            cairo_surface_destroy(surface);
            surface = nullptr;
        }
    }
    if (shm_region_) {
        munmap(shm_region_, shm_size_);
        shm_region_ = nullptr;
        shm_unlink(shm_name_.c_str());
    }
    shm_size_ = 0;
    last_surface_index_ = -1;
}

void ExternalSurfaceWidget::measure(cairo_t *cr) {
    cairo_surface_t *target = cairo_get_target(cr);
    const int width = cairo_image_surface_get_width(target);
    const int height = cairo_image_surface_get_height(target);
    setSize(width, height);
}

void ExternalSurfaceWidget::draw(cairo_t *cr) {
    if (!shm_region_ && !initShm(cr)) {
        return;
    }

    const int32_t ready = shm_region_->ready_index.exchange(-1);
    if (ready >= 0 && ready < SHM_BUFFERS_COUNT) {
        last_surface_index_ = ready;
        shm_region_->front_index.store(ready);
    }
    if (last_surface_index_ < 0) {
        return;
    }
    cairo_surface_t *surface = shm_surfaces_[last_surface_index_];
    if (!surface) {
        return;
    }
    cairo_surface_mark_dirty(surface);
    auto [x, y] = xy(cr);
    cairo_set_source_surface(cr, surface, x, y);
    cairo_paint(cr);
}