#ifndef OSD_WIDGETS_BASE_HPP
#define OSD_WIDGETS_BASE_HPP

#include <fact.hpp>

#include <sys/types.h>
#include <utility>
#include <vector>

#include <cairo.h>

struct Size {
    int width = 0;
    int height = 0;
};

struct Position {
    int x = 0;
    int y = 0;
};

struct CairoColor {
    double r;
    double g;
    double b;
    double a;
};

struct DrawStyle {
    CairoColor fill;
    CairoColor outline;
    double outline_width;
};

class Widget {
  public:
    Widget(int pos_x, int pos_y, uint num_args = 0) : position_{pos_x, pos_y}, args_(num_args) {}
    virtual ~Widget() = default;

    Widget(const Widget &) = delete;
    Widget &operator=(const Widget &) = delete;

    virtual void draw(cairo_t *cr) = 0;
    virtual void measure(cairo_t *cr) = 0;
    virtual void setFact(uint idx, Fact fact);

    void setPosition(int x, int y) {
        position_ = {x, y};
    }

    bool measureDirty() const {
        return measure_dirty_;
    }

    int width() const {
        return size_.width;
    }

    int height() const {
        return size_.height;
    }

  protected:
    static void measureChild(Widget &child, cairo_t *cr);
    void storeFact(uint idx, Fact fact);

    int x(cairo_t *cr) const;
    int y(cairo_t *cr) const;

    std::pair<int, int> xy(cairo_t *cr) const {
        return {x(cr), y(cr)};
    }

    const Fact &fact(uint idx) const {
        return args_.at(idx);
    }

    uint factCount() const {
        return static_cast<uint>(args_.size());
    }

    void invalidateMeasure() {
        measure_dirty_ = true;
    }

    void setSize(int width, int height);

  private:
    Size size_;
    Position position_;
    bool measure_dirty_ = true;
    std::vector<Fact> args_;
};

#endif
