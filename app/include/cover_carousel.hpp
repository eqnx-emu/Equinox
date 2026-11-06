//
//  cover_carousel.hpp
//  Equinox
//
//  Horizontal shelf of game covers: the focused one sits in the middle,
//  larger and floating, its neighbours step out smaller and dimmer.
//

#pragma once

#include <borealis.hpp>

#include <functional>

class CoverCarousel : public brls::Box {
  public:
    CoverCarousel();

    void addView(brls::View* view) override;
    void clearViews(bool free = true) override;
    void onChildFocusGained(brls::View* directChild,
                            brls::View* focusedView) override;
    void onChildFocusLost(brls::View* directChild,
                          brls::View* focusedView) override;
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    int selectedIndex() const { return selected; }
    size_t count() { return getChildren().size(); }
    // Time of the last selection change, to tell a deliberate tap on the
    // focused cover from the tap that just focused it.
    brls::Time selectedAt() const { return selectionTime; }

    // Called whenever the focused cover changes.
    std::function<void(int)> onSelect;

  private:
    void select(int index);

    int selected = 0;
    bool focused = false;
    brls::Animatable position = 0.0f;
    brls::Animatable focusGlow = 0.0f;
    brls::Time selectionTime = 0;
};

// Row of small dots showing the position in the shelf.
class CarouselDots : public brls::View {
  public:
    CarouselDots();
    void setState(size_t count, int index);
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

  private:
    size_t total = 0;
    int current = 0;
    brls::Animatable slide = 0.0f;
};
