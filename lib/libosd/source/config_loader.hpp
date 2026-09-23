#ifndef OSD_CONFIG_LOADER_HPP
#define OSD_CONFIG_LOADER_HPP

#include <fact.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

class Osd;

class OsdConfigLoader {
  public:
    explicit OsdConfigLoader(Osd &osd) : osd_(osd) {}

    bool load(const std::filesystem::path &path);

  private:
    using Json = nlohmann::json;

    struct WidgetCommon {
        std::string name;
        std::string type;
        std::string id;
        int x;
        int y;
        std::vector<FactMatcher> matchers;
    };

    bool loadConfig(const Json &config);

    bool loadWidgets(const Json &widgets);
    bool loadWidget(const Json &widget, WidgetCommon common);
    std::vector<FactMatcher> parseFacts(const Json &facts) const;

    bool loadLayouts(const Json &layouts);

    Osd &osd_;
    std::filesystem::path assets_dir_;
};

#endif