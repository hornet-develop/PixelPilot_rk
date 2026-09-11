#include "primitives.hpp"

#include <cmath>
#include <cstdio>
#include <utility>

// -----------------------------------------------------------------------------
// TextWidget
// -----------------------------------------------------------------------------

TextWidget::TextWidget(int pos_x, int pos_y, std::string text, uint num_args, DrawStyle style)
    : Widget(pos_x, pos_y, num_args), text_(std::move(text)), style_(style) {}

void TextWidget::setText(std::string text) {
    if (text_ == text)
        return;

    text_ = std::move(text);
    invalidateMeasure();
}

const std::string &TextWidget::text() const {
    return text_;
}

void TextWidget::draw(cairo_t *cr) {
    auto [x, y] = xy(cr);
    drawAt(cr, x, y);
}

void TextWidget::drawAt(cairo_t *cr, double x, double y) const {
    drawText(cr, x, y);
}

void TextWidget::measure(cairo_t *cr) {
    cairo_save(cr);
    buildTextPath(cr, 0.0, 0.0);

    double x1, y1, x2, y2;
    if (style_.outline_width > 0.0) {
        setupStroke(cr);
        cairo_stroke_extents(cr, &x1, &y1, &x2, &y2);
    } else {
        cairo_fill_extents(cr, &x1, &y1, &x2, &y2);
    }
    setSize(static_cast<int>(std::ceil(x2 - x1)), static_cast<int>(std::ceil(y2 - y1)));

    cairo_new_path(cr);
    cairo_restore(cr);
}

void TextWidget::setFillColor(const CairoColor &color) {
    style_.fill = color;
}

void TextWidget::setOutlineColor(const CairoColor &color) {
    style_.outline = color;
}

void TextWidget::setOutlineWidth(double width) {
    if (style_.outline_width == width)
        return;

    style_.outline_width = width;
    invalidateMeasure();
}

void TextWidget::setStyle(const DrawStyle &style) {
    if (style_.outline_width != style.outline_width)
        invalidateMeasure();
    style_ = style;
}

const DrawStyle &TextWidget::style() const {
    return style_;
}

void TextWidget::drawText(cairo_t *cr, double x, double y) const {
    cairo_save(cr);
    buildTextPath(cr, x, y);

    if (style_.outline_width > 0.0) {
        setupStroke(cr);
        cairo_set_source_rgba(cr, style_.outline.r, style_.outline.g, style_.outline.b, style_.outline.a);
        cairo_stroke_preserve(cr);
    }
    cairo_set_source_rgba(cr, style_.fill.r, style_.fill.g, style_.fill.b, style_.fill.a);
    cairo_fill(cr);
    cairo_restore(cr);
}

void TextWidget::buildTextPath(cairo_t *cr, double x, double y) const {
    cairo_new_path(cr);
    cairo_move_to(cr, x, y);
    cairo_text_path(cr, text_.c_str());
}

void TextWidget::setupStroke(cairo_t *cr) const {
    cairo_set_line_width(cr, style_.outline_width);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
}

// -----------------------------------------------------------------------------
// IconWidget
// -----------------------------------------------------------------------------

IconWidget::IconWidget(int pos_x, int pos_y, cairo_surface_t *icon, uint num_args, DrawStyle style)
    : Widget(pos_x, pos_y, num_args), icon_(icon), style_(style) {}

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

void IconWidget::setFillColor(const CairoColor &color) {
    style_.fill = color;
}

void IconWidget::setOutlineColor(const CairoColor &color) {
    style_.outline = color;
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

const DrawStyle &IconWidget::style() const {
    return style_;
}
cairo_surface_t *IconWidget::icon() const {
    return icon_;
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
// TplTextWidget
// -----------------------------------------------------------------------------

TplTextWidget::TplTextWidget(int pos_x, int pos_y, std::string tpl, uint num_args)
    : TextWidget(pos_x, pos_y, "", num_args), tpl_(std::move(tpl)) {
    setText(renderTpl());
}

void TplTextWidget::measure(cairo_t *cr) {
    setText(renderTpl());
    TextWidget::measure(cr);
}

std::string TplTextWidget::renderTpl() const {
    std::string msg;
    msg.reserve(tpl_.size());

    uint fact_i = 0;
    for (std::size_t i = 0; i < tpl_.size(); ++i) {
        char c = tpl_[i];
        if (c != '%') {
            msg.push_back(c);
            continue;
        }
        if (i + 1 >= tpl_.size()) {
            msg.push_back('%');
            break;
        }

        char spec = tpl_[++i];
        if (spec == '%') {
            msg.push_back('%');
            continue;
        }
        if (fact_i >= factCount()) {
            msg.push_back('-');
            continue;
        }

        Fact fact = this->fact(fact_i++);
        if (!fact.isDefined()) {
            msg.push_back('-');
            continue;
        }
        switch (spec) {
            case 'b':
                msg.push_back(fact.getBoolValue() ? 't' : 'f');
                break;
            case 'd':
            case 'i':
                msg.append(std::to_string(fact.getIntValue()));
                break;
            case 'u':
                msg.append(std::to_string(fact.getUintValue()));
                break;
            case 'f': {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.2f", fact.getDoubleValue());
                msg.append(buf);
                break;
            }
            case 's':
                msg.append(fact.getStrValue());
                break;
            default:
                msg.push_back('-');
                break;
        }
    }
    return msg;
}

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
