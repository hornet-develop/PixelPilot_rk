#include "command_line.hpp"
#include "config.hpp"

#include "pixelpilot_config.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <getopt.h>
#include <limits>
#include <string_view>

#include <spdlog/spdlog.h>

namespace {

constexpr char short_options[] = "ha:p:";

constexpr char help_text[] =
    "  Usage:\n"
    "    pixelpilot [Arguments]\n"
    "\n"
    "  Arguments:\n"
    "    -a <address>              - Listen address for RTP video stream   (Default: 0.0.0.0)\n"
    "\n"
    "    -p <port>                 - UDP port for RTP video stream         (Default: 5600)\n"
    "\n"
    "    --config <file>           - Load PixelPilot configuration from INI file\n"
    "\n"
    "    --socket <socket>         - read data from socket\n"
    "\n"
    "    --codec <codec>           - [ Deprecated ] Video codec, should be the same as on VTX\n"
    "                                Now codec is detected dynamically during runtime.\n"
    "                                Passed value <codec> will ignored\n"
    "\n"
    "    --log-level <level>       - Log verbosity level, debug|info|warn|error (Default: info)\n"
    "\n"
    "    --osd                     - Enable OSD\n"
    "\n"
    "    --osd-config <file>       - Path to OSD configuration file\n"
    "\n"
    "    --osd-refresh <rate>      - Defines the delay between osd refresh (Default: 1000 ms)\n"
    "\n"
    "    --dvr-template <path>     - Save the video feed (no osd) to the provided filename template.\n"
    "                                DVR is toggled by SIGUSR1 signal\n"
    "                                Supports placeholders %N, %Y, %m, %d, %H, %M, %S\n"
    "                                Example: /media/DVR/%N_%Y-%m-%d_%H-%M-%S.ts\n"
    "\n"
    "    --dvr-start               - Start DVR immediately\n"
    "\n"
    "    --dvr-osd                 - Burn OSD into DVR recording (WYSIWYG via DRM writeback)\n"
    "\n"
    "    --dvr-bitrate <bps>       - Target bitrate for DVR re-encoding (Default: 8000000)\n"
    "\n"
    "    --dvr-segment-time <min>  - Start a new DVR file every N minutes (0 = disabled, Default: 0)\n"
    "\n"
    "    --dvr-min-free-mb <MB>    - Stop/refuse DVR recording below this free space (Default: 200)\n"
    "\n"
    "    --dvr-require-mount       - Only record if the DVR directory is on a mounted external device\n"
    "\n"
    "    --screen-mode <mode>      - Override default screen mode. <width>x<heigth>@<fps> ex: 1920x1080@120\n"
    "\n"
    "    --target-frame-rate <fps> - Target DRM refresh rate for mode selection (30..120), ex: 60\n"
    "                                Makes DRM choose the highest available resolution at the requested FPS\n"
    "                                For optimal smoothness, use a value equal to or divisible by the video FPS\n"
    "\n"
    "    --disable-vsync           - Disable VSYNC commits\n"
    "\n"
    "    --screen-mode-list        - Print the list of supported screen modes and exit\n"
    "\n"
    "    --wfb-api-port            - Port of wfb-server for cli statistics. (Default: 8003)\n"
    "                                Use \"0\" to disable this stats\n"
    "\n"
    "    --screensaver-image       - Path to a PNG image to display on the screensaver\n"
    "\n"
    "    --version                 - Show program version\n";

enum OptionId {
    OPT_SOCKET = 256,
    OPT_CODEC,
    OPT_DVR,
    OPT_DVR_START,
    OPT_DVR_TEMPLATE,
    OPT_DVR_OSD,
    OPT_DVR_BITRATE,
    OPT_DVR_SEGMENT_TIME,
    OPT_DVR_MIN_FREE_MB,
    OPT_DVR_REQUIRE_MOUNT,
    OPT_LOG_LEVEL,
    OPT_OSD,
    OPT_OSD_CONFIG,
    OPT_OSD_REFRESH,
    OPT_OSD_ELEMENTS,
    OPT_OSD_TELEM_LVL,
    OPT_SCREEN_MODE,
    OPT_TARGET_FRAME_RATE,
    OPT_DISABLE_VSYNC,
    OPT_SCREEN_MODE_LIST,
    OPT_WFB_API_PORT,
    OPT_SCREENSAVER_IMG,
    OPT_CONFIG,
    OPT_VERSION
};

const struct option long_options[] = {
    {"socket", required_argument, nullptr, OPT_SOCKET},
    {"codec", required_argument, nullptr, OPT_CODEC},
    {"dvr", required_argument, nullptr, OPT_DVR},
    {"dvr-start", no_argument, nullptr, OPT_DVR_START},
    {"dvr-template", required_argument, nullptr, OPT_DVR_TEMPLATE},
    {"dvr-osd", no_argument, nullptr, OPT_DVR_OSD},
    {"dvr-bitrate", required_argument, nullptr, OPT_DVR_BITRATE},
    {"dvr-segment-time", required_argument, nullptr, OPT_DVR_SEGMENT_TIME},
    {"dvr-min-free-mb", required_argument, nullptr, OPT_DVR_MIN_FREE_MB},
    {"dvr-require-mount", no_argument, nullptr, OPT_DVR_REQUIRE_MOUNT},
    {"log-level", required_argument, nullptr, OPT_LOG_LEVEL},
    {"osd", no_argument, nullptr, OPT_OSD},
    {"osd-config", required_argument, nullptr, OPT_OSD_CONFIG},
    {"osd-refresh", required_argument, nullptr, OPT_OSD_REFRESH},
    {"osd-elements", required_argument, nullptr, OPT_OSD_ELEMENTS},
    {"osd-telem-lvl", required_argument, nullptr, OPT_OSD_TELEM_LVL},
    {"screen-mode", required_argument, nullptr, OPT_SCREEN_MODE},
    {"target-frame-rate", required_argument, nullptr, OPT_TARGET_FRAME_RATE},
    {"disable-vsync", no_argument, nullptr, OPT_DISABLE_VSYNC},
    {"screen-mode-list", no_argument, nullptr, OPT_SCREEN_MODE_LIST},
    {"wfb-api-port", required_argument, nullptr, OPT_WFB_API_PORT},
    {"screensaver-image", required_argument, nullptr, OPT_SCREENSAVER_IMG},
    {"config", required_argument, nullptr, OPT_CONFIG},
    {"version", no_argument, nullptr, OPT_VERSION},
    {"help", no_argument, nullptr, 'h'},
    {nullptr, 0, nullptr, 0}
};

void printHelp() {
    std::printf("PixelPilot FPV Decoder for Rockchip (%d.%d)\n\n", APP_VERSION_MAJOR, APP_VERSION_MINOR);
    std::fputs(help_text, stdout);
}

void printVersion() {
    std::printf("PixelPilot Rockchip %d.%d\n", APP_VERSION_MAJOR, APP_VERSION_MINOR);
}

template <typename T> bool parseInteger(const char *text, long min, long max, T &result) {
    if (!text || *text == '\0') {
        return false;
    }

    errno = 0;
    char *end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || value < min || value > max) {
        return false;
    }
    result = static_cast<T>(value);
    return true;
}

