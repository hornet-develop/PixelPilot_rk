#ifndef OSD_HPP
#define OSD_HPP

#include "fact.hpp"

#include <cairo.h>

#include <filesystem>
#include <memory>
#include <string>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

class Layout;
class OsdConfigLoader;
class Widget;

class Osd {
  public:
    using WidgetEnableMap = std::unordered_map<std::string, bool>;

    Osd(uint refresh_frequency_ms, WidgetEnableMap widget_enable);
    ~Osd();

    bool loadConfig(const std::filesystem::path &path);
    void setFact(Fact fact);
    void draw(cairo_t *cr);

    void loadScreensaverImage(const std::filesystem::path &path);
    void drawScreensaver(cairo_t *cr);

  private:
    friend class OsdConfigLoader;

    struct WidgetEntry {
        std::string id;
        std::unique_ptr<Widget> widget;
        std::vector<FactMatcher> matchers;
        std::vector<Layout *> layouts;
    };

    bool addWidget(std::string id, std::unique_ptr<Widget> widget, std::vector<FactMatcher> matchers);
    bool addLayout(std::unique_ptr<Layout> layout, const std::vector<std::string> &widget_ids);

    WidgetEntry *findWidget(const std::string &id);
    bool isWidgetEnabled(const std::string &id) const;

    void measureWidgets(cairo_t *cr);

    uint refreshFrequencyMs() const {
        return refresh_frequency_ms_;
    }

    std::vector<WidgetEntry> widget_entries_;
    std::vector<std::unique_ptr<Layout>> layouts_;
    const WidgetEnableMap widget_enabled_;

    cairo_surface_t *screensaver_image_ = nullptr;
    const uint refresh_frequency_ms_;
};

#endif
