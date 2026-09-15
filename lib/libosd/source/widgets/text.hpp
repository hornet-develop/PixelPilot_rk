#ifndef OSD_WIDGETS_TEXT_HPP
#define OSD_WIDGETS_TEXT_HPP

#include "base.hpp"
#include "icon.hpp"

#include <string>
#include <sys/types.h>
#include <utility>

class TextWidget : public Widget {
  private:
    static constexpr DrawStyle DEFAULT_STYLE{
        {1.0, 1.0, 1.0, 1.0},
        {0.0, 0.0, 0.0, 1.0},
        2.0,
    };

  public:
    TextWidget(int pos_x, int pos_y, std::string text, uint num_args = 0, DrawStyle style = DEFAULT_STYLE)
        : Widget(pos_x, pos_y, num_args), text_(std::move(text)), style_(style) {}

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;

    void drawAt(cairo_t *cr, double x, double y) const {
        drawText(cr, x, y);
    }

    void setText(std::string text);

    const std::string &text() const {
        return text_;
    }

    void setFillColor(const CairoColor &color) {
        style_.fill = color;
    }

    void setOutlineColor(const CairoColor &color) {
        style_.outline = color;
    }

    void setOutlineWidth(double width);
    void setStyle(const DrawStyle &style);

    const DrawStyle &style() const {
        return style_;
    }

  protected:
    void drawText(cairo_t *cr, double x, double y) const;

  private:
    void buildTextPath(cairo_t *cr, double x, double y) const;
    void setupStroke(cairo_t *cr) const;

    std::string text_;
    DrawStyle style_;
};

class TplTextWidget : public TextWidget {
  public:
    TplTextWidget(int pos_x, int pos_y, std::string tpl, uint num_args);

    void measure(cairo_t *cr) override;

  private:
    std::string renderTpl() const;

    std::string tpl_;
};

class IconTextWidget : public Widget {
  public:
    IconTextWidget(int pos_x, int pos_y, cairo_surface_t *icon, std::string text)
        : Widget(pos_x, pos_y), icon_(0, 0, icon), text_(0, 0, std::move(text)) {}

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;

  private:
    static constexpr int SPACING = 14;

    IconWidget icon_;
    TextWidget text_;
};

class IconTplTextWidget : public TplTextWidget {
  public:
    IconTplTextWidget(int pos_x, int pos_y, cairo_surface_t *icon, std::string tpl, uint num_args)
        : TplTextWidget(pos_x, pos_y, std::move(tpl), num_args), icon_(0, 0, icon) {}

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;

  private:
    static constexpr int SPACING = 14;
    IconWidget icon_;
};

#endif