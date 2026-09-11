#include "components.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

namespace {

constexpr uint MAX_WIDGET_REFRESH_MS = 2000;

}

// -----------------------------------------------------------------------------
// IconTextWidget
// -----------------------------------------------------------------------------

IconTextWidget::IconTextWidget(int pos_x, int pos_y, cairo_surface_t *icon, std::string text, uint num_args)
    : Widget(pos_x, pos_y, num_args), icon_(0, 0, icon), text_(0, 0, std::move(text)) {}

void IconTextWidget::measure(cairo_t *cr) {
    measureChild(icon_, cr);
    measureChild(text_, cr);
    setSize(icon_.width() + SPACING + text_.width(), std::max(icon_.height(), text_.height()));
}

void IconTextWidget::draw(cairo_t *cr) {
    auto [x, y] = xy(cr);
    icon_.drawAt(cr, x, y - 20);
    text_.drawAt(cr, x + icon_.width() + SPACING, y);
}

// -----------------------------------------------------------------------------
// IconTplTextWidget
// -----------------------------------------------------------------------------

IconTplTextWidget::IconTplTextWidget(int pos_x, int pos_y, cairo_surface_t *icon, std::string tpl, uint num_args)
    : TplTextWidget(pos_x, pos_y, std::move(tpl), num_args), icon_(0, 0, icon) {}

void IconTplTextWidget::measure(cairo_t *cr) {
    measureChild(icon_, cr);
    TplTextWidget::measure(cr);
    setSize(icon_.width() + SPACING + TplTextWidget::width(), std::max(icon_.height(), TplTextWidget::height()));
}

void IconTplTextWidget::draw(cairo_t *cr) {
    auto [x, y] = xy(cr);
    icon_.drawAt(cr, x, y - 20);
    drawText(cr, x + icon_.width() + SPACING, y);
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
// PopupWidget
// -----------------------------------------------------------------------------

PopupWidget::PopupWidget(int pos_x, int pos_y, uint timeout_ms, uint num_args)
    : Widget(pos_x, pos_y, num_args), timeout_(timeout_ms) {}

void PopupWidget::measure(cairo_t *cr) {
    auto now = std::chrono::steady_clock::now();
    removeExpired(now);

    double max_width = 0.0;
    double total_height = 0.0;

    for (auto &msg : msgs_) {
        if (!msg.measured) {
            cairo_text_extents_t extents;
            cairo_text_extents(cr, msg.text.c_str(), &extents);
            msg.width = extents.width;
            msg.height = extents.height;
            msg.measured = true;
        }
        max_width = std::max(max_width, msg.width + PADDING * 2);
        total_height += msg.height + PADDING * 2 + ITEM_SPACING;
    }
    setSize(static_cast<int>(std::ceil(max_width)), static_cast<int>(std::ceil(total_height)));
}

void PopupWidget::draw(cairo_t *cr) {
    auto [x, y] = xy(cr);
    auto now = std::chrono::steady_clock::now();
    if (removeExpired(now)) {
        invalidateMeasure();
    }
    double y_offset = y;
    for (const auto &msg : msgs_) {
        auto past = std::chrono::duration_cast<std::chrono::milliseconds>(now - msg.time);
        double fade_fraction = 1.0 - static_cast<double>(past.count()) / static_cast<double>(timeout_.count());

        // Draw popup box
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, fade_fraction / 3.0);
        cairo_rectangle(cr, x - PADDING, y_offset + PADDING, msg.width + (PADDING * 2), -(msg.height + (PADDING * 2)));
        cairo_fill(cr);

        // Draw popup text
        cairo_set_source_rgba(cr, 255.0, 255.0, 255.0, fade_fraction);
        cairo_move_to(cr, x, y_offset);
        cairo_show_text(cr, msg.text.c_str());
        y_offset += msg.height + (PADDING * 2) + ITEM_SPACING;
    }
}

void PopupWidget::setFact(uint, Fact fact) {
    msgs_.push_back({.time = std::chrono::steady_clock::now(), .text = fact.getStrValue()});
    invalidateMeasure();
}

