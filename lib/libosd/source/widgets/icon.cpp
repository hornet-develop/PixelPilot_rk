#include "icon.hpp"

#include <cassert>
#include <cmath>
#include <string>

#include <spdlog/spdlog.h>

namespace {

cairo_surface_t *openIcon(const std::filesystem::path &path) {
    cairo_surface_t *icon = cairo_image_surface_create_from_png(path.c_str());
    if (cairo_surface_status(icon) != CAIRO_STATUS_SUCCESS) {
        spdlog::error("Failed to open icon: {}", path.string());
        cairo_surface_destroy(icon);
        return nullptr;
    }
    return icon;
}

} // namespace

// -----------------------------------------------------------------------------
// IconWidget
// -----------------------------------------------------------------------------

IconWidget::~IconWidget() {
    if (icon_)
        cairo_surface_destroy(icon_);
}

void IconWidget::measure(cairo_t *) {
    if (!icon_) {
        setSize(0, 0);
        return;
    }
    int width = cairo_image_surface_get_width(icon_);
    int height = cairo_image_surface_get_height(icon_);
    int outline = outlineWidth();

    setSize(width + outline * 2, height + outline * 2);
}

void IconWidget::draw(cairo_t *cr) {
    auto [x, y] = xy(cr);
    drawIcon(cr, x, y - 20);
}

void IconWidget::drawAt(cairo_t *cr, double x, double y) const {
    drawIcon(cr, x, y);
}

void IconWidget::setOutlineWidth(double width) {
    if (style_.outline_width == width)
        return;

    style_.outline_width = width;
    invalidateMeasure();
}

void IconWidget::setStyle(const DrawStyle &style) {
    if (style_.outline_width != style.outline_width)
        invalidateMeasure();
    style_ = style;
}

void IconWidget::drawIcon(cairo_t *cr, double x, double y) const {
    if (!icon_)
        return;

    cairo_save(cr);
    int outline = outlineWidth();

    if (outline > 0) {
        cairo_set_source_rgba(cr, style_.outline.r, style_.outline.g, style_.outline.b, style_.outline.a);
        for (int dx = -outline; dx <= outline; ++dx) {
            for (int dy = -outline; dy <= outline; ++dy) {
                if (dx * dx + dy * dy > outline * outline)
                    continue;
                cairo_mask_surface(cr, icon_, x + dx, y + dy);
            }
        }
    }
    cairo_set_source_rgba(cr, style_.fill.r, style_.fill.g, style_.fill.b, style_.fill.a);
    cairo_mask_surface(cr, icon_, x, y);
    cairo_restore(cr);
}

int IconWidget::outlineWidth() const {
    return static_cast<int>(std::ceil(style_.outline_width));
}

// -----------------------------------------------------------------------------
// IconSelectorWidget
// -----------------------------------------------------------------------------

IconSelectorWidget::IconSelectorWidget(
    int pos_x, int pos_y, const std::vector<std::pair<std::pair<int, int>, std::filesystem::path>> &ranges_and_icons,
    const std::filesystem::path &assets_dir)
    : Widget(pos_x, pos_y, 1) {
    // Load and cache all icons during initialization
    for (const auto &[range, icon_path] : ranges_and_icons) {
        cairo_surface_t *icon = openIcon(assets_dir / icon_path);
        if (icon) {
            icons_.push_back({range, icon});
        }
    }
}

IconSelectorWidget::~IconSelectorWidget() {
    for (const auto &icon : icons_) {
        cairo_surface_destroy(icon.surface);
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
    if (idx != 0) {
        spdlog::error("IconSelectorWidget: invalid fact index {}", idx);
        assert(false && "IconSelectorWidget fact index out of range");
        return;
    }
    cairo_surface_t *icon = selectIcon(fact);
    if (icon == current_icon_) {
        return;
    }
    current_icon_ = icon;
    invalidateMeasure();
}

cairo_surface_t *IconSelectorWidget::selectIcon(const Fact &fact) const {
    if (!fact.isDefined()) {
        return nullptr;
    }

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
        case Fact::T_STRING: {
            try {
                size_t pos = 0;
                const std::string &str = fact.getStrValue();
                value = std::stol(str, &pos);
                if (pos != str.size()) {
                    return nullptr;
                }
            } catch (...) {
                return nullptr;
            }
            break;
        }
        case Fact::T_UNDEF:
        default:
            return nullptr;
    }

    // Iterate through the configured ranges and select the appropriate icon
    for (const auto &icon : icons_) {
        if (value >= icon.range.first && value <= icon.range.second) {
            return icon.surface;
        }
    }

    return nullptr; // No icon selected
}