#ifndef OSD_HPP
#define OSD_HPP

#include "fact.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <sys/types.h>
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

    bool loadWidgets(const nlohmann::json &widgets_json, const std::filesystem::path &assets_dir);
    std::unique_ptr<Widget> createWidget(const nlohmann::json &cfg, const std::filesystem::path &assets_dir,
                                         const std::string &name, const std::string &type, int x, int y, uint num_args);

    bool loadLayouts(const nlohmann::json &layouts_json);

    void addWidget(std::unique_ptr<Widget> widget, std::vector<FactMatcher> param_matchers, const std::string &id);
    void addWidgetToLayout(Layout *layout, Widget *widget);

    void measureWidgets(cairo_t *cr);

    std::vector<std::unique_ptr<Widget>> widgets_;
    std::vector<std::unique_ptr<Layout>> layouts_;

    std::unordered_map<Widget *, std::vector<Layout *>> widget_layouts_;
    std::unordered_map<std::string, Widget *> widgets_by_id_;
    std::vector<std::tuple<FactMatcher, Widget *, uint>> matchers_;

    cairo_surface_t *screensaver_image_ = nullptr;
    const uint refresh_frequency_ms_;
};

#endif