bool PopupWidget::removeExpired(std::chrono::time_point<std::chrono::steady_clock> now) {
    bool removed = false;
    while (!msgs_.empty() && now - msgs_.front().time > timeout_) {
        msgs_.pop_front();
        removed = true;
    }
    return removed;
}

// -----------------------------------------------------------------------------
// DvrStatusWidget
// -----------------------------------------------------------------------------

DvrStatusWidget::DvrStatusWidget(int pos_x, int pos_y, cairo_surface_t *icon, std::string text)
    : Widget(pos_x, pos_y, 1), icon_(0, 0, icon, 0, ICON_STYLE), text_(0, 0, std::move(text), 0, TEXT_STYLE) {}

void DvrStatusWidget::measure(cairo_t *cr) {
    if (!isActive()) {
        setSize(0, 0);
        return;
    }
    measureChild(icon_, cr);
    measureChild(text_, cr);
    setSize(icon_.width() + SPACING + text_.width(), std::max(icon_.height(), text_.height()));
}

void DvrStatusWidget::draw(cairo_t *cr) {
    if (!isActive()) {
        return;
    }
    auto [x, y] = xy(cr);
    icon_.drawAt(cr, x, y - 20);
    text_.drawAt(cr, x + icon_.width() + SPACING, y);
}

bool DvrStatusWidget::isActive() const {
    Fact status = fact(0);
    return status.isDefined() && status.getBoolValue();
}

// -----------------------------------------------------------------------------
// DvrStorageWidget
// -----------------------------------------------------------------------------

DvrStorageWidget::DvrStorageWidget(int pos_x, int pos_y, cairo_surface_t *icon)
    : Widget(pos_x, pos_y, 2), icon_(0, 0, icon), text_(0, 0, "-") {}

void DvrStorageWidget::measure(cairo_t *cr) {
    if (!visible_) {
        setSize(0, 0);
        return;
    }
    measureChild(icon_, cr);
    if (!show_text_) {
        setSize(icon_.width(), icon_.height());
        return;
    }
    measureChild(text_, cr);
    setSize(icon_.width() + SPACING + text_.width(), std::max(icon_.height(), text_.height()));
}

void DvrStorageWidget::draw(cairo_t *cr) {
    if (!visible_) {
        return;
    }
    auto [x, y] = xy(cr);
    icon_.drawAt(cr, x, y - 20);
    if (show_text_) {
        text_.drawAt(cr, x + icon_.width() + SPACING, y);
    }
}

void DvrStorageWidget::setFact(uint idx, Fact fact) {
    storeFact(idx, std::move(fact));
    updateState();
}

void DvrStorageWidget::updateState() {
    Fact status_fact = fact(0);
    if (!status_fact.isDefined()) {
        visible_ = false;
        return;
    }

    const auto status = status_fact.getUintValue();
    switch (status) {
        case 0:
            visible_ = true;
            show_text_ = false;
            icon_.setFillColor({0.4, 0.4, 0.44, 1.0});
            icon_.setOutlineColor({0.0, 0.0, 0.0, 0.4});
            break;
        case 1:
            visible_ = true;
            show_text_ = true;
            icon_.setFillColor({1.0, 1.0, 1.0, 1.0});
            icon_.setOutlineColor({0.0, 0.0, 0.0, 1.0});
            updateStorageText();
            break;
        case 2:
            visible_ = true;
            show_text_ = true;
            icon_.setFillColor({1.0, 0.0, 0.0, 1.0});
            icon_.setOutlineColor({0.0, 0.0, 0.0, 1.0});
            updateStorageText();
            break;
        default:
            visible_ = false;
            break;
    }
}

void DvrStorageWidget::updateStorageText() {
    Fact storage_fact = fact(1);
    if (!storage_fact.isDefined()) {
        text_.setText("-");
        return;
    }
    text_.setText(format_storage_size(storage_fact.getUintValue()));
}

