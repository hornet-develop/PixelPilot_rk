#include "osd_service.hpp"

extern "C" {
#include "../drm.h"
}

#include <cassert>
#include <chrono>
#include <utility>

#include <cairo.h>
#include <spdlog/spdlog.h>

// -----------------------------------------------------------------------------
// Display integration
// -----------------------------------------------------------------------------

extern pthread_mutex_t video_mutex;
extern pthread_cond_t video_cond;
extern std::atomic<bool> video_present;

extern pthread_mutex_t osd_mutex;
extern bool osd_update_ready;

// -----------------------------------------------------------------------------
// OsdService
// -----------------------------------------------------------------------------

std::unique_ptr<OsdService> OsdService::instance_;

OsdService::OsdService(OsdServiceParams params)
    : params_(std::move(params)), osd_(params_.refresh_frequency_ms, params_.widget_enabled) {}

bool OsdService::start(OsdServiceParams params) {
    if (instance_) {
        spdlog::error("OSD service is already running");
        return false;
    }

    instance_ = std::unique_ptr<OsdService>(new OsdService(std::move(params)));
    if (!instance_->init()) {
        instance_.reset();
        return false;
    }

    const int ret = pthread_create(&instance_->thread_, nullptr, &OsdService::threadEntry, instance_.get());
    if (ret != 0) {
        spdlog::error("Failed to start OSD thread: {}", ret);
        instance_.reset();
        return false;
    }
    return true;
}

bool OsdService::init() {
    if (params_.screensaver_enabled) {
        osd_.loadScreensaverImage(params_.screensaver_image);
    }
    if (params_.enabled && !params_.config_path.empty()) {
        osd_.loadConfig(params_.config_path);
    }

    modeset_buf *buf = &params_.out->osd_bufs[params_.out->osd_buf_switch];
    const int ret = modeset_perform_modeset(params_.fd,
                                            params_.out,
                                            params_.out->osd_request,
                                            &params_.out->osd_plane,
                                            buf->fb,
                                            buf->width,
                                            buf->height,
                                            params_.zpos,
                                            false);
    if (ret < 0) {
        spdlog::error("Failed to initialize OSD plane");
        return false;
    }
    return true;
}

void OsdService::stop() {
    if (!instance_)
        return;

    instance_->stop_.store(true);
    instance_->fact_cv_.notify_one();

    const int ret = pthread_join(instance_->thread_, nullptr);
    if (ret != 0) {
        spdlog::error("Failed to join OSD thread: {}", ret);
        return;
    }
    instance_.reset();
}

void OsdService::publishFact(Fact fact) {
    if (!instance_ || !instance_->params_.enabled)
        return;
    instance_->enqueueFact(std::move(fact));
}

void OsdService::publishFacts(std::vector<Fact> facts) {
    if (!instance_ || !instance_->params_.enabled)
        return;
    instance_->enqueueFacts(std::move(facts));
}

void *OsdService::threadEntry(void *arg) {
    auto *service = static_cast<OsdService *>(arg);
    service->run();
    return nullptr;
}

void OsdService::enqueueFact(Fact fact) {
    {
        std::lock_guard<std::mutex> lock(fact_mutex_);
        fact_queue_.push(std::move(fact));
    }
    fact_cv_.notify_one();
}

void OsdService::enqueueFacts(std::vector<Fact> facts) {
    {
        std::lock_guard<std::mutex> lock(fact_mutex_);
        for (Fact &fact : facts)
            fact_queue_.push(std::move(fact));
    }
    fact_cv_.notify_one();
}

void OsdService::drainFacts() {
    std::queue<Fact> facts;
    {
        std::lock_guard<std::mutex> lock(fact_mutex_);
        facts.swap(fact_queue_);
    }
    while (!facts.empty()) {
        Fact fact = std::move(facts.front());
        facts.pop();

        SPDLOG_DEBUG("got fact {}({})", fact.getName(), fact.getTags());
        osd_.setFact(std::move(fact));
    }
}

void OsdService::paintBuffer(modeset_buf *buf) {
    cairo_surface_t *surface =
        cairo_image_surface_create_for_data(buf->map, CAIRO_FORMAT_ARGB32, buf->width, buf->height, buf->stride);
    cairo_t *cr = cairo_create(surface);

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);

    if (!video_present.load()) {
        osd_.drawScreensaver(cr);
    }

    if (params_.enabled) {
        osd_.draw(cr);
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

void OsdService::refresh() {
    const int buf_idx = params_.out->osd_buf_switch ^ 1;
    modeset_buf *buf = &params_.out->osd_bufs[buf_idx];

    paintBuffer(buf);

    int ret = pthread_mutex_lock(&osd_mutex);
    assert(!ret);

    params_.out->osd_buf_switch = buf_idx;
    ret = pthread_mutex_unlock(&osd_mutex);
    assert(!ret);

    ret = pthread_mutex_lock(&video_mutex);
    assert(!ret);

    osd_update_ready = true;
    ret = pthread_cond_signal(&video_cond);
    assert(!ret);

    ret = pthread_mutex_unlock(&video_mutex);
    assert(!ret);
}

void OsdService::run() {
    pthread_setname_np(pthread_self(), "__OSD");

    auto last_display_at = std::chrono::steady_clock::now();

    while (!stop_.load()) {
        const auto refresh_period =
            params_.enabled ? std::chrono::milliseconds(params_.refresh_frequency_ms) : std::chrono::seconds(1);
        const auto next_display_at = last_display_at + refresh_period;

        std::unique_lock<std::mutex> lock(fact_mutex_);
        fact_cv_.wait_until(
            lock, next_display_at, [this] { return stop_.load() || (params_.enabled && !fact_queue_.empty()); });

        if (stop_.load()) {
            break;
        }

        const bool have_facts = params_.enabled && !fact_queue_.empty();
        lock.unlock();
        if (have_facts) {
            drainFacts();
        }
        if (std::chrono::steady_clock::now() < next_display_at) {
            continue;
        }

        SPDLOG_DEBUG("refresh OSD");

        refresh();
        last_display_at = std::chrono::steady_clock::now();
    }
    spdlog::info("OSD thread done.");
}
