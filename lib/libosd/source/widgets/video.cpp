#include "video.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <utility>

#include <spdlog/spdlog.h>

namespace {

constexpr uint MAX_WIDGET_REFRESH_MS = 2000;

std::chrono::milliseconds resolveRefreshRate(const char *widget_name, uint refresh_rate, uint refresh_frequency_ms) {
    if (refresh_rate < refresh_frequency_ms || refresh_rate > MAX_WIDGET_REFRESH_MS) {
        spdlog::warn("{}: Refresh rate '{}' is out of range [{}, {}]",
                     widget_name,
                     refresh_rate,
                     refresh_frequency_ms,
                     MAX_WIDGET_REFRESH_MS);
        spdlog::warn("{}: Using osd refresh rate: {}", widget_name, refresh_frequency_ms);
        return std::chrono::milliseconds(refresh_frequency_ms);
    }
    return std::chrono::milliseconds(refresh_rate);
}

} // namespace

// -----------------------------------------------------------------------------
// VideoWidget
// -----------------------------------------------------------------------------

VideoWidget::VideoWidget(int pos_x, int pos_y, uint window_size_ms, cairo_surface_t *icon, std::string tpl,
                         uint refresh_rate, uint num_args, uint refresh_frequency_ms)
    : Widget(pos_x, pos_y, num_args), icon_(0, 0, icon), text_(0, 0, std::move(tpl), num_args), fps_(window_size_ms),
      window_size_ms_(window_size_ms),
      refresh_rate_ms_(resolveRefreshRate("VideoWidget", refresh_rate, refresh_frequency_ms)) {}

void VideoWidget::measure(cairo_t *cr) {
    measureChild(icon_, cr);
    measureChild(text_, cr);
    setSize(icon_.width() + SPACING + text_.width(), std::max(icon_.height(), text_.height()));
}

void VideoWidget::draw(cairo_t *cr) {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - last_refresh_;

    if (has_value_ && elapsed >= refresh_rate_ms_) {
        last_refresh_ = now;
        const double fps = fps_.ratePerSecondOverLastMs(window_size_ms_);
        text_.setFact(0, Fact(FactMeta("video_fps"), static_cast<ulong>(fps)));
        invalidateMeasure();
    }
    const auto [x, y] = xy(cr);
    icon_.drawAt(cr, x, y - 20);
    text_.drawAt(cr, x + icon_.width() + SPACING, y);
}

void VideoWidget::setFact(uint idx, Fact fact) {
    if (idx >= factCount()) {
        assert(false && "VideoWidget fact index out of range");
        return;
    }
    if (idx != 0) {
        text_.setFact(idx, std::move(fact));
        invalidateMeasure();
        return;
    }
    if (!fact.isDefined()) {
        has_value_ = false;
        fps_.clear();
        last_refresh_ = {};
        text_.setFact(idx, Fact());
        invalidateMeasure();
        return;
    }
    has_value_ = true;

    const ulong num_frames = fact.getUintValue(); // should be always '1'
    fps_.add(num_frames, fact.getTimestamp());
    invalidateMeasure();
}

// -----------------------------------------------------------------------------
// VideoBitrateWidget
// -----------------------------------------------------------------------------

VideoBitrateWidget::VideoBitrateWidget(int pos_x, int pos_y, uint window_size_ms, cairo_surface_t *icon,
                                       std::string tpl, uint refresh_rate, uint num_args, uint refresh_frequency_ms)
    : Widget(pos_x, pos_y, num_args), icon_(0, 0, icon), text_(0, 0, std::move(tpl), num_args), bps_(window_size_ms),
      window_size_ms_(window_size_ms),
      refresh_rate_ms_(resolveRefreshRate("VideoBitrateWidget", refresh_rate, refresh_frequency_ms)) {
    assert(num_args == 1);
}

void VideoBitrateWidget::measure(cairo_t *cr) {
    measureChild(icon_, cr);
    measureChild(text_, cr);
    setSize(icon_.width() + SPACING + text_.width(), std::max(icon_.height(), text_.height()));
}