std::string DvrStorageWidget::format_storage_size(uint64_t bytes) {
    struct StorageUnit {
        uint64_t size;
        const char *name;
    };

    static constexpr StorageUnit units[] = {
        {1ULL << 40, "T"},
        {1ULL << 30, "G"},
        {1ULL << 20, "M"},
        {1ULL << 10, "K"},
    };

    const StorageUnit *unit = &units[3];
    for (const auto &u : units) {
        if (bytes >= u.size) {
            unit = &u;
            break;
        }
    }
    const double value = static_cast<double>(bytes) / unit->size;

    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f %s", value, unit->name);

    return buf;
}

// -----------------------------------------------------------------------------
// IconStatusWidget
// -----------------------------------------------------------------------------

IconStatusWidget::IconStatusWidget(int pos_x, int pos_y, cairo_surface_t *icon)
    : IconWidget(pos_x, pos_y, icon, 1,
                 DrawStyle{.fill = {0.4, 0.4, 0.44, 1.0}, .outline = {0.0, 0.0, 0.0, 0.4}, .outline_width = 1.0}) {}

void IconStatusWidget::setFact(uint idx, Fact fact) {
    if (fact.isDefined() && fact.getBoolValue()) {
        setFillColor({1.0, 1.0, 1.0, 1.0});
        setOutlineColor({0.0, 0.0, 0.0, 1.0});
    } else {
        setFillColor({0.4, 0.4, 0.44, 1.0});
        setOutlineColor({0.0, 0.0, 0.0, 0.4});
    }
}

// -----------------------------------------------------------------------------
// IconTplStatusWidget
// -----------------------------------------------------------------------------

IconTplStatusWidget::IconTplStatusWidget(int pos_x, int pos_y, cairo_surface_t *icon, std::string tpl, uint num_args)
    : Widget(pos_x, pos_y, num_args), icon_(0, 0, icon), text_(0, 0, std::move(tpl), num_args - 1) {
    setInactiveStyle();
}

void IconTplStatusWidget::measure(cairo_t *cr) {
    measureChild(icon_, cr);
    measureChild(text_, cr);
    setSize(icon_.width() + SPACING + text_.width(), std::max(icon_.height(), text_.height()));
}

void IconTplStatusWidget::draw(cairo_t *cr) {
    auto [x, y] = xy(cr);
    icon_.drawAt(cr, x, y - 20);
    text_.drawAt(cr, x + icon_.width() + SPACING, y);
}

void IconTplStatusWidget::setFact(uint idx, Fact fact) {
    if (idx == 0) {
        if (fact.isDefined() && fact.getBoolValue()) {
            setActiveStyle();
        } else {
            setInactiveStyle();
        }
        return;
    }
    text_.setFact(idx - 1, std::move(fact));
    invalidateMeasure();
}

void IconTplStatusWidget::setActiveStyle() {
    const CairoColor fill{1.0, 1.0, 1.0, 1.0};
    const CairoColor outline{0.0, 0.0, 0.0, 1.0};

    icon_.setFillColor(fill);
    icon_.setOutlineColor(outline);
    text_.setFillColor(fill);
    text_.setOutlineColor(outline);
}

