#include "video.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <utility>

#include <sys/types.h>

#include <spdlog/spdlog.h>

namespace {

constexpr uint MAX_WIDGET_REFRESH_MS = 2000;

}

// -----------------------------------------------------------------------------
// VideoWidget
// -----------------------------------------------------------------------------

VideoWidget::VideoWidget(int pos_x, int pos_y, uint window_size_ms, uint bucket_size_ms, cairo_surface_t *icon,
                         std::string tpl, uint refresh_rate, uint num_args, uint refresh_frequency_ms)
    : Widget(pos_x, pos_y, num_args), icon_(0, 0, icon), text_(0, 0, std::move(tpl), num_args),
      fps_(window_size_ms, bucket_size_ms) {
    if (refresh_rate < refresh_frequency_ms || refresh_rate > MAX_WIDGET_REFRESH_MS) {
        spdlog::warn("VideoWidget: Refresh rate '{}' is out of range [{} {}].", refresh_rate, refresh_frequency_ms,
                     MAX_WIDGET_REFRESH_MS);
        spdlog::warn("VideoWidget: Using osd refresh rate: {}", refresh_frequency_ms);
        refresh_rate_ms_ = std::chrono::milliseconds(refresh_frequency_ms);
    } else {
        refresh_rate_ms_ = std::chrono::milliseconds(refresh_rate);
    }
}

void VideoWidget::measure(cairo_t *cr) {
    measureChild(icon_, cr);
    measureChild(text_, cr);
    setSize(icon_.width() + SPACING + text_.width(), std::max(icon_.height(), text_.height()));
}

void VideoWidget::draw(cairo_t *cr) {
    auto [x, y] = xy(cr);
    icon_.drawAt(cr, x, y - 20);
    text_.drawAt(cr, x + icon_.width() + SPACING, y);
}

void VideoWidget::setFact(uint idx, Fact fact) {
    if (idx != 0) {
        text_.setFact(idx, std::move(fact));
        invalidateMeasure();
        return;
    }
    if (!fact.isDefined()) {
        text_.setFact(idx, Fact());
        invalidateMeasure();
        return;
    }
    // replace the value with its increment rate per-second
    ulong num_frames = fact.getUintValue(); // should be always '1'
    fps_.add(num_frames);

    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - last_drawn_;

    if (elapsed < refresh_rate_ms_) {
        return;
    }
    last_drawn_ = now;
    text_.setFact(idx, Fact(FactMeta("video_fps"), (ulong)fps_.rate_per_second_over_last_ms(1000)));
    invalidateMeasure();
}

// -----------------------------------------------------------------------------
// VideoBitrateWidget
// -----------------------------------------------------------------------------

VideoBitrateWidget::VideoBitrateWidget(int pos_x, int pos_y, uint window_size_ms, uint bucket_size_ms,
                                       cairo_surface_t *icon, std::string tpl, uint refresh_rate, uint num_args,
                                       uint refresh_frequency_ms)
    : Widget(pos_x, pos_y, num_args), icon_(0, 0, icon), text_(0, 0, std::move(tpl), num_args),
      bps_(window_size_ms, bucket_size_ms) {
    assert(num_args == 1);
    if (refresh_rate < refresh_frequency_ms || refresh_rate > MAX_WIDGET_REFRESH_MS) {
        spdlog::warn("VideoBitrateWidget: Refresh rate '{}' is out of range [{} {}].", refresh_rate,
                     refresh_frequency_ms, MAX_WIDGET_REFRESH_MS);
        spdlog::warn("VideoBitrateWidget: Using osd refresh rate: {}", refresh_frequency_ms);
        refresh_rate_ms_ = std::chrono::milliseconds(refresh_frequency_ms);
    } else {
        refresh_rate_ms_ = std::chrono::milliseconds(refresh_rate);
    }
}

void VideoBitrateWidget::measure(cairo_t *cr) {
    measureChild(icon_, cr);
    measureChild(text_, cr);
    setSize(icon_.width() + SPACING + text_.width(), std::max(icon_.height(), text_.height()));
}

void VideoBitrateWidget::draw(cairo_t *cr) {
    auto [x, y] = xy(cr);
    icon_.drawAt(cr, x, y - 20);
    text_.drawAt(cr, x + icon_.width() + SPACING, y);
}

void VideoBitrateWidget::setFact(uint idx, Fact fact) {
    assert(idx == 0);
    if (!fact.isDefined()) {
        text_.setFact(idx, Fact());
        invalidateMeasure();
        return;
    }
    // replace the value with its increment rate per-second
    ulong num_bytes = fact.getUintValue();
    bps_.add(num_bytes);

    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - last_drawn_;

    if (elapsed < refresh_rate_ms_) {
        return;
    }
    last_drawn_ = now;

    // 125000 is 1_000_000 / 8 (megabits, not megabytes)
    text_.setFact(idx, Fact(FactMeta("video_mbps"), bps_.rate_per_second_over_last_ms(1000) / 125000.0));
    invalidateMeasure();
}

// -----------------------------------------------------------------------------
// VideoDecodeLatencyWidget
// -----------------------------------------------------------------------------

VideoDecodeLatencyWidget::VideoDecodeLatencyWidget(int pos_x, int pos_y, uint window_size_ms, uint bucket_size_ms,
                                                   cairo_surface_t *icon, std::string tpl, uint refresh_rate,
                                                   uint num_args, uint refresh_frequency_ms)
    : Widget(pos_x, pos_y, num_args), icon_(0, 0, icon), text_(0, 0, std::move(tpl), 3),
      timing_(window_size_ms, bucket_size_ms) {
    assert(num_args == 1);
    if (refresh_rate < refresh_frequency_ms || refresh_rate > MAX_WIDGET_REFRESH_MS) {
        spdlog::warn("VideoDecodeLatencyWidget: Refresh rate '{}' is out of range [{} {}].", refresh_rate,
                     refresh_frequency_ms, MAX_WIDGET_REFRESH_MS);
        spdlog::warn("VideoDecodeLatencyWidget: Using osd refresh rate: {}", refresh_frequency_ms);
        refresh_rate_ms_ = std::chrono::milliseconds(refresh_frequency_ms);
    } else {
        refresh_rate_ms_ = std::chrono::milliseconds(refresh_rate);
    }
}

void VideoDecodeLatencyWidget::measure(cairo_t *cr) {
    measureChild(icon_, cr);
    measureChild(text_, cr);
    setSize(icon_.width() + SPACING + text_.width(), std::max(icon_.height(), text_.height()));
}

void VideoDecodeLatencyWidget::draw(cairo_t *cr) {
    auto [x, y] = xy(cr);
    icon_.drawAt(cr, x, y - 20);
    text_.drawAt(cr, x + icon_.width() + SPACING, y);
}

void VideoDecodeLatencyWidget::setFact(uint idx, Fact fact) {
    assert(idx == 0);
    if (!fact.isDefined()) {
        text_.setFact(0, Fact());
        text_.setFact(1, Fact());
        text_.setFact(2, Fact());
        invalidateMeasure();
        return;
    }
    const ulong decode_time = fact.getUintValue();
    timing_.add(decode_time);

    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - last_drawn_;

    if (elapsed < refresh_rate_ms_) {
        return;
    }
    last_drawn_ = now;

    Stats stats = timing_.get_stats_over_last_ms_result(1000);
    text_.setFact(0, Fact(FactMeta("video_avg"), stats.average));
    text_.setFact(1, Fact(FactMeta("video_min"), stats.min));
    text_.setFact(2, Fact(FactMeta("video_max"), stats.max));
    invalidateMeasure();
}
