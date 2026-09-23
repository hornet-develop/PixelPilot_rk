#ifndef PARSER_HELPERS_HPP
#define PARSER_HELPERS_HPP

#include "config.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>

#include <spdlog/common.h>

namespace parser_helpers {

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

inline bool parseBool(std::string_view value, bool &result) {
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

inline bool parseLogLevel(std::string_view value, spdlog::level::level_enum &result) {
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

inline bool parseScreenMode(const char *value, SystemConfig &system) {
    if (!value || *value == '\0') {
        return false;
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

} // namespace parser_helpers

#endif