void IconTplStatusWidget::setInactiveStyle() {
    const CairoColor fill{0.4, 0.4, 0.44, 1.0};
    const CairoColor outline{0.0, 0.0, 0.0, 0.4};

    icon_.setFillColor(fill);
    icon_.setOutlineColor(outline);
    text_.setFillColor(fill);
    text_.setOutlineColor(outline);
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

// -----------------------------------------------------------------------------
// GPSWidget
// -----------------------------------------------------------------------------

GPSWidget::GPSWidget(int pos_x, int pos_y, uint num_args) : TextWidget(pos_x, pos_y, "", num_args) {
    assert(num_args == 3);
}

void GPSWidget::measure(cairo_t *cr) {
    if (!isReady()) {
        setSize(0, 0);
        return;
    }
    setText(formatText());
    TextWidget::measure(cr);
}

void GPSWidget::draw(cairo_t *cr) {
    if (!isReady())
        return;
    auto [x, y] = xy(cr);
    drawAt(cr, x + TEXT_OFFSET_X, y);
}

bool GPSWidget::isReady() const {
    Fact fix = fact(0);
    Fact lat = fact(1);
    Fact lon = fact(2);
    return fix.isDefined() && lat.isDefined() && lon.isDefined();
}

std::string GPSWidget::formatText() const {
    Fact fix_fact = fact(0);
    Fact lat_fact = fact(1);
    Fact lon_fact = fact(2);
    std::string fix_type = "undef";
    char buf[64];
    switch (fix_fact.getUintValue()) {
        case 0:
            fix_type = "no GPS";
            break;
        case 1:
            fix_type = "no fix";
            break;
        case 2:
            fix_type = "2D fix";
            break;
        case 3:
            fix_type = "3D fix";
            break;
        case 4:
            fix_type = "DGPS/SBAS 3D";
            break;
        case 5:
            fix_type = "RTK float 3D";
            break;
        case 6:
            fix_type = "RTK Fixed 3D";
            break;
        case 7:
            fix_type = "Static fixed";
            break;
        case 8:
            fix_type = "PPP 3D";
            break;
    }
    const double lat = lat_fact.getIntValue() * 1.0e-7;
    const double lon = lon_fact.getIntValue() * 1.0e-7;
    std::snprintf(buf, sizeof(buf), "%s Lat:%f, Lon:%f", fix_type.c_str(), lat, lon);
    return buf;
}

// -----------------------------------------------------------------------------
// TimeWidget
// -----------------------------------------------------------------------------

TimeWidget::TimeWidget(int pos_x, int pos_y, uint num_args)
    : TextWidget(pos_x, pos_y, "---- -- -- --:--:--", num_args,
                 DrawStyle{.fill = {0.8, 0.8, 0.8, 1.0}, .outline = {0.0, 0.0, 0.0, 1.0}, .outline_width = 1.0}) {}

void TimeWidget::draw(cairo_t *cr) {
    updateTime();
    TextWidget::draw(cr);
}

void TimeWidget::updateTime() {
    const auto now = std::chrono::steady_clock::now();
    if (now - last_update_ < std::chrono::seconds(1)) {
        return;
    }

    last_update_ = now;
    const auto system_now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(system_now);

    std::tm tm{};
    if (localtime_r(&time, &tm) == nullptr) {
        return;
    }

    const int year = tm.tm_year + 1900;
    if (year < MIN_VALID_YEAR) {
        setText("---- -- -- --:--:--");
        return;
    }

    char buffer[20];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm) == 0) {
        return;
    }
    setText(buffer);
}

// -----------------------------------------------------------------------------
// DebugWidget
// -----------------------------------------------------------------------------

DebugWidget::DebugWidget(int pos_x, int pos_y, uint num_args)
    : Widget(pos_x, pos_y, num_args), lines_(num_args, "undef") {}

void DebugWidget::measure(cairo_t *cr) {
    double max_width = 0.0;
    for (const auto &line : lines_) {
        cairo_text_extents_t extents;
        cairo_text_extents(cr, line.c_str(), &extents);
        max_width = std::max(max_width, extents.width);
    }
    setSize(static_cast<int>(std::ceil(max_width)), static_cast<int>(lines_.size() * LINE_HEIGHT));
}

void DebugWidget::draw(cairo_t *cr) {
    auto [x, y] = xy(cr);
    auto y_offset = y;
    for (const auto &line : lines_) {
        cairo_set_source_rgba(cr, 255.0, 50.0, 50.0, 1.0);
        cairo_move_to(cr, x, y_offset);
        cairo_show_text(cr, line.c_str());
        y_offset += LINE_HEIGHT;
        SPDLOG_INFO("dbg draw {}", line);
    }
}

void DebugWidget::setFact(uint idx, Fact fact) {
    lines_.at(idx) = formatFact(std::move(fact));
    invalidateMeasure();
}

