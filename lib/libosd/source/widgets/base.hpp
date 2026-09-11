#ifndef OSD_WIDGETS_BASE_HPP
#define OSD_WIDGETS_BASE_HPP

#include <cairo.h>
#include <osd/fact.hpp>

#include <utility>
#include <vector>

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
    Widget(int pos_x, int pos_y, uint num_args = 0);
    virtual ~Widget() = default;

    Widget(const Widget &) = delete;
    Widget &operator=(const Widget &) = delete;

    virtual void draw(cairo_t *cr) = 0;
    virtual void measure(cairo_t *cr) = 0;
    virtual void setFact(uint idx, Fact fact);

    void setPosition(int x, int y);

    bool measureDirty() const;

    int width() const;
    int height() const;

    const Size &size() const;
    const Position &position() const;

    int x(cairo_t *cr) const;
    int y(cairo_t *cr) const;
    std::pair<int, int> xy(cairo_t *cr) const;

  protected:
    void measureChild(Widget &child, cairo_t *cr);

    void storeFact(uint idx, Fact fact);
    const Fact &fact(uint idx) const;
    uint factCount() const;

    void invalidateMeasure();
    void setSize(int width, int height);

  private:
    Size size_;
    Position position_;
    bool measure_dirty_ = true;
    std::vector<Fact> args_;
};

#endif
