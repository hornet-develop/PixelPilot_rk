#include "text.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
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