#include "config_loader.hpp"

#include "layout.hpp"
#include "widgets/graphics.hpp"
#include "widgets/icon.hpp"
#include "widgets/info.hpp"
#include "widgets/status.hpp"
#include "widgets/text.hpp"
#include "widgets/video.hpp"

#include <osd.hpp>

#include <cairo.h>

#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace {

cairo_surface_t *openIcon(const std::string &widget_name, const std::filesystem::path &base_path,
                          std::filesystem::path icon_path) {
    if (icon_path.is_relative()) {
        icon_path = base_path / icon_path;
    }
    cairo_surface_t *icon = cairo_image_surface_create_from_png(icon_path.c_str());
    const cairo_status_t status = cairo_surface_status(icon);
    if (status != CAIRO_STATUS_SUCCESS) {
        spdlog::error(
            "Widget '{}': Can't open icon '{}': {}", widget_name, icon_path.string(), cairo_status_to_string(status));
        cairo_surface_destroy(icon);
        return nullptr;
    }
    return icon;
}

} // namespace

bool OsdConfigLoader::load(const std::filesystem::path &path) {
    std::ifstream file(path);
    if (!file) {
        spdlog::error("Failed to open OSD config '{}'", path.string());
        return false;
    }

    try {
        const Json cfg = Json::parse(file);
        return loadConfig(cfg);
    } catch (const Json::parse_error &e) {
        spdlog::error("Failed to parse OSD config '{}': {}", path.string(), e.what());
        return false;
    } catch (const Json::exception &e) {
        spdlog::error("Invalid OSD config '{}': {}", path.string(), e.what());
        return false;
    }
}

bool OsdConfigLoader::loadConfig(const Json &cfg) {
    if (!cfg.contains("format")) {
        spdlog::error("OSD config doesn't have 'format' key");
        return false;
    }
    if (!cfg.contains("widgets") || !cfg.at("widgets").is_array()) {
        spdlog::error("OSD config doesn't have valid 'widgets' array");
        return false;
    }
    assets_dir_ = cfg.value("assets_dir", std::string{"."});

    if (!loadWidgets(cfg.at("widgets"))) {
        return false;
    }
    if (cfg.contains("layouts")) {
        if (!cfg.at("layouts").is_array()) {
            spdlog::error("OSD config doesn't have valid 'layouts' array");
            return false;
        }
        if (!loadLayouts(cfg.at("layouts"))) {
            return false;
        }
    }
    return true;
}

bool OsdConfigLoader::loadWidgets(const Json &widgets) {
    for (const auto &widget : widgets) {
        WidgetCommon params{
            widget.at("name").get<std::string>(),
            widget.at("type").get<std::string>(),
            widget.at("id").get<std::string>(),
            widget.at("x").get<int>(),
            widget.at("y").get<int>(),
            {},
        };
        const auto &facts = widget.at("facts");
        if (!facts.is_array()) {
            spdlog::error("Widget '{}': 'facts' must be an array", params.name);
            return false;
        }
        params.matchers = parseFacts(facts);
        if (!loadWidget(widget, std::move(params))) {
            return false;
        }
    }
    return true;
}

