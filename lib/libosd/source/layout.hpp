#ifndef OSD_LAYOUT_HPP
#define OSD_LAYOUT_HPP

#include <cairo.h>
#include <vector>

class Widget;

class Layout {
  public:
    Layout(int pos_x, int pos_y, int spacing) : pos_x_(pos_x), pos_y_(pos_y), spacing_(spacing) {}

    virtual ~Layout() = default;

    Layout(const Layout &) = delete;
    Layout &operator=(const Layout &) = delete;

    void addWidget(Widget *widget);

    void invalidate() {
        dirty_ = true;
    }

    bool dirty() const {
        return dirty_;
    }

    void update(cairo_t *cr);

  protected:
    virtual void doUpdate(cairo_t *cr) = 0;

    int x(cairo_t *cr) const;
    int y(cairo_t *cr) const;

    const std::vector<Widget *> &widgets() const {
        return widgets_;
    }

    int spacing() const {
        return spacing_;
    }

  private:
    const int pos_x_;
    const int pos_y_;
    const int spacing_;

    bool dirty_ = true;
    std::vector<Widget *> widgets_;
};

class HorizontalLayout : public Layout {
  public:
    enum class Direction {
        LeftToRight,
        RightToLeft,
    };

    HorizontalLayout(int pos_x, int pos_y, int spacing, Direction direction)
        : Layout(pos_x, pos_y, spacing), direction_(direction) {}

  protected:
    void doUpdate(cairo_t *cr) override;

  private:
    const Direction direction_;
};

#endif
