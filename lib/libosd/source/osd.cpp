#include <fact.hpp>
#include <osd.hpp>

#include "layout.hpp"
#include "widgets/graphics.hpp"
#include "widgets/icon.hpp"
#include "widgets/info.hpp"
#include "widgets/status.hpp"
#include "widgets/text.hpp"
#include "widgets/video.hpp"

#include <cairo.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace {

std::vector<FactMatcher> parseMatchers(const json &facts_json) {
    std::vector<FactMatcher> matchers;
    matchers.reserve(facts_json.size());
    for (const auto &matcher_json : facts_json) {
        const auto name = matcher_json.at("name").get<std::string>();
        FactTags tags;
        if (matcher_json.contains("tags")) {
            for (const auto &[key, value] : matcher_json.at("tags").items()) {
                tags.emplace(key, value.get<std::string>());
            }
        }
        matchers.emplace_back(name, std::move(tags));
    }
    return matchers;
}

cairo_surface_t *openIcon(const std::string &widget_name, const std::filesystem::path &base_path,
                          std::filesystem::path icon_path) {
    if (icon_path.is_relative()) {
        icon_path = base_path / icon_path;
    }
    cairo_surface_t *icon = cairo_image_surface_create_from_png(icon_path.c_str());
    const cairo_status_t status = cairo_surface_status(icon);
    if (status != CAIRO_STATUS_SUCCESS) {
        spdlog::error("Widget '{}': Can't open icon '{}': {}", widget_name, icon_path.string(),
                      cairo_status_to_string(status));
        cairo_surface_destroy(icon);
        return nullptr;
    }
    return icon;
}

} // namespace

Osd::Osd(uint refresh_frequency_ms) : refresh_frequency_ms_(refresh_frequency_ms) {}

Osd::~Osd() {
    if (screensaver_image_) {
        cairo_surface_destroy(screensaver_image_);
    }
}

bool Osd::loadConfig(const std::filesystem::path &path) {
    std::ifstream file(path);
    if (!file) {
        spdlog::error("Failed to open OSD config '{}'", path.string());
        return false;
    }

    try {
        const json cfg = json::parse(file);
        return loadConfigJson(cfg);
    } catch (const json::parse_error &e) {
        spdlog::error("Failed to parse OSD config '{}': {}", path.string(), e.what());
        return false;
    } catch (const json::exception &e) {
        spdlog::error("Invalid OSD config '{}': {}", path.string(), e.what());
        return false;
    }
}

