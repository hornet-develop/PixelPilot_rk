#include "status.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <utility>

// -----------------------------------------------------------------------------
// IconStatusWidget
// -----------------------------------------------------------------------------

void IconStatusWidget::setFact(uint idx, Fact fact) {
    if (idx != 0) {
        assert(false && "IconStatusWidget fact index out of range");
        return;
    }
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
    const auto [x, y] = xy(cr);
    icon_.drawAt(cr, x, y - 20);
    text_.drawAt(cr, x + icon_.width() + SPACING, y);
}

void IconTplStatusWidget::setFact(uint idx, Fact fact) {
    if (idx >= factCount()) {
        assert(false && "IconTplStatusWidget fact index out of range");
        return;
    }
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
// DvrStatusWidget
// -----------------------------------------------------------------------------

void DvrStatusWidget::measure(cairo_t *cr) {
    if (!isActive()) {
        setSize(0, 0);
        return;
    }
    IconWidget::measure(cr);
}

void DvrStatusWidget::draw(cairo_t *cr) {
    if (!isActive()) {
        return;
    }
    IconWidget::draw(cr);
}

bool DvrStatusWidget::isActive() const {
    const Fact &status = fact(0);
    return status.isDefined() && status.getBoolValue();
}

// -----------------------------------------------------------------------------
// DvrStorageWidget
// -----------------------------------------------------------------------------

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
    const auto [x, y] = xy(cr);
    icon_.drawAt(cr, x, y - 20);
    if (show_text_) {
        text_.drawAt(cr, x + icon_.width() + SPACING, y);
    }
}

void DvrStorageWidget::setFact(uint idx, Fact fact) {
    if (idx >= factCount()) {
        assert(false && "DvrStorageWidget fact index out of range");
        return;
    }
    storeFact(idx, std::move(fact));
    updateState();
}

void DvrStorageWidget::updateState() {
    const Fact &status_fact = fact(0);
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
    const Fact &storage_fact = fact(1);
    if (!storage_fact.isDefined()) {
        text_.setText("-");
        return;
    }
    text_.setText(formatStorageSize(storage_fact.getUintValue()));
}

std::string DvrStorageWidget::formatStorageSize(uint64_t bytes) {
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
    std::snprintf(buf, sizeof(buf), "%.1f %s", value, unit->name);

    return buf;
}
