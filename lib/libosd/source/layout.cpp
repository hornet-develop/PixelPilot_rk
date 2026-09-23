#include "layout.hpp"
#include "widgets/base.hpp"

// -----------------------------------------------------------------------------
// Layout
// -----------------------------------------------------------------------------

void Layout::addWidget(Widget *widget) {
    widgets_.push_back(widget);
    dirty_ = true;
}

void Layout::update(cairo_t *cr) {
    if (!dirty_)
        return;

    doUpdate(cr);
    dirty_ = false;
}

int Layout::x(cairo_t *cr) const {
    cairo_surface_t *target = cairo_get_target(cr);
    const int width = cairo_image_surface_get_width(target);
    return (width + pos_x_) % width;
}

int Layout::y(cairo_t *cr) const {
    cairo_surface_t *target = cairo_get_target(cr);
    const int height = cairo_image_surface_get_height(target);
    return (height + pos_y_) % height;
}

// -----------------------------------------------------------------------------
// HorizontalLayout
// -----------------------------------------------------------------------------

void HorizontalLayout::doUpdate(cairo_t *cr) {
    int cursor_x = x(cr);
    const int pos_y = y(cr);
    const bool rtl = direction_ == Direction::RightToLeft;

    bool first = true;

    for (Widget *widget : widgets()) {
        if (widget->width() == 0 && widget->height() == 0)
            continue;
        if (!first)
            cursor_x += rtl ? -spacing() : spacing();
        if (rtl)
            cursor_x -= widget->width();
        widget->setPosition(cursor_x, pos_y);
        if (!rtl)
            cursor_x += widget->width();
        first = false;
    }
}