bool OsdConfigLoader::loadWidget(const Json &widget, WidgetCommon params) {
    const auto &name = params.name;
    const auto &type = params.type;

    const int x = params.x;
    const int y = params.y;

    const uint num_args = static_cast<uint>(params.matchers.size());
    const uint refresh_frequency_ms = osd_.refreshFrequencyMs();

    // -------------------------------------------------------------------------
    // Text widgets
    // -------------------------------------------------------------------------

    if (type == "TextWidget") {
        const auto text = widget.at("text").get<std::string>();
        return osd_.addWidget(
            std::move(params.id), std::make_unique<TextWidget>(x, y, text, num_args), std::move(params.matchers));
    }
    if (type == "TplTextWidget") {
        const auto tpl = widget.at("template").get<std::string>();
        return osd_.addWidget(
            std::move(params.id), std::make_unique<TplTextWidget>(x, y, tpl, num_args), std::move(params.matchers));
    }
    if (type == "IconTplTextWidget") {
        const auto tpl = widget.at("template").get<std::string>();
        const auto icon_path = widget.at("icon_path").get<std::string>();
        cairo_surface_t *icon = openIcon(name, assets_dir_, icon_path);
        if (!icon) {
            return false;
        }
        return osd_.addWidget(std::move(params.id),
                              std::make_unique<IconTplTextWidget>(x, y, icon, tpl, num_args),
                              std::move(params.matchers));
    }

    // -------------------------------------------------------------------------
    // Icon widgets
    // -------------------------------------------------------------------------

    if (type == "IconSelectorWidget") {
        std::vector<std::pair<std::pair<int, int>, std::filesystem::path>> ranges_and_icons;
        for (const auto &range_icon : widget.at("ranges_and_icons")) {
            const auto &range = range_icon.at("range");
            const int range_start = range.at(0).get<int>();
            const int range_end = range.at(1).get<int>();
            const auto icon_path = range_icon.at("icon_path").get<std::string>();
            ranges_and_icons.push_back({{range_start, range_end}, icon_path});
        }
        return osd_.addWidget(std::move(params.id),
                              std::make_unique<IconSelectorWidget>(x, y, ranges_and_icons, assets_dir_),
                              std::move(params.matchers));
    }

    // -------------------------------------------------------------------------
    // Status widgets
    // -------------------------------------------------------------------------

    if (type == "IconStatusWidget") {
        const auto icon_path = widget.at("icon_path").get<std::string>();
        cairo_surface_t *icon = openIcon(name, assets_dir_, icon_path);
        if (!icon) {
            return false;
        }
        return osd_.addWidget(
            std::move(params.id), std::make_unique<IconStatusWidget>(x, y, icon), std::move(params.matchers));
    }
    if (type == "IconTplStatusWidget") {
        const auto tpl = widget.at("template").get<std::string>();
        const auto icon_path = widget.at("icon_path").get<std::string>();
        cairo_surface_t *icon = openIcon(name, assets_dir_, icon_path);
        if (!icon) {
            return false;
        }
        return osd_.addWidget(std::move(params.id),
                              std::make_unique<IconTplStatusWidget>(x, y, icon, tpl, num_args),
                              std::move(params.matchers));
    }
    if (type == "DvrStatusWidget") {
        const auto icon_path = widget.at("icon_path").get<std::string>();
        cairo_surface_t *icon = openIcon(name, assets_dir_, icon_path);
        if (!icon) {
            return false;
        }
        return osd_.addWidget(
            std::move(params.id), std::make_unique<DvrStatusWidget>(x, y, icon), std::move(params.matchers));
    }
    if (type == "DvrStorageWidget") {
        const auto icon_path = widget.at("icon_path").get<std::string>();
        cairo_surface_t *icon = openIcon(name, assets_dir_, icon_path);
        if (!icon) {
            return false;
        }
        return osd_.addWidget(
            std::move(params.id), std::make_unique<DvrStorageWidget>(x, y, icon), std::move(params.matchers));
    }

    // -------------------------------------------------------------------------
    // Video widgets
    // -------------------------------------------------------------------------

    if (type == "VideoWidget") {
        const auto tpl = widget.at("template").get<std::string>();
        const auto icon_path = widget.at("icon_path").get<std::string>();
        const uint window_size_ms = widget.at("per_second_window_s").get<uint>() * 1000;
        const uint bucket_size_ms = widget.at("per_second_bucket_ms").get<uint>();
        const uint refresh_rate_ms = widget.value("refresh_rate_ms", refresh_frequency_ms);
        cairo_surface_t *icon = openIcon(name, assets_dir_, icon_path);
        if (!icon) {
            return false;
        }
        return osd_.addWidget(
            std::move(params.id),
            std::make_unique<VideoWidget>(
                x, y, window_size_ms, bucket_size_ms, icon, tpl, refresh_rate_ms, num_args, refresh_frequency_ms),
            std::move(params.matchers));
    }
    if (type == "VideoBitrateWidget") {
        const auto tpl = widget.at("template").get<std::string>();
        const auto icon_path = widget.at("icon_path").get<std::string>();
        const uint window_size_ms = widget.at("per_second_window_s").get<uint>() * 1000;
        const uint bucket_size_ms = widget.at("per_second_bucket_ms").get<uint>();
        const uint refresh_rate_ms = widget.value("refresh_rate_ms", refresh_frequency_ms);
        cairo_surface_t *icon = openIcon(name, assets_dir_, icon_path);
        if (!icon) {
            return false;
        }
        return osd_.addWidget(
            std::move(params.id),
            std::make_unique<VideoBitrateWidget>(
                x, y, window_size_ms, bucket_size_ms, icon, tpl, refresh_rate_ms, num_args, refresh_frequency_ms),
            std::move(params.matchers));
    }
    if (type == "VideoDecodeLatencyWidget") {
        const auto tpl = widget.at("template").get<std::string>();
        const auto icon_path = widget.at("icon_path").get<std::string>();
        const uint window_size_ms = widget.at("per_second_window_s").get<uint>() * 1000;
        const uint bucket_size_ms = widget.at("per_second_bucket_ms").get<uint>();
        const uint refresh_rate_ms = widget.value("refresh_rate_ms", refresh_frequency_ms);
        cairo_surface_t *icon = openIcon(name, assets_dir_, icon_path);
        if (!icon) {
            return false;
        }
        return osd_.addWidget(
            std::move(params.id),
            std::make_unique<VideoDecodeLatencyWidget>(
                x, y, window_size_ms, bucket_size_ms, icon, tpl, refresh_rate_ms, num_args, refresh_frequency_ms),
            std::move(params.matchers));
    }

    // -------------------------------------------------------------------------
    // Graphics widgets
    // -------------------------------------------------------------------------

    if (type == "BoxWidget") {
        const auto width = widget.at("width").get<uint>();
        const auto height = widget.at("height").get<uint>();
        const auto &color = widget.at("color");
        const CairoColor cairo_color{
            color.at("r").get<double>(),
            color.at("g").get<double>(),
            color.at("b").get<double>(),
            color.at("alpha").get<double>(),
        };
        return osd_.addWidget(std::move(params.id),
                              std::make_unique<BoxWidget>(x, y, width, height, cairo_color),
                              std::move(params.matchers));
    }
    if (type == "BarChartWidget") {
        const auto width = widget.at("width").get<uint>();
        const auto height = widget.at("height").get<uint>();
        const auto window_s = widget.at("window_s").get<uint>();
        const auto num_buckets = widget.at("num_buckets").get<uint>();
        const auto stats_kind_str = widget.at("stats_kind").get<std::string>();
        BarChartWidget::StatsField stats_kind;
        if (stats_kind_str == "sum") {
            stats_kind = BarChartWidget::STATS_SUM;
        } else if (stats_kind_str == "min") {
            stats_kind = BarChartWidget::STATS_MIN;
        } else if (stats_kind_str == "max") {
            stats_kind = BarChartWidget::STATS_MAX;
        } else if (stats_kind_str == "count") {
            stats_kind = BarChartWidget::STATS_COUNT;
        } else if (stats_kind_str == "avg") {
            stats_kind = BarChartWidget::STATS_AVG;
        } else {
            spdlog::error("Widget '{}': invalid stats_kind '{}'", name, stats_kind_str);
            return false;
        }
        const uint window_ms = window_s * 1000;
        if (num_buckets == 0 || window_ms == 0 || window_ms / num_buckets == 0) {
            spdlog::error("Widget '{}': invalid bar chart window/bucket configuration", name);
            return false;
        }
        return osd_.addWidget(std::move(params.id),
                              std::make_unique<BarChartWidget>(x, y, width, height, window_s, num_buckets, stats_kind),
                              std::move(params.matchers));
    }
    if (type == "ExternalSurfaceWidget") {
        return osd_.addWidget(std::move(params.id),
                              std::make_unique<ExternalSurfaceWidget>(x, y, name, refresh_frequency_ms),
                              std::move(params.matchers));
    }

    // -------------------------------------------------------------------------
    // Info widgets
    // -------------------------------------------------------------------------

    if (type == "GPSWidget") {
        return osd_.addWidget(
            std::move(params.id), std::make_unique<GPSWidget>(x, y, num_args), std::move(params.matchers));
    }
    if (type == "TimeWidget") {
        return osd_.addWidget(
            std::move(params.id), std::make_unique<TimeWidget>(x, y, num_args), std::move(params.matchers));
    }
    if (type == "PopupWidget") {
        const auto timeout_ms = widget.at("timeout_ms").get<uint>();
        if (timeout_ms == 0) {
            spdlog::error("Widget '{}': timeout_ms must be greater than zero", name);
            return false;
        }
        return osd_.addWidget(std::move(params.id),
                              std::make_unique<PopupWidget>(x, y, timeout_ms, num_args),
                              std::move(params.matchers));
    }
    if (type == "DebugWidget") {
        return osd_.addWidget(
            std::move(params.id), std::make_unique<DebugWidget>(x, y, num_args), std::move(params.matchers));
    }
    spdlog::error("Widget '{}': unknown type '{}'", name, type);
    return false;
}

