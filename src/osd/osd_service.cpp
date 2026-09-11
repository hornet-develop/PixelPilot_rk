#include "osd_service.hpp"

extern "C" {
#include "../drm.h"
}

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <utility>

#include <unistd.h>

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
    : params_(std::move(params)), osd_(params_.refresh_frequency_ms) {}

bool OsdService::start(OsdServiceParams params) {
    if (instance_) {
        spdlog::error("OSD service is already running");
        return false;
    }

    instance_ = std::unique_ptr<OsdService>(new OsdService(std::move(params)));
    const int ret = pthread_create(&instance_->thread_, nullptr, &OsdService::threadEntry, instance_.get());

    if (ret != 0) {
        spdlog::error("Failed to start OSD thread: {}", ret);
        instance_.reset();
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

void OsdService::updateCustomMessage() {
    if (!params_.enabled || !params_.custom_message_enabled)
        return;

    static constexpr char FILENAME[] = "/run/pixelpilot.msg";
    FILE *file = fopen(FILENAME, "r");
    if (!file)
        return;

    char message[80]{};
    if (fgets(message, sizeof(message), file) == nullptr) {
        perror("Error reading custom OSD message");
        fclose(file);
        return;
    }

    fclose(file);
    if (unlink(FILENAME) != 0)
        perror("Error deleting custom OSD message");

    message[sizeof(message) - 1] = '\0';
    if (char *newline = strchr(message, '\n'))
        *newline = '\0';

    FactTags tags = {{"file", FILENAME}};
    osd_.setFact(Fact(FactMeta("osd.custom_message", tags), std::string(message)));
}

void OsdService::paintBuffer(modeset_buf *buf) {
    cairo_surface_t *surface = cairo_image_surface_create_for_data(buf->map, CAIRO_FORMAT_ARGB32, buf->width, buf->height, buf->stride);
    cairo_t *cr = cairo_create(surface);

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);

    if (!video_present.load())
        osd_.drawScreensaver(cr);

    if (params_.enabled)
        osd_.draw(cr);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

void OsdService::run() {
    pthread_setname_np(pthread_self(), "__OSD");

    if (!params_.screensaver_image.empty()) {
        osd_.loadScreensaverImage(params_.screensaver_image);
    }
    if (params_.enabled && !params_.config_path.empty()) {
        osd_.loadConfig(params_.config_path);
    }

    auto last_display_at = std::chrono::steady_clock::now();

    modeset_buf *buf = &params_.out->osd_bufs[params_.out->osd_buf_switch];
    int ret = modeset_perform_modeset(params_.fd, params_.out, params_.out->osd_request, &params_.out->osd_plane, buf->fb, buf->width,
                                      buf->height, params_.zpos);
    assert(ret >= 0);
    while (!stop_.load()) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - last_display_at;
        const auto refresh_period = std::chrono::milliseconds(params_.refresh_frequency_ms);
        auto wait = std::chrono::milliseconds(0);
        if (params_.enabled) {
            if (elapsed < refresh_period) {
                wait = std::chrono::duration_cast<std::chrono::milliseconds>(refresh_period - elapsed);
            }
        } else {
            wait = std::chrono::seconds(1);
        }
        std::unique_lock<std::mutex> lock(fact_mutex_);
        fact_cv_.wait_for(lock, wait, [this] { return stop_.load() || (params_.enabled && !fact_queue_.empty());});
        if (stop_.load())
            break;

        const bool have_facts = params_.enabled && !fact_queue_.empty();

        lock.unlock();
        if (have_facts) {
            drainFacts();
            continue;
        }

        SPDLOG_DEBUG("refresh OSD");

        const int buf_idx = params_.out->osd_buf_switch ^ 1;
        buf = &params_.out->osd_bufs[buf_idx];

        updateCustomMessage();
        paintBuffer(buf);

        ret = pthread_mutex_lock(&osd_mutex);
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

        last_display_at = std::chrono::steady_clock::now();
    }
    spdlog::info("OSD thread done.");
}
