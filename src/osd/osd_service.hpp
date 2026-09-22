#ifndef OSD_SERVICE_HPP
#define OSD_SERVICE_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include <fact.hpp>
#include <osd.hpp>

struct modeset_buf;
struct modeset_output;

struct OsdServiceParams {
    modeset_output *out = nullptr;
    int fd = -1;

    std::string config_path;
    std::string screensaver_image;
    bool screensaver_enabled = false;

    uint32_t refresh_frequency_ms = 1000;
    int zpos = 2;

    bool enabled = false;
    std::unordered_map<std::string, bool> widget_enabled;
};

class OsdService {
  public:
    ~OsdService() = default;

    static bool start(OsdServiceParams params);
    static void stop();

    static void publishFact(Fact fact);
    static void publishFacts(std::vector<Fact> facts);

    OsdService(const OsdService &) = delete;
    OsdService &operator=(const OsdService &) = delete;

  private:
    explicit OsdService(OsdServiceParams params);

    static void *threadEntry(void *arg);

    bool init();
    void run();
    void refresh();

    void enqueueFact(Fact fact);
    void enqueueFacts(std::vector<Fact> facts);
    void drainFacts();

    void paintBuffer(modeset_buf *buf);

    static std::unique_ptr<OsdService> instance_;

    OsdServiceParams params_;
    Osd osd_;

    pthread_t thread_{};

    std::queue<Fact> fact_queue_;
    std::mutex fact_mutex_;
    std::condition_variable fact_cv_;

    std::atomic<bool> stop_{false};
};

#endif
