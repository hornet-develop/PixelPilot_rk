#ifndef OSD_WIDGETS_STATUS_HPP
#define OSD_WIDGETS_STATUS_HPP

#include "base.hpp"
#include "icon.hpp"
#include "text.hpp"

#include <cstdint>
#include <string>

class IconStatusWidget : public IconWidget {
  public:
    IconStatusWidget(int pos_x, int pos_y, cairo_surface_t *icon)
        : IconWidget(pos_x, pos_y, icon, 1,
                     DrawStyle{
                         {0.4, 0.4, 0.44, 1.0},
                         {0.0, 0.0, 0.0, 0.4},
                         1.0,
                     }) {}

    void setFact(uint idx, Fact fact) override;
};

class IconTplStatusWidget : public Widget {
  public:
    IconTplStatusWidget(int pos_x, int pos_y, cairo_surface_t *icon, std::string tpl, uint num_args);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;
    void setFact(uint idx, Fact fact) override;

  private:
    void setActiveStyle();
    void setInactiveStyle();

    static constexpr int SPACING = 9;

    IconWidget icon_;
    TplTextWidget text_;
};

class DvrStatusWidget : public IconWidget {
  private:
    static constexpr DrawStyle ICON_STYLE{
        {1.0, 0.0, 0.0, 1.0},
        {0.0, 0.0, 0.0, 1.0},
        1.0,
    };

  public:
    DvrStatusWidget(int pos_x, int pos_y, cairo_surface_t *icon) : IconWidget(pos_x, pos_y, icon, 1, ICON_STYLE) {}

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;

  private:
    bool isActive() const;
};

class DvrStorageWidget : public Widget {
  public:
    DvrStorageWidget(int pos_x, int pos_y, cairo_surface_t *icon)
        : Widget(pos_x, pos_y, 2), icon_(0, 0, icon), text_(0, 0, "-") {}

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;
    void setFact(uint idx, Fact fact) override;

  private:
    void updateState();
    void updateStorageText();

    static std::string formatStorageSize(uint64_t bytes);

    static constexpr int SPACING = 5;
    bool visible_ = false;
    bool show_text_ = false;

    IconWidget icon_;
    TextWidget text_;
};

#endif