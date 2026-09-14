#ifndef OSD_WIDGETS_ICON_HPP
#define OSD_WIDGETS_ICON_HPP

#include "base.hpp"

#include <filesystem>
#include <map>
#include <utility>
#include <vector>

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
    cairo_surface_t *selectIcon(Fact &fact);
    cairo_surface_t *openIcon(const std::filesystem::path &icon_path);

    std::map<std::pair<int, int>, cairo_surface_t *> icon_cache_; // Cache of loaded icons
    std::filesystem::path assets_dir_;
    cairo_surface_t *current_icon_ = nullptr; // Currently selected icon
};

#endif