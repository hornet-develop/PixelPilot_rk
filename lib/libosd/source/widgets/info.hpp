#ifndef OSD_WIDGETS_INFO_HPP
#define OSD_WIDGETS_INFO_HPP

#include "base.hpp"
#include "text.hpp"

#include <chrono>
#include <deque>
#include <string>
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
    DebugWidget(int pos_x, int pos_y, uint num_args);

    void measure(cairo_t *cr) override;
    void draw(cairo_t *cr) override;
    void setFact(uint idx, Fact fact) override;

  private:
    static std::string formatFact(Fact fact);

    static constexpr int LINE_HEIGHT = 20;

    std::vector<std::string> lines_;
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

#endif