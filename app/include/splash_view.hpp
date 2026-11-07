//
//  splash_view.hpp
//  Equinox
//

#pragma once

#include <borealis.hpp>

// Opening animation, shown once over the home when the app starts. The logo
// glows, then its sun and moon halves slide apart like doors, each taking half
// of the screen with it. Any button or a tap skips it.
class SplashView : public brls::Box {
  public:
    SplashView();

    // Pushes the splash over whatever activity is on top.
    static void show();

    bool isTranslucent() override { return true; }
    brls::View* getDefaultFocus() override { return this; }
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

  private:
    brls::Time start = 0;
    bool closing = false;

    void finish();
};
