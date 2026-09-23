#include <fact.hpp>
#include <osd.hpp>

#include "config_loader.hpp"
#include "layout.hpp"
#include "widgets/base.hpp"

#include <cairo.h>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

Osd::Osd(uint refresh_frequency_ms, WidgetEnableMap widget_enable)
    : widget_enabled_(std::move(widget_enable)), refresh_frequency_ms_(refresh_frequency_ms) {}

Osd::~Osd() {
    if (screensaver_image_) {
        cairo_surface_destroy(screensaver_image_);
    }
}

bool Osd::loadConfig(const std::filesystem::path &path) {
    OsdConfigLoader loader(*this);
    return loader.load(path);
}

bool Osd::addWidget(std::string id, std::unique_ptr<Widget> widget, std::vector<FactMatcher> matchers) {
    if (id.empty()) {
        spdlog::error("Widget id must not be empty");
        return false;
    }
    if (findWidget(id)) {
        spdlog::error("Duplicate widget id '{}'", id);
        return false;
    }
    if (!isWidgetEnabled(id)) {
        widget_entries_.push_back({std::move(id), nullptr, {}, {}});
        return true;
    }
    widget_entries_.push_back({std::move(id), std::move(widget), std::move(matchers), {}});
    return true;
}

bool Osd::addLayout(std::unique_ptr<Layout> layout, const std::vector<std::string> &widget_ids) {
    std::vector<WidgetEntry *> entries;
    entries.reserve(widget_ids.size());
    for (const auto &id : widget_ids) {
        WidgetEntry *entry = findWidget(id);
        if (!entry) {
            spdlog::error("Layout references unknown widget '{}'", id);
            return false;
        }
        if (!entry->widget) {
            continue;
        }
        entries.push_back(entry);
    }

    Layout *layout_ptr = layout.get();
    for (WidgetEntry *entry : entries) {
        layout_ptr->addWidget(entry->widget.get());
        entry->layouts.push_back(layout_ptr);
    }
    layouts_.push_back(std::move(layout));
    return true;
}

Osd::WidgetEntry *Osd::findWidget(const std::string &id) {
    for (auto &entry : widget_entries_) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

bool Osd::isWidgetEnabled(const std::string &id) const {
    const auto it = widget_enabled_.find(id);
    if (it == widget_enabled_.end()) {
        return false;
    }
    return it->second;
}

void Osd::measureWidgets(cairo_t *cr) {
    for (const auto &entry : widget_entries_) {
        if (!entry.widget || !entry.widget->measureDirty()) {
            continue;
        }
        const int old_width = entry.widget->width();
        const int old_height = entry.widget->height();

        entry.widget->measure(cr);
        if (old_width == entry.widget->width() && old_height == entry.widget->height()) {
            continue;
        }
        for (Layout *layout : entry.layouts) {
            layout->invalidate();
        }
    }
}

void Osd::draw(cairo_t *cr) {
    cairo_select_font_face(cr, "Roboto", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 20);

    measureWidgets(cr);
    for (const auto &layout : layouts_) {
        layout->update(cr);
    }
    for (const auto &entry : widget_entries_) {
        if (entry.widget) {
            entry.widget->draw(cr);
        }
    }
}

void Osd::setFact(Fact fact) {
    for (const auto &entry : widget_entries_) {
        if (!entry.widget) {
            continue;
        }
        uint arg_idx = 0;
        for (const auto &matcher : entry.matchers) {
            if (fact.matches(matcher)) {
                entry.widget->setFact(arg_idx, fact);
            }
            ++arg_idx;
        }
    }
}

void Osd::loadScreensaverImage(const std::filesystem::path &path) {
    cairo_surface_t *image = cairo_image_surface_create_from_png(path.c_str());

    const cairo_status_t status = cairo_surface_status(image);
    if (status != CAIRO_STATUS_SUCCESS) {
        spdlog::error("Can't open screensaver image '{}': {}", path.string(), cairo_status_to_string(status));
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
            spdlog::error(
                "Screensaver image {} x {} larger than screen {} x {}", image_width, image_height, width, height);

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
