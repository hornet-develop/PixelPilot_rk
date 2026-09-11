#ifndef OSD_WIDGETS_COMPONENTS_HPP
#define OSD_WIDGETS_COMPONENTS_HPP

#include "helpers/running_average.hpp"
#include "primitives.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <osd/shared_surface.hpp>

// -----------------------------------------------------------------------------
// Composite widgets
// -----------------------------------------------------------------------------

class IconTextWidget : public Widget {
  public:
    IconTextWidget(int pos_x, int pos_y, cairo_surface_t *icon, std::string text, uint num_args = 0);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;

  private:
    static constexpr int SPACING = 14;

    IconWidget icon_;
    TextWidget text_;
};

class IconTplTextWidget : public TplTextWidget {
  public:
    IconTplTextWidget(int pos_x, int pos_y, cairo_surface_t *icon, std::string tpl, uint num_args);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;

  private:
    static constexpr int SPACING = 14;
    IconWidget icon_;
};

class BarChartWidget : public Widget {
  public:
    enum StatsField { STATS_MIN, STATS_MAX, STATS_SUM, STATS_COUNT, STATS_AVG };

    BarChartWidget(int pos_x, int pos_y, uint width, uint height, uint window_s, uint num_buckets,
                   StatsField stats_field);

    void measure(cairo_t *) override;
    void draw(cairo_t *cr) override;
    void setFact(uint idx, Fact fact) override;

  private:
    std::string shorten(long num);
    std::vector<double> select_stats(std::vector<Stats> stats);

    uint width_;
    uint height_;
    uint window_ms_;
    uint num_buckets_;

    StatsField stats_field_ = STATS_SUM;
    RunningAverage stats_;

    TextWidget max_label_;
    TextWidget min_label_;
};

/**
 * Displays text facts for a period of time, stacking them one after another; fading-out opacity.
 * Convenient for warnings, custom messages and pop-ups.
 *
 * @param timeout_ms stop displaying the fact after this many milliseconds since it was received
 */
class PopupWidget : public Widget {
  public:
    PopupWidget(int pos_x, int pos_y, uint timeout_ms, uint num_args);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;
    void setFact(uint, Fact fact) override;

  private:
    struct Message {
        std::chrono::time_point<std::chrono::steady_clock> time;
        std::string text;
        double width = 0.0;
        double height = 0.0;
        bool measured = false;
    };

    bool removeExpired(std::chrono::time_point<std::chrono::steady_clock> now);

    static constexpr double PADDING = 5.0;
    static constexpr double ITEM_SPACING = 2.0;

    std::deque<Message> msgs_;
    std::chrono::milliseconds timeout_;
};

// -----------------------------------------------------------------------------
// Status widgets
// -----------------------------------------------------------------------------

class DvrStatusWidget : public Widget {
  public:
    DvrStatusWidget(int pos_x, int pos_y, cairo_surface_t *icon, std::string text);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;

  private:
    bool isActive() const;

    static constexpr int SPACING = 14;

    static constexpr DrawStyle ICON_STYLE{
        .fill = {1.0, 0.0, 0.0, 1.0},
        .outline = {0.0, 0.0, 0.0, 1.0},
        .outline_width = 1.0,
    };

    static constexpr DrawStyle TEXT_STYLE{
        .fill = {1.0, 0.0, 0.0, 1.0},
        .outline = {0.0, 0.0, 0.0, 1.0},
        .outline_width = 2.0,
    };

    IconWidget icon_;
    TextWidget text_;
};

class DvrStorageWidget : public Widget {
  public:
    DvrStorageWidget(int pos_x, int pos_y, cairo_surface_t *icon);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;
    void setFact(uint idx, Fact fact) override;

  private:
    void updateState();
    void updateStorageText();

    static std::string format_storage_size(uint64_t bytes);

    static constexpr int SPACING = 5;
    bool visible_ = false;
    bool show_text_ = false;

    IconWidget icon_;
    TextWidget text_;
};

class IconStatusWidget : public IconWidget {
  public:
    IconStatusWidget(int pos_x, int pos_y, cairo_surface_t *icon);

    void setFact(uint idx, Fact fact) override;
};

class IconTplStatusWidget : public Widget {
  public:
    IconTplStatusWidget(int pos_x, int pos_y, cairo_surface_t *icon, std::string tpl, uint num_args);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;
    void setFact(uint idx, Fact fact) override;

  private:
    void setActiveStyle();
    void setInactiveStyle();

    static constexpr int SPACING = 9;

