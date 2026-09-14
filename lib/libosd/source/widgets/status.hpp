#ifndef OSD_WIDGETS_STATUS_HPP
#define OSD_WIDGETS_STATUS_HPP

#include "base.hpp"
#include "icon.hpp"
#include "text.hpp"

#include <cstdint>
#include <string>

class IconStatusWidget : public IconWidget {
  public:
    IconStatusWidget(int pos_x, int pos_y, cairo_surface_t *icon);

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

class DvrStatusWidget : public Widget {
  public:
    DvrStatusWidget(int pos_x, int pos_y, cairo_surface_t *icon, std::string text);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;

  private:
    bool isActive() const;

    static constexpr int SPACING = 14;

    static constexpr DrawStyle ICON_STYLE{
        .fill = {1.0, 0.0, 0.0, 1.0},
        .outline = {0.0, 0.0, 0.0, 1.0},
        .outline_width = 1.0,
    };

    static constexpr DrawStyle TEXT_STYLE{
        .fill = {1.0, 0.0, 0.0, 1.0},
        .outline = {0.0, 0.0, 0.0, 1.0},
        .outline_width = 2.0,
    };

    IconWidget icon_;
    TextWidget text_;
};

class DvrStorageWidget : public Widget {
  public:
    DvrStorageWidget(int pos_x, int pos_y, cairo_surface_t *icon);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;
    void setFact(uint idx, Fact fact) override;

  private:
    void updateState();
    void updateStorageText();

    static std::string format_storage_size(uint64_t bytes);

    static constexpr int SPACING = 5;
    bool visible_ = false;
    bool show_text_ = false;

    IconWidget icon_;
    TextWidget text_;
};

#endif