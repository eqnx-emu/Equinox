//
//  backdrop_frame.hpp
//  Equinox
//
//  AppletFrame that lets the current content paint a backdrop over the
//  whole frame (under the header and footer), e.g. the library's glow.
//

#pragma once

#include <borealis.hpp>

#include <functional>

class BackdropFrame : public brls::AppletFrame {
  public:
    using Painter = std::function<void(NVGcontext*, float x, float y,
                                       float width, float height)>;

    void setBackdrop(Painter painter) { backdrop = std::move(painter); }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override {
        // The frame background is already painted; header, content and
        // footer draw after this.
        if (backdrop)
            backdrop(vg, x, y, width, height);
        AppletFrame::draw(vg, x, y, width, height, style, ctx);
    }

    static brls::View* create() { return new BackdropFrame(); }

  private:
    Painter backdrop;
};