bool Osd::loadConfigJson(const json &cfg) {
    if (!cfg.contains("format")) {
        spdlog::error("OSD config doesn't have 'format' key");
        return false;
    }
    if (!cfg.contains("widgets") || !cfg.at("widgets").is_array()) {
        spdlog::error("OSD config doesn't have valid 'widgets' array");
        return false;
    }

    const std::filesystem::path assets_dir = cfg.value("assets_dir", std::string{"."});
    if (!loadWidgets(cfg.at("widgets"), assets_dir)) {
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

bool Osd::loadWidgets(const json &widgets_json, const std::filesystem::path &assets_dir) {
    for (const auto &widget_json : widgets_json) {
        const auto name = widget_json.at("name").get<std::string>();
        const auto type = widget_json.at("type").get<std::string>();
        const auto x = widget_json.at("x").get<int>();
        const auto y = widget_json.at("y").get<int>();

        const auto id = widget_json.value("id", std::string{});
        if (!id.empty() && widgets_by_id_.find(id) != widgets_by_id_.end()) {
            spdlog::error("Duplicate widget id '{}'", id);
            return false;
        }
        const auto &facts_json = widget_json.at("facts");
        if (!facts_json.is_array()) {
            spdlog::error("Widget '{}': 'facts' must be an array", name);
            return false;
        }
        auto matchers = parseMatchers(facts_json);
        auto widget = createWidget(widget_json, assets_dir, name, type, x, y, static_cast<uint>(matchers.size()));
        if (!widget) {
            return false;
        }
        addWidget(std::move(widget), std::move(matchers), id);
    }
    return true;
}

std::unique_ptr<Widget> Osd::createWidget(const json &cfg, const std::filesystem::path &assets_dir,
                                          const std::string &name, const std::string &type, int x, int y,
                                          uint num_args) {
    // -------------------------------------------------------------------------
    // Text widgets
    // -------------------------------------------------------------------------

    if (type == "TextWidget") {
        const auto text = cfg.at("text").get<std::string>();
        return std::make_unique<TextWidget>(x, y, text);
    }
    if (type == "TplTextWidget") {
        const auto tpl = cfg.at("template").get<std::string>();
        return std::make_unique<TplTextWidget>(x, y, tpl, num_args);
    }
    if (type == "IconTplTextWidget") {
        const auto tpl = cfg.at("template").get<std::string>();
        const auto icon_path = cfg.at("icon_path").get<std::string>();
        cairo_surface_t *icon = openIcon(name, assets_dir, icon_path);
        if (!icon) {
            return nullptr;
        }
        return std::make_unique<IconTplTextWidget>(x, y, icon, tpl, num_args);
    }

    // -------------------------------------------------------------------------
    // Icon widgets
    // -------------------------------------------------------------------------

    if (type == "IconSelectorWidget") {
        std::vector<std::pair<std::pair<int, int>, std::filesystem::path>> ranges_and_icons;
        for (const auto &range_icon : cfg.at("ranges_and_icons")) {
            const auto &range = range_icon.at("range");
            const int range_start = range.at(0).get<int>();
            const int range_end = range.at(1).get<int>();
            const auto icon_path = range_icon.at("icon_path").get<std::string>();
            ranges_and_icons.push_back({{range_start, range_end}, icon_path});
        }
        return std::make_unique<IconSelectorWidget>(x, y, ranges_and_icons, assets_dir);
    }

    // -------------------------------------------------------------------------
    // Status widgets
    // -------------------------------------------------------------------------

    if (type == "IconStatusWidget") {
        const auto icon_path = cfg.at("icon_path").get<std::string>();
        cairo_surface_t *icon = openIcon(name, assets_dir, icon_path);
        if (!icon) {
            return nullptr;
        }
        return std::make_unique<IconStatusWidget>(x, y, icon);
    }
    if (type == "IconTplStatusWidget") {
        const auto tpl = cfg.at("template").get<std::string>();
        const auto icon_path = cfg.at("icon_path").get<std::string>();
        cairo_surface_t *icon = openIcon(name, assets_dir, icon_path);
        if (!icon) {
            return nullptr;
        }
        return std::make_unique<IconTplStatusWidget>(x, y, icon, tpl, num_args);
    }
    if (type == "DvrStatusWidget") {
        const auto icon_path = cfg.at("icon_path").get<std::string>();
        cairo_surface_t *icon = openIcon(name, assets_dir, icon_path);
        if (!icon) {
            return nullptr;
        }
        return std::make_unique<DvrStatusWidget>(x, y, icon);
    }
    if (type == "DvrStorageWidget") {
        const auto icon_path = cfg.at("icon_path").get<std::string>();
        cairo_surface_t *icon = openIcon(name, assets_dir, icon_path);
        if (!icon) {
            return nullptr;
        }
        return std::make_unique<DvrStorageWidget>(x, y, icon);
    }

    // -------------------------------------------------------------------------
    // Video widgets
    // -------------------------------------------------------------------------

    if (type == "VideoWidget") {
        const auto tpl = cfg.at("template").get<std::string>();
        const auto icon_path = cfg.at("icon_path").get<std::string>();
        const uint window_size_ms = cfg.at("per_second_window_s").get<uint>() * 1000;
        const uint bucket_size_ms = cfg.at("per_second_bucket_ms").get<uint>();
        const uint refresh_rate_ms = cfg.value("refresh_rate_ms", refresh_frequency_ms_);
        cairo_surface_t *icon = openIcon(name, assets_dir, icon_path);
        if (!icon) {
            return nullptr;
        }
        return std::make_unique<VideoWidget>(x, y, window_size_ms, bucket_size_ms, icon, tpl, refresh_rate_ms, num_args,
                                             refresh_frequency_ms_);
    }
    if (type == "VideoBitrateWidget") {
        const auto tpl = cfg.at("template").get<std::string>();
        const auto icon_path = cfg.at("icon_path").get<std::string>();
        const uint window_size_ms = cfg.at("per_second_window_s").get<uint>() * 1000;
        const uint bucket_size_ms = cfg.at("per_second_bucket_ms").get<uint>();
        const uint refresh_rate_ms = cfg.value("refresh_rate_ms", refresh_frequency_ms_);
        cairo_surface_t *icon = openIcon(name, assets_dir, icon_path);
        if (!icon) {
            return nullptr;
        }
        return std::make_unique<VideoBitrateWidget>(x, y, window_size_ms, bucket_size_ms, icon, tpl, refresh_rate_ms,
                                                    num_args, refresh_frequency_ms_);
    }
    if (type == "VideoDecodeLatencyWidget") {
        const auto tpl = cfg.at("template").get<std::string>();
        const auto icon_path = cfg.at("icon_path").get<std::string>();
        const uint window_size_ms = cfg.at("per_second_window_s").get<uint>() * 1000;
        const uint bucket_size_ms = cfg.at("per_second_bucket_ms").get<uint>();
        const uint refresh_rate_ms = cfg.value("refresh_rate_ms", refresh_frequency_ms_);
        cairo_surface_t *icon = openIcon(name, assets_dir, icon_path);
        if (!icon) {
            return nullptr;
        }
        return std::make_unique<VideoDecodeLatencyWidget>(x, y, window_size_ms, bucket_size_ms, icon, tpl,
                                                          refresh_rate_ms, num_args, refresh_frequency_ms_);
    }

    // -------------------------------------------------------------------------
    // Graphics widgets
    // -------------------------------------------------------------------------

    if (type == "BoxWidget") {
        const auto width = cfg.at("width").get<uint>();
        const auto height = cfg.at("height").get<uint>();
        const auto &color = cfg.at("color");
        const CairoColor cairo_color{
            color.at("r").get<double>(),
            color.at("g").get<double>(),
            color.at("b").get<double>(),
            color.at("alpha").get<double>(),
        };
        return std::make_unique<BoxWidget>(x, y, width, height, cairo_color);
    }
    if (type == "BarChartWidget") {
        const auto width = cfg.at("width").get<uint>();
        const auto height = cfg.at("height").get<uint>();
        const auto window_s = cfg.at("window_s").get<uint>();
        const auto num_buckets = cfg.at("num_buckets").get<uint>();
        const auto stats_kind_str = cfg.at("stats_kind").get<std::string>();
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
            return nullptr;
        }
        const uint window_ms = window_s * 1000;
        if (num_buckets == 0 || window_ms == 0 || window_ms / num_buckets == 0) {
            spdlog::error("Widget '{}': invalid bar chart window/bucket configuration", name);
            return nullptr;
        }
        return std::make_unique<BarChartWidget>(x, y, width, height, window_s, num_buckets, stats_kind);
    }
    if (type == "ExternalSurfaceWidget") {
        return std::make_unique<ExternalSurfaceWidget>(x, y, name, refresh_frequency_ms_);
    }

    // -------------------------------------------------------------------------
    // Info widgets
    // -------------------------------------------------------------------------

    if (type == "GPSWidget") {
        return std::make_unique<GPSWidget>(x, y, num_args);
    }
    if (type == "TimeWidget") {
        return std::make_unique<TimeWidget>(x, y, num_args);
    }
    if (type == "PopupWidget") {
        const auto timeout_ms = cfg.at("timeout_ms").get<uint>();
        if (timeout_ms == 0) {
            spdlog::error("Widget '{}': timeout_ms must be greater than zero", name);
            return nullptr;
        }
        return std::make_unique<PopupWidget>(x, y, timeout_ms, num_args);
    }
    if (type == "DebugWidget") {
        return std::make_unique<DebugWidget>(x, y, num_args);
    }

    spdlog::error("Widget '{}': unknown type '{}'", name, type);
    return nullptr;
}

bool Osd::loadLayouts(const json &layouts_json) {
    for (const auto &cfg : layouts_json) {
        const auto type = cfg.at("type").get<std::string>();
        const auto x = cfg.at("x").get<int>();
        const auto y = cfg.at("y").get<int>();
        const auto spacing = cfg.at("spacing").get<int>();

        std::unique_ptr<Layout> layout;
        if (type == "horizontal") {
            const auto direction = cfg.at("direction").get<std::string>();
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
        const auto &widget_ids = cfg.at("widgets");
        if (!widget_ids.is_array()) {
            spdlog::error("Layout 'widgets' must be an array");
            return false;
        }

        std::vector<Widget *> layout_widgets;
        layout_widgets.reserve(widget_ids.size());

        for (const auto &widget_id_json : widget_ids) {
            const auto widget_id = widget_id_json.get<std::string>();
            auto it = widgets_by_id_.find(widget_id);
            if (it == widgets_by_id_.end()) {
                spdlog::error("Layout references unknown widget '{}'", widget_id);
                return false;
            }
            layout_widgets.push_back(it->second);
        }
        Layout *layout_ptr = layout.get();
        for (Widget *widget : layout_widgets) {
            addWidgetToLayout(layout_ptr, widget);
        }
        layouts_.push_back(std::move(layout));
    }
    return true;
}

void Osd::addWidget(std::unique_ptr<Widget> widget, std::vector<FactMatcher> param_matchers, const std::string &id) {
    Widget *widget_ptr = widget.get();
    widgets_.push_back(std::move(widget));

    uint arg_idx = 0;
    for (auto &matcher : param_matchers) {
        matchers_.emplace_back(std::move(matcher), widget_ptr, arg_idx++);
    }
    if (!id.empty()) {
        widgets_by_id_[id] = widget_ptr;
    }
}

void Osd::addWidgetToLayout(Layout *layout, Widget *widget) {
    layout->addWidget(widget);
    widget_layouts_[widget].push_back(layout);
}

void Osd::measureWidgets(cairo_t *cr) {
    for (auto &widget : widgets_) {
        if (!widget->measureDirty()) {
            continue;
        }
        const int old_width = widget->width();
        const int old_height = widget->height();

        widget->measure(cr);
        if (old_width == widget->width() && old_height == widget->height()) {
            continue;
        }
        auto it = widget_layouts_.find(widget.get());
        if (it == widget_layouts_.end()) {
            continue;
        }
        for (Layout *layout : it->second) {
            layout->invalidate();
        }
    }
}

void Osd::draw(cairo_t *cr) {
    cairo_select_font_face(cr, "Roboto", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 20);

    measureWidgets(cr);
    for (auto &layout : layouts_) {
        layout->update(cr);
    }
    for (auto &widget : widgets_) {
        widget->draw(cr);
    }
}

void Osd::setFact(Fact fact) {
    for (const auto &[matcher, widget, arg_idx] : matchers_) {
        if (fact.matches(matcher)) {
            widget->setFact(arg_idx, fact);
        }
    }
}

void Osd::loadScreensaverImage(const std::string &path) {
    cairo_surface_t *image = cairo_image_surface_create_from_png(path.c_str());

    const cairo_status_t status = cairo_surface_status(image);
    if (status != CAIRO_STATUS_SUCCESS) {
        spdlog::error("Can't open screensaver image '{}': {}", path, cairo_status_to_string(status));
        cairo_surface_destroy(image);
        return;
    }
    if (screensaver_image_) {
        cairo_surface_destroy(screensaver_image_);
    }
    screensaver_image_ = image;
}

void Osd::drawScreensaver(cairo_t *cr) {
    cairo_surface_t *target = cairo_get_target(cr);
    const int width = cairo_image_surface_get_width(target);
    const int height = cairo_image_surface_get_height(target);

    cairo_save(cr);

    // Black background
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    cairo_paint(cr);

    if (screensaver_image_) {
        const int image_width = cairo_image_surface_get_width(screensaver_image_);
        const int image_height = cairo_image_surface_get_height(screensaver_image_);

        if (image_width > width || image_height > height) {
            spdlog::error("Screensaver image {} x {} larger than screen {} x {}", image_width, image_height, width,
                          height);

            cairo_restore(cr);
            return;
        }
        const int x = (width - image_width) / 2;
        const int y = (height - image_height) / 2;

        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_set_source_surface(cr, screensaver_image_, x, y);
        cairo_rectangle(cr, x, y, image_width, image_height);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}
