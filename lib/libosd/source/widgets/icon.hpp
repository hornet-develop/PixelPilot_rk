#ifndef OSD_WIDGETS_ICON_HPP
#define OSD_WIDGETS_ICON_HPP

#include "base.hpp"

#include <filesystem>
#include <sys/types.h>
#include <utility>
#include <vector>

class IconWidget : public Widget {
  private:
    static constexpr DrawStyle DEFAULT_STYLE{
        {1.0, 1.0, 1.0, 1.0},
        {0.0, 0.0, 0.0, 1.0},
        1.0,
    };

  public:
    IconWidget(int pos_x, int pos_y, cairo_surface_t *icon, uint num_args = 0, DrawStyle style = DEFAULT_STYLE)
        : Widget(pos_x, pos_y, num_args), icon_(icon), style_(style) {}

    ~IconWidget() override;

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;
    void drawAt(cairo_t *cr, double x, double y) const;

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
    cairo_surface_t *icon() const {
        return icon_;
    }

    void drawIcon(cairo_t *cr, double x, double y) const;
    int outlineWidth() const;

  private:
    cairo_surface_t *icon_;
    DrawStyle style_;
};

class IconSelectorWidget : public Widget {
  public:
    IconSelectorWidget(int pos_x, int pos_y,
                       const std::vector<std::pair<std::pair<int, int>, std::filesystem::path>> &ranges_and_icons,
                       const std::filesystem::path &assets_dir);
    ~IconSelectorWidget() override;

    void measure(cairo_t *) override;
    void draw(cairo_t *cr) override;
    void setFact(uint idx, Fact fact) override;

  private:
    struct CachedIcon {
        std::pair<int, int> range;
        cairo_surface_t *surface;
    };

    cairo_surface_t *selectIcon(const Fact &fact) const;

    std::vector<CachedIcon> icons_;
    cairo_surface_t *current_icon_ = nullptr;
};

#endif