#include "base.hpp"

#include <utility>

Widget::Widget(int pos_x, int pos_y, uint num_args) : position_{pos_x, pos_y}, args_(num_args) {}

void Widget::setFact(uint idx, Fact fact) {
    storeFact(idx, std::move(fact));
}

void Widget::setPosition(int x, int y) {
    position_ = {x, y};
}

bool Widget::measureDirty() const {
    return measure_dirty_;
}

int Widget::width() const {
    return size_.width;
}

int Widget::height() const {
    return size_.height;
}

const Size &Widget::size() const {
    return size_;
}

const Position &Widget::position() const {
    return position_;
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

std::pair<int, int> Widget::xy(cairo_t *cr) const {
    return {x(cr), y(cr)};
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

const Fact &Widget::fact(uint idx) const {
    return args_.at(idx);
}

uint Widget::factCount() const {
    return static_cast<uint>(args_.size());
}

void Widget::invalidateMeasure() {
    measure_dirty_ = true;
}

void Widget::setSize(int width, int height) {
    size_ = {width, height};
    measure_dirty_ = false;
}
