#ifndef OSD_WIDGETS_INFO_HPP
#define OSD_WIDGETS_INFO_HPP

#include "base.hpp"
#include "text.hpp"

#include <chrono>
#include <deque>
#include <string>
#include <sys/types.h>
#include <vector>

class TimeWidget : public TextWidget {
  public:
    TimeWidget(int pos_x, int pos_y, uint num_args);

    void draw(cairo_t *cr) override;

  private:
    static constexpr int MIN_VALID_YEAR = 2026;

    void updateTime();

    std::chrono::steady_clock::time_point last_update_{};
};

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

class DebugWidget : public Widget {
  public:
    DebugWidget(int pos_x, int pos_y, uint num_args) : Widget(pos_x, pos_y, num_args), lines_(num_args, "undef") {}

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;
    void setFact(uint idx, Fact fact) override;

  private:
    static std::string formatFact(const Fact &fact);

    static constexpr int LINE_HEIGHT = 20;

    std::vector<std::string> lines_;
};

class PopupWidget : public Widget {
  public:
    PopupWidget(int pos_x, int pos_y, uint timeout_ms, uint num_args);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;
    void setFact(uint, Fact fact) override;

  private:
    using Clock = std::chrono::steady_clock;

    struct Message {
        Clock::time_point time;
        std::string text;
        double width = 0.0;
        double height = 0.0;
        bool measured = false;
    };

    bool removeExpired(Clock::time_point now);

    static constexpr double PADDING = 5.0;
    static constexpr double ITEM_SPACING = 2.0;

    std::deque<Message> msgs_;
    const std::chrono::milliseconds timeout_;
};

#endif