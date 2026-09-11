#ifndef OSD_WIDGETS_PRIMITIVES_HPP
#define OSD_WIDGETS_PRIMITIVES_HPP

#include "base.hpp"

#include <string>

class TextWidget : public Widget {
  public:
    TextWidget(int pos_x, int pos_y, std::string text, uint num_args = 0, DrawStyle style = DEFAULT_STYLE);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;
    void drawAt(cairo_t *cr, double x, double y) const;

    void setText(std::string text);
    const std::string &text() const;

    void setFillColor(const CairoColor &color);
    void setOutlineColor(const CairoColor &color);
    void setOutlineWidth(double width);
    void setStyle(const DrawStyle &style);

    const DrawStyle &style() const;

  protected:
    void drawText(cairo_t *cr, double x, double y) const;

  private:
    void buildTextPath(cairo_t *cr, double x, double y) const;
    void setupStroke(cairo_t *cr) const;

    static constexpr DrawStyle DEFAULT_STYLE{
        .fill = {1.0, 1.0, 1.0, 1.0},
        .outline = {0.0, 0.0, 0.0, 1.0},
        .outline_width = 2.0,
    };

    std::string text_;
    DrawStyle style_;
};

class IconWidget : public Widget {
  public:
    IconWidget(int pos_x, int pos_y, cairo_surface_t *icon, uint num_args = 0, DrawStyle style = DEFAULT_STYLE);
    ~IconWidget() override;

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;
    void drawAt(cairo_t *cr, double x, double y) const;

    void setFillColor(const CairoColor &color);
    void setOutlineColor(const CairoColor &color);
    void setOutlineWidth(double width);
    void setStyle(const DrawStyle &style);

    const DrawStyle &style() const;

  protected:
    cairo_surface_t *icon() const;

    void drawIcon(cairo_t *cr, double x, double y) const;
    int outlineWidth() const;

  private:
    static constexpr DrawStyle DEFAULT_STYLE{
        .fill = {1.0, 1.0, 1.0, 1.0},
        .outline = {0.0, 0.0, 0.0, 1.0},
        .outline_width = 1.0,
    };

    cairo_surface_t *icon_;
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

class BoxWidget : public Widget {
  public:
    BoxWidget(int pos_x, int pos_y, uint width, uint height, CairoColor color);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;

  private:
    uint width_;
    uint height_;
    CairoColor color_;
};

#endif