std::vector<FactMatcher> OsdConfigLoader::parseFacts(const Json &facts) const {
    std::vector<FactMatcher> matchers;
    matchers.reserve(facts.size());
    for (const auto &fact : facts) {
        const auto name = fact.at("name").get<std::string>();
        FactTags tags;
        if (fact.contains("tags")) {
            for (const auto &[key, value] : fact.at("tags").items()) {
                tags.emplace(key, value.get<std::string>());
            }
        }
        matchers.emplace_back(name, std::move(tags));
    }
    return matchers;
}

bool OsdConfigLoader::loadLayouts(const Json &layouts) {
    for (const auto &layout_cfg : layouts) {
        const auto type = layout_cfg.at("type").get<std::string>();
        const auto x = layout_cfg.at("x").get<int>();
        const auto y = layout_cfg.at("y").get<int>();
        const auto spacing = layout_cfg.at("spacing").get<int>();

        std::unique_ptr<Layout> layout;
        if (type == "horizontal") {
            const auto direction = layout_cfg.at("direction").get<std::string>();
            HorizontalLayout::Direction dir;
            if (direction == "left-to-right") {
                dir = HorizontalLayout::Direction::LeftToRight;
            } else if (direction == "right-to-left") {
                dir = HorizontalLayout::Direction::RightToLeft;
            } else {
                spdlog::error("Unknown horizontal layout direction '{}'", direction);
                return false;
            }
            layout = std::make_unique<HorizontalLayout>(x, y, spacing, dir);
        } else {
            spdlog::error("Unknown layout type '{}'", type);
            return false;
        }
        const auto &widgets = layout_cfg.at("widgets");
        if (!widgets.is_array()) {
            spdlog::error("Layout 'widgets' must be an array");
            return false;
        }

        std::vector<std::string> widget_ids;
        widget_ids.reserve(widgets.size());
        for (const auto &widget : widgets) {
            widget_ids.push_back(widget.get<std::string>());
        }
        if (!osd_.addLayout(std::move(layout), widget_ids)) {
            return false;
        }
    }
    return true;
}