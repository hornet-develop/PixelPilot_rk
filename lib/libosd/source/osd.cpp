#include <osd/fact.hpp>
#include <osd/osd.hpp>

#include "layout.hpp"
#include "widgets/components.hpp"

#include <cairo.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

using json = nlohmann::json;

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

    nlohmann::json cfg;
    try {
        cfg = nlohmann::json::parse(file);
    } catch (const nlohmann::json::exception &e) {
        spdlog::error("Failed to parse OSD config '{}': {}", path.string(), e.what());
        return false;
    }

    try {
        return loadConfigJson(cfg);
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

    std::filesystem::path assets_dir{"."};
    if (cfg.contains("assets_dir")) {
        assets_dir = cfg.at("assets_dir").get<std::string>();
    }

    const auto &widgets_json = cfg.at("widgets");
    for (const auto &widget_json : widgets_json) {
        if (!widget_json.contains("name") || !widget_json.contains("type") || !widget_json.contains("x") ||
            !widget_json.contains("y") || !widget_json.contains("facts")) {
            spdlog::error("Missing required key name/type/x/y/facts");
            return false;
        }
        auto name = widget_json.at("name").get<std::string>();
        auto type = widget_json.at("type").get<std::string>();
        auto x = widget_json.at("x").get<int>();
        auto y = widget_json.at("y").get<int>();
        std::vector<FactMatcher> matchers;
        for (const auto &matcher_json : widget_json.at("facts")) {
            auto matcher_name = matcher_json.at("name").get<std::string>();
            FactTags tags;
            if (matcher_json.contains("tags")) {
                for (const auto &[key, value] : matcher_json.at("tags").items()) {
                    tags.emplace(key, value.get<std::string>());
                }
            }
            matchers.push_back(FactMatcher(matcher_name, tags));
        }
        if (type == "TextWidget") {
            addWidget(std::make_unique<TextWidget>(x, y, widget_json.at("text").get<std::string>()), matchers);
        } else if (type == "ExternalSurfaceWidget") {
            addWidget(std::make_unique<ExternalSurfaceWidget>(x, y, name, refresh_frequency_ms_), matchers);
        } else if (type == "IconSelectorWidget") {
            std::vector<std::pair<std::pair<int, int>, std::filesystem::path>> ranges_and_icons;
            for (const auto &range_icon : widget_json.at("ranges_and_icons")) {
                const int range_start = range_icon.at("range")[0].get<int>();
                const int range_end = range_icon.at("range")[1].get<int>();
                const std::filesystem::path icon_path = range_icon.at("icon_path").get<std::string>();
                ranges_and_icons.push_back({{range_start, range_end}, icon_path});
            }
            addWidget(std::make_unique<IconSelectorWidget>(x, y, ranges_and_icons, assets_dir), matchers);
        } else if (type == "TplTextWidget") {
            auto tpl = widget_json.at("template").get<std::string>();
            addWidget(std::make_unique<TplTextWidget>(x, y, tpl, (uint)matchers.size()), matchers);
        } else if (type == "IconTplTextWidget") {
            auto tpl = widget_json.at("template").get<std::string>();
            auto icon_path = widget_json.at("icon_path").get<std::string>();
            cairo_surface_t *icon = openIcon(name, assets_dir, icon_path);
            if (!icon)
                break;
            addWidget(std::make_unique<IconTplTextWidget>(x, y, icon, tpl, (uint)matchers.size()), matchers);
        } else if (type == "DvrStatusWidget") {
            auto id = widget_json.at("id").get<std::string>();
            auto text = widget_json.at("text").get<std::string>();
            auto icon_path = widget_json.at("icon_path").get<std::string>();
            cairo_surface_t *icon = openIcon(name, assets_dir, icon_path);
            if (!icon)
                break;
            addWidget(std::make_unique<DvrStatusWidget>(x, y, icon, text), matchers, id);
        } else if (type == "DvrStorageWidget") {
            auto id = widget_json.at("id").get<std::string>();
            auto icon_path = widget_json.at("icon_path").get<std::string>();
            cairo_surface_t *icon = openIcon(name, assets_dir, icon_path);
            if (!icon)
                break;
            addWidget(std::make_unique<DvrStorageWidget>(x, y, icon), matchers, id);
        } else if (type == "IconStatusWidget") {
            auto icon_path = widget_json.at("icon_path").get<std::string>();
            cairo_surface_t *icon = openIcon(name, assets_dir, icon_path);
            if (!icon)
                break;
            addWidget(std::make_unique<IconStatusWidget>(x, y, icon), matchers);
        } else if (type == "IconTplStatusWidget") {
            auto tpl = widget_json.at("template").get<std::string>();
            auto icon_path = widget_json.at("icon_path").get<std::string>();
            cairo_surface_t *icon = openIcon(name, assets_dir, icon_path);
            if (!icon)
                break;
            addWidget(std::make_unique<IconTplStatusWidget>(x, y, icon, tpl, (uint)matchers.size()), matchers);
        } else if (type == "VideoWidget") {
            auto tpl = widget_json.at("template").get<std::string>();
            auto icon_path = widget_json.at("icon_path").get<std::string>();
            uint window_size_s = widget_json.at("per_second_window_s").get<uint>();
            uint bucket_size_ms = widget_json.at("per_second_bucket_ms").get<uint>();
            uint refresh_rate_ms = widget_json.value("refresh_rate_ms", refresh_frequency_ms_);
            cairo_surface_t *icon = openIcon(name, assets_dir, icon_path);
            if (!icon)
                break;
            addWidget(std::make_unique<VideoWidget>(x, y, window_size_s * 1000, bucket_size_ms, icon, tpl,
                                                    refresh_rate_ms, (uint)matchers.size(), refresh_frequency_ms_),
                      matchers);
        } else if (type == "VideoBitrateWidget") {
            auto tpl = widget_json.at("template").get<std::string>();
            auto icon_path = widget_json.at("icon_path").get<std::string>();
            uint window_size_s = widget_json.at("per_second_window_s").get<uint>();
            uint bucket_size_ms = widget_json.at("per_second_bucket_ms").get<uint>();
            uint refresh_rate_ms = widget_json.value("refresh_rate_ms", refresh_frequency_ms_);
            cairo_surface_t *icon = openIcon(name, assets_dir, icon_path);
            if (!icon)
                break;
            addWidget(std::make_unique<VideoBitrateWidget>(x, y, window_size_s * 1000, bucket_size_ms, icon, tpl,
                                                           refresh_rate_ms, (uint)matchers.size(),
                                                           refresh_frequency_ms_),
                      matchers);
        } else if (type == "VideoDecodeLatencyWidget") {
            auto tpl = widget_json.at("template").get<std::string>();
            auto icon_path = widget_json.at("icon_path").get<std::string>();
            uint window_size_s = widget_json.at("per_second_window_s").get<uint>();
            uint bucket_size_ms = widget_json.at("per_second_bucket_ms").get<uint>();
            uint refresh_rate_ms = widget_json.value("refresh_rate_ms", refresh_frequency_ms_);
            cairo_surface_t *icon = openIcon(name, assets_dir, icon_path);
            if (!icon)
                break;
            addWidget(std::make_unique<VideoDecodeLatencyWidget>(x, y, window_size_s * 1000, bucket_size_ms, icon, tpl,
                                                                 refresh_rate_ms, 1, refresh_frequency_ms_),
                      matchers);
        } else if (type == "BoxWidget") {
            auto width = widget_json.at("width").get<uint>();
            auto height = widget_json.at("height").get<uint>();
            json color_j = widget_json.at("color");
            auto r = color_j.at("r").get<double>();
            auto g = color_j.at("g").get<double>();
            auto b = color_j.at("b").get<double>();
            auto a = color_j.at("alpha").get<double>();
            addWidget(std::make_unique<BoxWidget>(x, y, width, height, CairoColor{r, g, b, a}), matchers);
        } else if (type == "BarChartWidget") {
            auto width = widget_json.at("width").get<uint>();
            auto height = widget_json.at("height").get<uint>();
            auto window_s = widget_json.at("window_s").get<uint>();
            auto num_buckets = widget_json.at("num_buckets").get<uint>();
            auto stats_kind_str = widget_json.at("stats_kind").get<std::string>();
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
                SPDLOG_WARN("{}: invalid stats_kind {}", name, stats_kind_str);
                break;
            }
            addWidget(std::make_unique<BarChartWidget>(x, y, width, height, window_s, num_buckets, stats_kind),
                      matchers);
        } else if (type == "GPSWidget") {
            addWidget(std::make_unique<GPSWidget>(x, y, (uint)matchers.size()), matchers);
        } else if (type == "TimeWidget") {
            addWidget(std::make_unique<TimeWidget>(x, y, (uint)matchers.size()), matchers);
        } else if (type == "PopupWidget") {
            auto timeout_ms = widget_json.at("timeout_ms").get<uint>();
            addWidget(std::make_unique<PopupWidget>(x, y, timeout_ms, (uint)matchers.size()), matchers);
        } else if (type == "DebugWidget") {
            addWidget(std::make_unique<DebugWidget>(x, y, (uint)matchers.size()), matchers);
        } else {
            spdlog::warn("Widget '{}': unknown type: {}", name, type);
        }
    }
    if (cfg.contains("layouts")) {
        const auto &layouts_json = cfg.at("layouts");
        for (const auto &layout_json : layouts_json) {
            if (!layout_json.contains("type") || !layout_json.contains("x") || !layout_json.contains("y") ||
                !layout_json.contains("spacing")) {
                spdlog::error("Invalid layout configuration");
                return false;
            }
            const auto type = layout_json.at("type").get<std::string>();
            const auto x = layout_json.at("x").get<int>();
            const auto y = layout_json.at("y").get<int>();
            const auto spacing = layout_json.at("spacing").get<int>();

            std::unique_ptr<Layout> layout;
            if (type == "horizontal") {
                if (!layout_json.contains("direction")) {
                    spdlog::error("Horizontal layout has no direction");
                    return false;
                }
                const auto direction = layout_json.at("direction").get<std::string>();
                HorizontalLayout::Direction dir;
                if (direction == "left-to-right") {
                    dir = HorizontalLayout::Direction::LeftToRight;
                } else if (direction == "right-to-left") {
                    dir = HorizontalLayout::Direction::RightToLeft;
                } else {
                    spdlog::error("Unknown horizontal layout direction '{}'", direction);
                    continue;
                }
                layout = std::make_unique<HorizontalLayout>(x, y, spacing, dir);
            } else {
                spdlog::error("Unknown layout type '{}'", type);
                continue;
            }
            if (!layout_json.contains("widgets")) {
                spdlog::error("Layout has no widgets");
                return false;
            }
            Layout *layout_ptr = layout.get();
            const auto &widget_ids_json = layout_json.at("widgets");
            for (const auto &widget_id_json : widget_ids_json) {
                const auto widget_id = widget_id_json.get<std::string>();
                auto it = widgets_by_id.find(widget_id);
                if (it == widgets_by_id.end()) {
                    spdlog::error("Layout references unknown widget '{}'", widget_id);
                    continue;
                }
                addWidgetToLayout(layout_ptr, it->second);
            }
            layouts.push_back(std::move(layout));
        }
    }
    return true;
}

