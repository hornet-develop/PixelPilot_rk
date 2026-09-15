#ifndef OSD_WIDGETS_VIDEO_HPP
#define OSD_WIDGETS_VIDEO_HPP

#include "base.hpp"
#include "helpers/running_average.hpp"
#include "icon.hpp"
#include "text.hpp"

#include <chrono>
#include <string>
#include <sys/types.h>

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
    const std::chrono::milliseconds refresh_rate_ms_;
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
    const std::chrono::milliseconds refresh_rate_ms_;
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
    const std::chrono::milliseconds refresh_rate_ms_;
    std::chrono::steady_clock::time_point last_drawn_{};
};

#endif