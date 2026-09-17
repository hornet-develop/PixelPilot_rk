#include "config.hpp"

#include <ini.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>

namespace {

template <typename T> bool parseInteger(const char *text, long min_value, long max_value, T &result) {
    if (!text || *text == '\0') {
        return false;
    }

    errno = 0;
    char *end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value < min_value || value > max_value) {
        return false;
    }
    result = static_cast<T>(value);
    return true;
}

bool parseBool(std::string_view value, bool &result) {
    if (value == "true") {
        result = true;
        return true;
    }
    if (value == "false") {
        result = false;
        return true;
    }
    return false;
}

bool parseLogLevel(std::string_view value, spdlog::level::level_enum &result) {
    if (value == "debug") {
        result = spdlog::level::debug;
    } else if (value == "info") {
        result = spdlog::level::info;
    } else if (value == "warn") {
        result = spdlog::level::warn;
    } else if (value == "error") {
        result = spdlog::level::err;
    } else {
        return false;
    }
    return true;
}

bool parseScreenMode(const char *value, SystemConfig &system) {
    if (!value || *value == '\0') {
        return true;
    }
    int width = 0;
    int height = 0;
    int refresh_rate = 0;
    int consumed = 0;

    if (std::sscanf(value, "%dx%d@%d%n", &width, &height, &refresh_rate, &consumed) != 3 || value[consumed] != '\0' ||
        width <= 0 || height <= 0 || refresh_rate <= 0 || width > std::numeric_limits<uint16_t>::max() ||
        height > std::numeric_limits<uint16_t>::max()) {
        return false;
    }
    system.screen_width = static_cast<uint16_t>(width);
    system.screen_height = static_cast<uint16_t>(height);
    system.screen_refresh_rate = static_cast<uint32_t>(refresh_rate);
    return true;
}

int invalidConfigValue(std::string_view option, std::string_view value, const char *expected = nullptr) {
    if (expected) {
        spdlog::error("Invalid {} '{}' (expected {})", option, value, expected);
    } else {
        spdlog::error("Invalid {} '{}'", option, value);
    }
    return 0;
}

int configHandler(void *user, const char *section, const char *name, const char *value) {
    auto &config = *static_cast<Config *>(user);

    const std::string_view section_name = section;
    const std::string_view key = name;
    const std::string_view val = value;

    if (section_name == "system") {
        if (key == "listen_address") {
            struct in_addr address{};
            if (inet_pton(AF_INET, value, &address) != 1) {
                return invalidConfigValue("system.listen_address", val, "IPv4 address");
            }
            config.system.listen_address = value;
            return 1;
        }
        if (key == "listen_port") {
            if (!parseInteger(value, 1, std::numeric_limits<uint16_t>::max(), config.system.listen_port)) {
                return invalidConfigValue("system.listen_port", val, "1..65535");
            }
            return 1;
        }
        if (key == "socket") {
            config.system.socket_path = value;
            return 1;
        }
        if (key == "log_level") {
            if (!parseLogLevel(val, config.system.log_level)) {
                return invalidConfigValue("system.log_level", val, "debug|info|warn|error");
            }
            return 1;
        }
        if (key == "screen_mode") {
            if (!parseScreenMode(value, config.system)) {
                return invalidConfigValue("system.screen_mode", val, "<width>x<height>@<refresh>");
            }
            return 1;
        }
        if (key == "target_frame_rate") {
            uint32_t frame_rate;
            if (!parseInteger(value, 0, 120, frame_rate) || (frame_rate != 0 && frame_rate < 30)) {
                return invalidConfigValue("system.target_frame_rate", val, "0 or 30..120");
            }
            config.system.target_frame_rate = frame_rate;
            return 1;
        }
        if (key == "vsync") {
            if (!parseBool(val, config.system.vsync)) {
                return invalidConfigValue("system.vsync", val, "true|false");
            }
            return 1;
        }
        if (key == "wfb_port") {
            if (!parseInteger(value, 0, std::numeric_limits<uint16_t>::max(), config.system.wfb_port)) {
                return invalidConfigValue("system.wfb_port", val, "0..65535");
            }
            return 1;
        }
        if (key == "screensaver_image") {
            if (!val.empty() && !std::filesystem::exists(value)) {
                return invalidConfigValue("system.screensaver_image", val, "existing file");
            }
            config.system.screensaver_image = value;
            return 1;
        }
    }
    if (section_name == "osd") {
        if (key == "enabled") {
            if (!parseBool(val, config.osd.enabled)) {
                return invalidConfigValue("osd.enabled", val, "true|false");
            }
            return 1;
        }
        if (key == "config") {
            config.osd.config_path = value;
            return 1;
        }
        if (key == "refresh_ms") {
            if (!parseInteger(value, 1, 2000, config.osd.refresh_ms)) {
                return invalidConfigValue("osd.refresh_ms", val, "1..2000");
            }
            return 1;
        }
    }
    if (section_name == "osd.widgets") {
        bool enabled;
        if (!parseBool(val, enabled)) {
            return invalidConfigValue("osd.widgets", val, "true|false");
        }
        const auto widget = config.osd.widget_enabled.find(std::string(key));
        if (widget == config.osd.widget_enabled.end()) {
            spdlog::warn("Unknown OSD widget '{}'", key);
            return 1;
        }
        widget->second = enabled;
        return 1;
    }
    if (section_name == "dvr") {
        if (key == "start") {
            if (!parseBool(val, config.dvr.start)) {
                return invalidConfigValue("dvr.start", val, "true|false");
            }
            return 1;
        }
        if (key == "template") {
            config.dvr.file_template = value;
            return 1;
        }
        if (key == "osd") {
            if (!parseBool(val, config.dvr.osd)) {
                return invalidConfigValue("dvr.osd", val, "true|false");
            }
            return 1;
        }
        if (key == "bitrate") {
            if (!parseInteger(value, 1, std::numeric_limits<int>::max(), config.dvr.bitrate)) {
                return invalidConfigValue("dvr.bitrate", val, "positive integer");
            }
            return 1;
        }
        if (key == "segment_time_min") {
            if (!parseInteger(value, 0, 60, config.dvr.segment_time_min)) {
                return invalidConfigValue("dvr.segment_time_min", val, "0..60");
            }
            return 1;
        }
        if (key == "min_free_mb") {
            if (!parseInteger(value, 0, std::numeric_limits<int>::max(), config.dvr.min_free_mb)) {
                return invalidConfigValue("dvr.min_free_mb", val, "non-negative integer");
            }
            return 1;
        }
        if (key == "require_mount") {
            if (!parseBool(val, config.dvr.require_mount)) {
                return invalidConfigValue("dvr.require_mount", val, "true|false");
            }
            return 1;
        }
    }
    spdlog::warn("Unknown config option '{}.{}'", section_name, key);
    return 1;
}

} // namespace

bool loadConfigFile(const std::filesystem::path &path, Config &config) {
    const int result = ini_parse(path.c_str(), configHandler, &config);
    if (result < 0) {
        spdlog::error("Failed to read config file '{}'", path.string());
        return false;
    }
    if (result > 0) {
        spdlog::error("Failed to parse config file '{}' at line {}", path.string(), result);
        return false;
    }
    return true;
}
