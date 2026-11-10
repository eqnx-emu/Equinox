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
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

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

    // Skeleton: each line of text drawn as a soft bar instead. Shown while the
    // shelf zooms in, then the text fades in over `fadeMs`.
    void setSkeleton(bool on, int fadeMs = 0) {
        if (on && skeletonLabels.empty())
            collectLabels(this);
        skeleton.stop();
        const float target = on ? 1.0f : 0.0f;
        if (fadeMs <= 0) {
            skeleton.reset(target);
            return;
        }
        skeleton.reset(skeleton.getValue());
        skeleton.addStep(target, fadeMs, brls::EasingFunction::quadraticOut);
        skeleton.start();
    }

    void frame(brls::FrameContext* ctx) override {
        const float sk = skeleton;
        if (!skeletonLabels.empty())
            fadeLabels(sk);

        const float s = scale;
        if (s == 1.0f) {
            brls::Box::frame(ctx);
            if (sk > 0.0f)
                drawSkeleton(ctx->vg, sk);
            return;
        }

        const float cx = getX() + getWidth() / 2;
        const float cy = getY() + getHeight() / 2;
        nvgSave(ctx->vg);
        // Glyphs stay at their resting size and scale as a texture: with a
        // 6% step the press dip (0.94) sat on a step edge and the text
        // flipped between two raster sizes.
        nvgTextScaleSnap(ctx->vg, 10.0f);
        nvgTranslate(ctx->vg, cx, cy);
        nvgScale(ctx->vg, s, s);
        nvgTranslate(ctx->vg, -cx, -cy);
        brls::Box::frame(ctx);
        if (sk > 0.0f)
            drawSkeleton(ctx->vg, sk);
        nvgRestore(ctx->vg);
    }

  private:
    brls::Animatable scale = 1.0f;
    bool pressing = false;

    brls::Animatable skeleton = 0.0f;
    // Labels under the skeleton. Card labels keep alpha 1 otherwise.
    std::vector<brls::Label*> skeletonLabels;

    void collectLabels(brls::View* view) {
        if (auto* label = dynamic_cast<brls::Label*>(view)) {
            skeletonLabels.push_back(label);
            return;
        }
        if (auto* box = dynamic_cast<brls::Box*>(view))
            for (brls::View* child : box->getChildren())
                collectLabels(child);
    }

    // The text fades out as the skeleton comes in, back in as it goes.
    void fadeLabels(float sk) {
        for (brls::Label* label : skeletonLabels)
            label->setAlpha(1.0f - sk);
        if (sk <= 0.0f)
            skeletonLabels.clear();
    }

    void drawSkeleton(NVGcontext* vg, float sk) {
        nvgFillColor(vg, nvgRGBAf(1, 1, 1, 0.12f * sk * getAlpha()));
        for (brls::Label* label : skeletonLabels) {
            const std::string text = label->getFullText();
            if (text.empty() || label->getVisibility() != brls::Visibility::VISIBLE ||
                label->getWidth() <= 0)
                continue;
            const float size = label->getFontSize();
            nvgFontFaceId(vg, label->getFont());
            nvgFontSize(vg, size);
            const float textW = nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, nullptr);
            const float w = std::min(textW, label->getWidth());
            const float line = std::min(label->getHeight(), size * label->getLineHeight());
            const float h = size * 0.7f;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, label->getX(), label->getY() + (line - h) / 2, w, h, h / 2);
            nvgFill(vg);
        }
    }
};
