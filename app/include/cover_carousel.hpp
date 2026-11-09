//
//  cover_carousel.hpp
//  Equinox
//
//  Horizontal shelf of covers or cards: the focused one sits in the middle,
//  larger and floating, its neighbours step out smaller and dimmer.
//

#pragma once

#include <borealis.hpp>

#include <functional>
#include <vector>

// Sizes and spacing of a shelf.
struct ShelfStyle {
    float height = 408;
    // Scale of the focused item, of its direct neighbours, and the smallest.
    float focusScale = 1.32f;
    float neighbourScale = 0.84f;
    float minScale = 0.62f;
    // Scale lost per step past the neighbours.
    float scaleStep = 0.1f;
    // Distance from the centre to the first neighbour, then between items.
    float firstStep = 228;
    float step = 158;
    // Opacity of the neighbours and what each further step loses.
    float neighbourAlpha = 0.7f;
    float alphaStep = 0.22f;
    // How much narrower the neighbours look (turned towards the centre).
    float squash = 0.1f;
    // Darkening of the items away from the centre.
    float dim = 0.35f;
    float radius = 12;
    // Light edge around every item, to lift it off the background.
    float edge = 0;

    // Game covers: library and favorites.
    static ShelfStyle covers() { return {}; }

    // Cards on the home screen.
    static ShelfStyle cards() {
        ShelfStyle s;
        s.height = 300;
        s.focusScale = 1.12f;
        s.neighbourScale = 0.8f;
        s.minScale = 0.64f;
        s.scaleStep = 0.16f;
        s.firstStep = 330;
        s.step = 220;
        s.neighbourAlpha = 0.88f;
        s.alphaStep = 0.3f;
        s.squash = 0.04f;
        s.dim = 0.1f;
        s.radius = 16;
        s.edge = 0.28f;
        return s;
    }
};

// An item that draws its own look: when frameless() is true the shelf skips
// the frame it puts around a cover (glow, dimming, edge and focus ring) and
// only keeps the shadow on the ground.
class ShelfItem {
  public:
    virtual ~ShelfItem() = default;
    virtual bool frameless() const { return false; }
};

class CoverCarousel : public brls::Box {
  public:
    explicit CoverCarousel(ShelfStyle style = ShelfStyle::covers());

    void addView(brls::View* view) override;
    void clearViews(bool free = true) override;
    void onChildFocusGained(brls::View* directChild,
                            brls::View* focusedView) override;
    void onChildFocusLost(brls::View* directChild,
                          brls::View* focusedView) override;
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    int selectedIndex() const { return selected; }
    // Moves the shelf to a cover without the slide; focus it afterwards.
    void jumpTo(int index) { position.reset((float)index); }
    size_t count() { return getChildren().size(); }
    // Time of the last selection change, to tell a deliberate tap on the
    // focused cover from the tap that just focused it.
    brls::Time selectedAt() const { return selectionTime; }

    // Called whenever the focused cover changes.
    std::function<void(int)> onSelect;

    // "Entering" the focused item: it moves offsetX away from the centre
    // and settles at its normal size, the others fly off to the sides.
    void setEntered(bool entered, float offsetX = 0);
    bool isEntered() const { return entered; }

  private:
    void select(int index);

    ShelfStyle shelf;
    int selected = 0;
    bool focused = false;
    bool entered = false;
    float enterOffset = 0;
    brls::Animatable position = 0.0f;
    brls::Animatable focusGlow = 0.0f;
    brls::Animatable enter = 0.0f;
    brls::Time selectionTime = 0;
    brls::Time enterTime = 0;
    // Reused every frame for the visible covers, far to near.
    std::vector<size_t> drawOrder;
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
