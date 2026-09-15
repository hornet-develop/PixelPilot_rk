#include "base.hpp"

#include <utility>

void Widget::setFact(uint idx, Fact fact) {
    storeFact(idx, std::move(fact));
}

int Widget::x(cairo_t *cr) const {
    cairo_surface_t *target = cairo_get_target(cr);
    int width = cairo_image_surface_get_width(target);

    return (width + position_.x) % width;
}

int Widget::y(cairo_t *cr) const {
    cairo_surface_t *target = cairo_get_target(cr);
    int height = cairo_image_surface_get_height(target);

    return (height + position_.y) % height;
}

void Widget::measureChild(Widget &child, cairo_t *cr) {
    if (child.measureDirty()) {
        child.measure(cr);
    }
}

void Widget::storeFact(uint idx, Fact fact) {
    args_.at(idx) = std::move(fact);
    measure_dirty_ = true;
}

void Widget::setSize(int width, int height) {
    size_ = {width, height};
    measure_dirty_ = false;
}