bool parseLogLevel(std::string_view value, spdlog::level::level_enum &level) {
    if (value == "debug") {
        level = spdlog::level::debug;
    } else if (value == "info") {
        level = spdlog::level::info;
    } else if (value == "warn") {
        level = spdlog::level::warn;
    } else if (value == "error") {
        level = spdlog::level::err;
    } else {
        return false;
    }
    return true;
}

bool parseScreenMode(const char *value, SystemConfig &system) {
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

CommandLineResult invalidArgument(const char *option, const char *value, const char *expected = nullptr) {
    if (expected) {
        spdlog::error("{}: invalid value '{}' (expected {})", option, value, expected);
    } else {
        spdlog::error("{}: invalid value '{}'", option, value);
    }
    printHelp();
    return CommandLineResult::Error;
}

} // namespace

std::filesystem::path findConfigPath(int argc, char **argv) {
    std::filesystem::path config_path;
    const int saved_opterr = opterr;

    opterr = 0;
    optind = 1;

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1) {
        if (opt == OPT_CONFIG) {
            config_path = optarg;
        }
    }
    optind = 1;
    opterr = saved_opterr;

    return config_path;
}

CommandLineResult parseCommandLine(int argc, char **argv, Config &config) {
    CommandLineResult result = CommandLineResult::Run;

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                printHelp();
                return CommandLineResult::ExitSuccess;

            case 'a': { // -a <address>
                struct in_addr address{};
                if (inet_pton(AF_INET, optarg, &address) != 1) {
                    return invalidArgument("-a", optarg, "IPv4 address");
                }
                config.system.listen_address = optarg;
                break;
            }

            case 'p': // -p <port>
                if (!parseInteger(optarg, 1, std::numeric_limits<uint16_t>::max(), config.system.listen_port)) {
                    return invalidArgument("-p", optarg, "1..65535");
                }
                break;

            case OPT_SOCKET: // --socket
                config.system.socket_path = optarg;
                break;

            case OPT_CODEC: // --codec (deprecated)
                spdlog::warn("--codec parameter is removed");
                break;

            case OPT_DVR: // --dvr (deprecated)
                config.dvr.file_template = optarg;
                config.dvr.start = true;
                spdlog::warn("--dvr is deprecated. Use --dvr-template and --dvr-start");
                break;

            case OPT_DVR_START: // --dvr-start
                config.dvr.start = true;
                break;

            case OPT_DVR_TEMPLATE: // --dvr-template
                config.dvr.file_template = optarg;
                break;

            case OPT_DVR_OSD: // --dvr-osd
                config.dvr.osd = true;
                break;

            case OPT_DVR_BITRATE: // --dvr-bitrate
                if (!parseInteger(optarg, 1, std::numeric_limits<int>::max(), config.dvr.bitrate)) {
                    return invalidArgument("--dvr-bitrate", optarg, "positive integer");
                }
                break;

            case OPT_DVR_SEGMENT_TIME: // --dvr-segment-time
                if (!parseInteger(optarg, 0, 60, config.dvr.segment_time_min)) {
                    return invalidArgument("--dvr-segment-time", optarg, "0..60 minutes");
                }
                break;

            case OPT_DVR_MIN_FREE_MB: // --dvr-min-free-mb
                if (!parseInteger(optarg, 0, std::numeric_limits<int>::max(), config.dvr.min_free_mb)) {
                    return invalidArgument("--dvr-min-free-mb", optarg, "non-negative integer");
                }
                break;

            case OPT_DVR_REQUIRE_MOUNT: // --dvr-require-mount
                config.dvr.require_mount = true;
                break;

            case OPT_LOG_LEVEL: // --log-level
                if (!parseLogLevel(optarg, config.system.log_level)) {
                    return invalidArgument("--log-level", optarg, "debug|info|warn|error");
                }
                break;

            case OPT_OSD: // --osd
                config.osd.enabled = true;
                break;

            case OPT_OSD_CONFIG: // --osd-config
                config.osd.config_path = optarg;
                break;

            case OPT_OSD_REFRESH: // --osd-refresh
                if (!parseInteger(optarg, 1, 2000, config.osd.refresh_ms)) {
                    return invalidArgument("--osd-refresh", optarg, "1..2000 ms");
                }
                break;

            case OPT_OSD_ELEMENTS: // --osd-elements (deprecated)
                spdlog::warn("--osd-elements parameter is removed");
                break;

            case OPT_OSD_TELEM_LVL: // --osd-telem-lvl (deprecated)
                spdlog::warn("--osd-telem-lvl parameter is removed");
                break;

            case OPT_SCREEN_MODE: // --screen-mode
                if (!parseScreenMode(optarg, config.system)) {
                    return invalidArgument("--screen-mode", optarg, "<width>x<height>@<refresh>");
                }
                break;

            case OPT_TARGET_FRAME_RATE: { // --target-frame-rate
                uint32_t frame_rate;
                if (!parseInteger(optarg, 0, 120, frame_rate) || (frame_rate != 0 && frame_rate < 30)) {
                    return invalidArgument("--target-frame-rate", optarg, "0 or 30..120");
                }
                config.system.target_frame_rate = frame_rate;
                break;
            }

            case OPT_DISABLE_VSYNC: // --disable-vsync
                config.system.vsync = false;
                break;

            case OPT_SCREEN_MODE_LIST: // --screen-mode-list
                result = CommandLineResult::PrintModeList;
                break;

            case OPT_WFB_API_PORT: // --wfb-api-port
                if (!parseInteger(optarg, 0, std::numeric_limits<uint16_t>::max(), config.system.wfb_port)) {
                    return invalidArgument("--wfb-api-port", optarg, "0..65535");
                }
                break;

            case OPT_SCREENSAVER_IMG: { // --screensaver-image
                const std::filesystem::path image_path = optarg;
                if (!std::filesystem::exists(image_path)) {
                    return invalidArgument("--screensaver-image", optarg, "existing file");
                }
                config.system.screensaver_image = image_path.string();
                break;
            }

            case OPT_CONFIG:
                // Config file has already been loaded before CLI parsing.
                break;

            case OPT_VERSION: // --version
                printVersion();
                return CommandLineResult::ExitSuccess;

            case '?':
            default:
                printHelp();
                return CommandLineResult::Error;
        }
    }
    return result;
}
