#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

#include <spdlog/common.h>

struct SystemConfig {
    std::string listen_address = "0.0.0.0";
    uint16_t listen_port = 5600;
    std::string socket_path;

    uint16_t screen_width = 0;
    uint16_t screen_height = 0;
    uint32_t screen_refresh_rate = 0;
    uint32_t target_frame_rate = 0;
    bool stretch_video = false;
    bool vsync = true;

    uint16_t wfb_port = 8003;
    spdlog::level::level_enum log_level = spdlog::level::info;

    bool screensaver_enabled = false;
    std::string screensaver_image;
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
        {"msposd", true},
    };
};

struct Config {
    SystemConfig system;
    DvrConfig dvr;
    OsdConfig osd;
};

bool loadConfigFile(const std::filesystem::path &path, Config &config);

#endif
