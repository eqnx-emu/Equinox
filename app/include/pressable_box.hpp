//
//  pressable_box.hpp
//  Equinox
//
//  A box that sinks in and springs back when pressed. The press finishes
//  before the action runs, so the animation is seen even when the action
//  leaves the screen.
//

#pragma once

#include <borealis.hpp>
#include <functional>

class PressableBox : public brls::Box {
  public:
    using brls::Box::Box;

    void press(std::function<void()> then) {
        if (pressing)
            return; // a second press while animating would run the action twice
        scale.stop();
        pressing = true;
        scale.reset(1.0f);
        scale.addStep(0.94f, 90, brls::EasingFunction::quadraticOut);
        scale.addStep(1.0f, 180, brls::EasingFunction::backOut);
        scale.setEndCallback([this, then](bool finished) {
            pressing = false;
            if (finished && then)
                then();
        });
        scale.start();
    }

    void frame(brls::FrameContext* ctx) override {
        const float s = scale;
        if (s == 1.0f) {
            brls::Box::frame(ctx);
            return;
        }

        const float cx = getX() + getWidth() / 2;
        const float cy = getY() + getHeight() / 2;
        nvgSave(ctx->vg);
        nvgTranslate(ctx->vg, cx, cy);
        nvgScale(ctx->vg, s, s);
        nvgTranslate(ctx->vg, -cx, -cy);
        brls::Box::frame(ctx);
        nvgRestore(ctx->vg);
    }

  private:
    brls::Animatable scale = 1.0f;
    bool pressing = false;
};