Osd *Osd::addWidget(std::unique_ptr<Widget> widget, std::vector<FactMatcher> param_matchers, const std::string &id) {
    Widget *widget_ptr = widget.get();
    widgets.push_back(std::move(widget));

    uint arg_idx = 0;
    for (const auto &matcher : param_matchers) {
        matchers.emplace_back(matcher, widget_ptr, arg_idx++);
    }
    if (!id.empty()) {
        if (widgets_by_id.find(id) != widgets_by_id.end()) {
            spdlog::error("Duplicate widget id '{}'", id);
        } else {
            widgets_by_id[id] = widget_ptr;
        }
    }
    return this;
}

void Osd::addWidgetToLayout(Layout *layout, Widget *widget) {
    layout->addWidget(widget);
    widget_layouts[widget].push_back(layout);
}

void Osd::measureWidgets(cairo_t *cr) {
    for (auto &widget : widgets) {
        if (!widget->measureDirty()) {
            continue;
        }
        const int old_width = widget->width();
        const int old_height = widget->height();

        widget->measure(cr);
        if (old_width == widget->width() && old_height == widget->height()) {
            continue;
        }
        auto it = widget_layouts.find(widget.get());
        if (it == widget_layouts.end()) {
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
    for (auto &layout : layouts) {
        layout->update(cr);
    }
    for (auto &widget : widgets)
        widget->draw(cr);
}

void Osd::setFact(Fact fact) {
    for (const auto &[matcher, widget, arg_idx] : matchers) {
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

cairo_surface_t *Osd::openIcon(const std::string &widget_name, const std::filesystem::path &base_path,
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