    IconWidget icon_;
    TplTextWidget text_;
};

// -----------------------------------------------------------------------------
// Video widgets
// -----------------------------------------------------------------------------

class VideoWidget : public Widget {
  public:
    VideoWidget(int pos_x, int pos_y, uint window_size_ms, uint bucket_size_ms, cairo_surface_t *icon, std::string tpl,
                uint refresh_rate, uint num_args, uint refresh_frequency_ms);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;
    void setFact(uint idx, Fact fact) override;

  private:
    static constexpr int SPACING = 14;

    IconWidget icon_;
    TplTextWidget text_;

    RunningAverage fps_;
    std::chrono::milliseconds refresh_rate_ms_{};
    std::chrono::steady_clock::time_point last_drawn_{};
};

class VideoBitrateWidget : public Widget {
  public:
    VideoBitrateWidget(int pos_x, int pos_y, uint window_size_ms, uint bucket_size_ms, cairo_surface_t *icon,
                       std::string tpl, uint refresh_rate, uint num_args, uint refresh_frequency_ms);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;
    void setFact(uint idx, Fact fact) override;

  private:
    static constexpr int SPACING = 14;

    IconWidget icon_;
    TplTextWidget text_;

    RunningAverage bps_;
    std::chrono::milliseconds refresh_rate_ms_{};
    std::chrono::steady_clock::time_point last_drawn_{};
};

class VideoDecodeLatencyWidget : public Widget {
  public:
    VideoDecodeLatencyWidget(int pos_x, int pos_y, uint window_size_ms, uint bucket_size_ms, cairo_surface_t *icon,
                             std::string tpl, uint refresh_rate, uint num_args, uint refresh_frequency_ms);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;
    void setFact(uint idx, Fact fact) override;

  private:
    static constexpr int SPACING = 14;

    IconWidget icon_;
    TplTextWidget text_;

    RunningAverage timing_;
    std::chrono::milliseconds refresh_rate_ms_{};
    std::chrono::steady_clock::time_point last_drawn_{};
};

// -----------------------------------------------------------------------------
// Other widgets
// -----------------------------------------------------------------------------

class GPSWidget : public TextWidget {
  public:
    GPSWidget(int pos_x, int pos_y, uint num_args);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;

  private:
    bool isReady() const;

    std::string formatText() const;

    static constexpr int TEXT_OFFSET_X = 40;
};

class TimeWidget : public TextWidget {
  public:
    TimeWidget(int pos_x, int pos_y, uint num_args);

    void draw(cairo_t *cr) override;

  private:
    static constexpr int MIN_VALID_YEAR = 2026;

    void updateTime();

    std::chrono::steady_clock::time_point last_update_{};
};

class DebugWidget : public Widget {
  public:
    DebugWidget(int pos_x, int pos_y, uint num_args);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;
    void setFact(uint idx, Fact fact) override;

  private:
    static std::string formatFact(Fact fact);

    static constexpr int LINE_HEIGHT = 20;

    std::vector<std::string> lines_;
};

class ExternalSurfaceWidget : public Widget {
  public:
    ExternalSurfaceWidget(int pos_x, int pos_y, std::string shm_name, uint refresh_frequency_ms);
    ~ExternalSurfaceWidget() override;

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;

  private:
    void init_shm(cairo_t *cr);

    SharedMemoryRegion *shm_region = nullptr;
    int32_t last_surface_index = -1;
    cairo_surface_t *shm_surfaces[SHM_BUFFERS_COUNT] = {};
    size_t shm_size = 0;
    unsigned char *shm_data = nullptr;
    std::string shm_name;
    uint refresh_frequency_ms_;
};

class IconSelectorWidget : public Widget {
  public:
    IconSelectorWidget(int pos_x, int pos_y,
                       const std::vector<std::pair<std::pair<int, int>, std::filesystem::path>> &ranges_and_icons,
                       const std::filesystem::path &assets_dir);
    ~IconSelectorWidget() override;

    void measure(cairo_t *) override;
    void draw(cairo_t *cr) override;
    void setFact(uint idx, Fact fact) override;

  private:
    cairo_surface_t *selectIcon(Fact &fact);
    cairo_surface_t *openIcon(const std::filesystem::path &icon_path);

    std::map<std::pair<int, int>, cairo_surface_t *> icon_cache_; // Cache of loaded icons
    std::filesystem::path assets_dir_;
    cairo_surface_t *current_icon_ = nullptr; // Currently selected icon
};

#endif