std::string DebugWidget::formatFact(Fact fact) {
    std::ostringstream oss;

    if (!fact.isDefined()) {
        oss << "undef";
    } else {
        oss << fact.getName() << " (" << fact.getTypeName() << ") {";
        for (const auto &tag : fact.getTags()) {
            oss << tag.first << "=>" << tag.second << ", ";
        }
        oss << "} = " << fact.asString();
    }
    return oss.str();
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

// -----------------------------------------------------------------------------
// IconSelectorWidget
// -----------------------------------------------------------------------------

IconSelectorWidget::IconSelectorWidget(
    int pos_x, int pos_y, const std::vector<std::pair<std::pair<int, int>, std::filesystem::path>> &ranges_and_icons,
    const std::filesystem::path &assets_dir)
    : Widget(pos_x, pos_y, 1), assets_dir_(assets_dir) {
    // Load and cache all icons during initialization
    for (const auto &[range, icon_path] : ranges_and_icons) {
        cairo_surface_t *icon = openIcon(icon_path);
        if (icon) {
            icon_cache_[range] = icon;
        }
    }
}

IconSelectorWidget::~IconSelectorWidget() {
    for (auto &[range, icon] : icon_cache_) {
        if (icon) {
            cairo_surface_destroy(icon);
        }
    }
}

void IconSelectorWidget::measure(cairo_t *) {
    if (!current_icon_) {
        setSize(0, 0);
        return;
    }
    int width = cairo_image_surface_get_width(current_icon_);
    int height = cairo_image_surface_get_height(current_icon_);
    setSize(width + 2, height + 2);
}

void IconSelectorWidget::draw(cairo_t *cr) {
    if (!current_icon_) {
        return;
    }
    auto [x, y] = xy(cr);
    cairo_save(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx * dx + dy * dy > 1 * 1)
                continue;
            cairo_mask_surface(cr, current_icon_, x + dx, y + dy);
        }
    }
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    cairo_mask_surface(cr, current_icon_, x, y);
    cairo_restore(cr);
}

void IconSelectorWidget::setFact(uint idx, Fact fact) {
    assert(idx == 0);
    cairo_surface_t *icon = selectIcon(fact);
    if (icon == current_icon_) {
        return;
    }
    current_icon_ = icon;
    invalidateMeasure();
}

cairo_surface_t *IconSelectorWidget::selectIcon(Fact &fact) {
    if (!fact.isDefined())
        return nullptr;

    long value = 0;

    // Convert all fact types to comparable integer values
    switch (fact.getType()) {
        case Fact::T_BOOL:
            value = fact.getBoolValue() ? 1 : 0;
            break;
        case Fact::T_INT:
            value = fact.getIntValue();
            break;
        case Fact::T_UINT:
            value = static_cast<long>(fact.getUintValue());
            break;
        case Fact::T_DOUBLE:
            value = static_cast<long>(fact.getDoubleValue());
            break;
        case Fact::T_STRING:
            try {
                value = std::stol(fact.getStrValue());
            } catch (...) {
                // If string can't be converted to number, use 0
                value = 0;
            }
            break;
        case Fact::T_UNDEF:
        default:
            return nullptr;
    }

    // Iterate through the configured ranges and select the appropriate icon
    for (const auto &[range, icon] : icon_cache_) {
        if (value >= range.first && value <= range.second) {
            return icon;
        }
    }

    return nullptr; // No icon selected
}

cairo_surface_t *IconSelectorWidget::openIcon(const std::filesystem::path &icon_path) {
    std::filesystem::path full_path = assets_dir_ / icon_path;
    cairo_surface_t *icon = cairo_image_surface_create_from_png(full_path.c_str());
    if (cairo_surface_status(icon) != CAIRO_STATUS_SUCCESS) {
        spdlog::error("Failed to open icon: {}", full_path.string());
        cairo_surface_destroy(icon);
        return nullptr;
    }
    return icon;
}
