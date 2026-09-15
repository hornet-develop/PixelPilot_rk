#include "info.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>

// -----------------------------------------------------------------------------
// TimeWidget
// -----------------------------------------------------------------------------

TimeWidget::TimeWidget(int pos_x, int pos_y, uint num_args)
    : TextWidget(pos_x, pos_y, "---- -- -- --:--:--", num_args,
                 DrawStyle{{0.8, 0.8, 0.8, 1.0}, {0.0, 0.0, 0.0, 1.0}, 1.0}) {}

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
    const Fact &fix = fact(0);
    const Fact &lat = fact(1);
    const Fact &lon = fact(2);
    return fix.isDefined() && lat.isDefined() && lon.isDefined();
}

std::string GPSWidget::formatText() const {
    const Fact &fix_fact = fact(0);
    const Fact &lat_fact = fact(1);
    const Fact &lon_fact = fact(2);
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
    std::snprintf(buf, sizeof(buf), "%s Lat:%.7f, Lon:%.7f", fix_type.c_str(), lat, lon);
    return buf;
}

// -----------------------------------------------------------------------------
// DebugWidget
// -----------------------------------------------------------------------------

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
    const auto [x, y] = xy(cr);
    auto y_offset = y;
    for (const auto &line : lines_) {
        cairo_set_source_rgba(cr, 1.0, 50.0 / 255.0, 50.0 / 255.0, 1.0);
        cairo_move_to(cr, x, y_offset);
        cairo_show_text(cr, line.c_str());
        y_offset += LINE_HEIGHT;
    }
}

void DebugWidget::setFact(uint idx, Fact fact) {
    if (idx >= lines_.size()) {
        assert(false && "DebugWidget fact index out of range");
        return;
    }
    lines_[idx] = formatFact(fact);
    invalidateMeasure();
}

std::string DebugWidget::formatFact(const Fact &fact) {
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
// PopupWidget
// -----------------------------------------------------------------------------

PopupWidget::PopupWidget(int pos_x, int pos_y, uint timeout_ms, uint) : Widget(pos_x, pos_y), timeout_(timeout_ms) {
    assert(timeout_ms > 0);
}

void PopupWidget::measure(cairo_t *cr) {
    const auto now = std::chrono::steady_clock::now();
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
        total_height += msg.height + PADDING * 2;
    }
    if (!msgs_.empty()) {
        total_height += ITEM_SPACING * (msgs_.size() - 1);
    }
    setSize(static_cast<int>(std::ceil(max_width)), static_cast<int>(std::ceil(total_height)));
}

void PopupWidget::draw(cairo_t *cr) {
    const auto now = std::chrono::steady_clock::now();
    if (removeExpired(now)) {
        invalidateMeasure();
    }
    const auto [x, y] = xy(cr);
    double y_offset = y;
    for (const auto &msg : msgs_) {
        const auto past = std::chrono::duration_cast<std::chrono::milliseconds>(now - msg.time);
        const double fade_fraction = 1.0 - static_cast<double>(past.count()) / static_cast<double>(timeout_.count());

        // Draw popup box
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, fade_fraction / 3.0);
        cairo_rectangle(cr, x - PADDING, y_offset + PADDING, msg.width + (PADDING * 2), -(msg.height + (PADDING * 2)));
        cairo_fill(cr);

        // Draw popup text
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, fade_fraction);
        cairo_move_to(cr, x, y_offset);
        cairo_show_text(cr, msg.text.c_str());
        y_offset += msg.height + (PADDING * 2) + ITEM_SPACING;
    }
}

void PopupWidget::setFact(uint, Fact fact) {
    msgs_.push_back({std::chrono::steady_clock::now(), fact.getStrValue()});
    invalidateMeasure();
}

bool PopupWidget::removeExpired(Clock::time_point now) {
    bool removed = false;
    while (!msgs_.empty() && now - msgs_.front().time > timeout_) {
        msgs_.pop_front();
        removed = true;
    }
    return removed;
}