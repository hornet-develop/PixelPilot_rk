#ifndef OSD_LAYOUT_HPP
#define OSD_LAYOUT_HPP

#include <cairo.h>
#include <vector>

class Widget;

class Layout {
  public:
    Layout(int pos_x, int pos_y, int spacing);
    virtual ~Layout() = default;

    Layout(const Layout &) = delete;
    Layout &operator=(const Layout &) = delete;

    void addWidget(Widget *widget);

    void invalidate();
    bool dirty() const;
    void update(cairo_t *cr);

  protected:
    virtual void doUpdate(cairo_t *cr) = 0;

    int x(cairo_t *cr) const;
    int y(cairo_t *cr) const;

    const std::vector<Widget *> &widgets() const;
    int spacing() const;

  private:
    int pos_x_;
    int pos_y_;
    int spacing_;

    bool dirty_ = true;
    std::vector<Widget *> widgets_;
};

class HorizontalLayout : public Layout {
  public:
    enum class Direction {
        LeftToRight,
        RightToLeft,
    };

    HorizontalLayout(int pos_x, int pos_y, int spacing, Direction direction);

  protected:
    void doUpdate(cairo_t *cr) override;

  private:
    Direction direction_;
};

#endif
