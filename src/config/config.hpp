#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

#include <spdlog/common.h>

struct VideoConfig {
    std::string address = "0.0.0.0";
    uint16_t port = 5600;
    std::string socket;
};

struct LoggingConfig {
    spdlog::level::level_enum level = spdlog::level::info;
};

struct DisplayConfig {
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t refresh_rate = 0;

    uint32_t target_frame_rate = 0;
    bool vsync = true;
};

struct DvrConfig {
    bool start = false;

    std::string file_template;

    bool osd = false;
    int bitrate = 8000000;
    int segment_time_min = 0;
    int min_free_mb = 200;
    bool require_mount = false;
};

struct OsdConfig {
    bool enabled = false;

    std::string config_path;
    uint32_t refresh_ms = 1000;

    std::unordered_map<std::string, bool> widget_enabled{
        {"video_fps_resolution", true},
        {"video_link_bitrate", true},
        {"signal_snr", true},
        {"signal_rssi", true},
        {"dvr_storage", true},
        {"wfb_status", true},
        {"drone_status", true},
        {"dvr_status", true},
        {"msposd", true}
    };
};

struct WfbConfig {
    uint16_t api_port = 8003;
};

struct ScreensaverConfig {
    std::string image_path;
};

struct Config {
    VideoConfig video;
    LoggingConfig logging;
    DisplayConfig display;
    DvrConfig dvr;
    OsdConfig osd;
    WfbConfig wfb;
    ScreensaverConfig screensaver;
};

bool loadConfigFile(const std::filesystem::path &path, Config &config);

#endif