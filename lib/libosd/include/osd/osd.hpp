#ifndef OSD_OSD_HPP
#define OSD_OSD_HPP

#include "fact.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <cairo.h>
#include <nlohmann/json_fwd.hpp>

class Layout;
class Widget;

class Osd {
  public:
    explicit Osd(uint refresh_frequency_ms);
    ~Osd();

    bool loadConfig(const std::filesystem::path &path);
    void loadScreensaverImage(const std::string &path);

    void draw(cairo_t *cr);
    void drawScreensaver(cairo_t *cr);

    void setFact(Fact fact);

  private:
    bool loadConfigJson(const nlohmann::json &cfg);

    Osd *addWidget(std::unique_ptr<Widget> widget, std::vector<FactMatcher> param_matchers, const std::string &id = "");

    void addWidgetToLayout(Layout *layout, Widget *widget);
    void measureWidgets(cairo_t *cr);

    cairo_surface_t *openIcon(const std::string &widget_name, const std::filesystem::path &base_path,
                              std::filesystem::path icon_path);

    std::vector<std::unique_ptr<Layout>> layouts;
    std::unordered_map<Widget *, std::vector<Layout *>> widget_layouts;
    std::unordered_map<std::string, Widget *> widgets_by_id;

    std::vector<std::unique_ptr<Widget>> widgets;
    std::vector<std::tuple<FactMatcher, Widget *, uint>> matchers;

    cairo_surface_t *screensaver_image_ = nullptr;
    uint refresh_frequency_ms_;
};

#endif