void VideoBitrateWidget::draw(cairo_t *cr) {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - last_refresh_;

    if (has_value_ && elapsed >= refresh_rate_ms_) {
        last_refresh_ = now;
        const double bytes_per_second = bps_.ratePerSecondOverLastMs(window_size_ms_);

        // 125000 is 1_000_000 / 8 (megabits, not megabytes)
        const double mbps = bytes_per_second / 125000.0;
        text_.setFact(0, Fact(FactMeta("video_mbps"), mbps));
        invalidateMeasure();
    }
    const auto [x, y] = xy(cr);
    icon_.drawAt(cr, x, y - 20);
    text_.drawAt(cr, x + icon_.width() + SPACING, y);
}

void VideoBitrateWidget::setFact(uint idx, Fact fact) {
    if (idx != 0) {
        assert(false && "VideoBitrateWidget fact index out of range");
        return;
    }

    if (!fact.isDefined()) {
        has_value_ = false;
        bps_.clear();
        last_refresh_ = {};
        text_.setFact(0, Fact());
        invalidateMeasure();
        return;
    }
    has_value_ = true;
    const ulong num_bytes = fact.getUintValue();
    bps_.add(num_bytes, fact.getTimestamp());
    invalidateMeasure();
}

// -----------------------------------------------------------------------------
// VideoDecodeLatencyWidget
// -----------------------------------------------------------------------------

VideoDecodeLatencyWidget::VideoDecodeLatencyWidget(int pos_x, int pos_y, uint window_size_ms, cairo_surface_t *icon,
                                                   std::string tpl, uint refresh_rate, uint num_args,
                                                   uint refresh_frequency_ms)
    : Widget(pos_x, pos_y, num_args), icon_(0, 0, icon), text_(0, 0, std::move(tpl), 3), timing_(window_size_ms),
      window_size_ms_(window_size_ms),
      refresh_rate_ms_(resolveRefreshRate("VideoDecodeLatencyWidget", refresh_rate, refresh_frequency_ms)) {
    assert(num_args == 1);
}

void VideoDecodeLatencyWidget::measure(cairo_t *cr) {
    measureChild(icon_, cr);
    measureChild(text_, cr);
    setSize(icon_.width() + SPACING + text_.width(), std::max(icon_.height(), text_.height()));
}

void VideoDecodeLatencyWidget::draw(cairo_t *cr) {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - last_refresh_;

    if (has_value_ && elapsed >= refresh_rate_ms_) {
        last_refresh_ = now;

        const Stats stats = timing_.statsOverLastMs(window_size_ms_);
        if (stats.count > 0) {
            text_.setFact(0, Fact(FactMeta("video_avg"), stats.average));
            text_.setFact(1, Fact(FactMeta("video_min"), stats.min));
            text_.setFact(2, Fact(FactMeta("video_max"), stats.max));
        } else {
            text_.setFact(0, Fact());
            text_.setFact(1, Fact());
            text_.setFact(2, Fact());
        }
        invalidateMeasure();
    }
    const auto [x, y] = xy(cr);
    icon_.drawAt(cr, x, y - 20);
    text_.drawAt(cr, x + icon_.width() + SPACING, y);
}

void VideoDecodeLatencyWidget::setFact(uint idx, Fact fact) {
    if (idx != 0) {
        assert(false && "VideoDecodeLatencyWidget fact index out of range");
        return;
    }

    if (!fact.isDefined()) {
        has_value_ = false;
        timing_.clear();
        last_refresh_ = {};
        text_.setFact(0, Fact());
        text_.setFact(1, Fact());
        text_.setFact(2, Fact());
        invalidateMeasure();
        return;
    }
    has_value_ = true;

    const ulong decode_time = fact.getUintValue();
    timing_.add(decode_time, fact.getTimestamp());
    invalidateMeasure();
}