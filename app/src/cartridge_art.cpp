//
//  cartridge_art.cpp
//  Equinox
//

#include "cartridge_art.hpp"

#if defined(EQX_EMU_UI)

#include <algorithm>

namespace {
// Everything below is drawn in these units and scaled to the box.
constexpr float kW = 240;
constexpr float kH = 176;
constexpr float kTopRadius = 16;
constexpr float kBottomRadius = 5;
// The lower part, which goes into the console, is narrower.
constexpr float kWaistTop = 130;
constexpr float kWaistBottom = 142;
constexpr float kWaistInset = 9;

constexpr float kLabelX = 46;
constexpr float kLabelY = 17;
constexpr float kLabelW = kW - 2 * kLabelX;
constexpr float kLabelH = 96;
constexpr float kWell = 4; // the recess around the label

constexpr float kGripX = 13;
constexpr float kGripW = 24;
constexpr float kGripTop = 20;
constexpr float kGripBottom = 112;

constexpr float kRingGap = 3;
constexpr float kRingWidth = 3;

NVGcolor black(float alpha) { return nvgRGBAf(0, 0, 0, alpha); }
NVGcolor white(float alpha) { return nvgRGBAf(1, 1, 1, alpha); }

NVGcolor faded(NVGcolor c, float alpha) {
    c.a *= alpha;
    return c;
}

NVGcolor shade(NVGcolor c, float amount) {
    const auto mix = [amount](float v) {
        return amount >= 0 ? v + (1 - v) * amount : v * (1 + amount);
    };
    return nvgRGBAf(mix(c.r), mix(c.g), mix(c.b), c.a);
}

void shellPath(NVGcontext* vg) {
    const float r = kTopRadius;
    const float b = kBottomRadius;
    const float in = kWaistInset;
    nvgBeginPath(vg);
    nvgMoveTo(vg, 0, r);
    nvgArcTo(vg, 0, 0, r, 0, r);
    nvgLineTo(vg, kW - r, 0);
    nvgArcTo(vg, kW, 0, kW, r, r);
    nvgLineTo(vg, kW, kWaistTop);
    nvgBezierTo(vg, kW, kWaistTop + 7, kW - in, kWaistBottom - 7, kW - in, kWaistBottom);
    nvgLineTo(vg, kW - in, kH - b);
    nvgArcTo(vg, kW - in, kH, kW - in - b, kH, b);
    nvgLineTo(vg, in + b, kH);
    nvgArcTo(vg, in, kH, in, kH - b, b);
    nvgLineTo(vg, in, kWaistBottom);
    nvgBezierTo(vg, in, kWaistBottom - 7, 0, kWaistTop + 7, 0, kWaistTop);
    nvgClosePath(vg);
}

// Fills the part of the shell inside the rectangle.
void shellBand(NVGcontext* vg, float x, float y, float w, float h, NVGpaint paint) {
    nvgSave(vg);
    nvgIntersectScissor(vg, x, y, w, h);
    shellPath(vg);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
    nvgRestore(vg);
}

void grip(NVGcontext* vg, float x) {
    // A shallow channel with ridges across it.
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, kGripTop, kGripW, kGripBottom - kGripTop, 4);
    nvgFillColor(vg, black(0.07f));
    nvgFill(vg);
    nvgSave(vg);
    nvgIntersectScissor(vg, x + 1, kGripTop + 3, kGripW - 2, kGripBottom - kGripTop - 6);
    for (float top = kGripTop + 4; top < kGripBottom - 4; top += 6) {
        nvgBeginPath(vg);
        nvgRect(vg, x + 2, top, kGripW - 4, 1.6f);
        nvgFillColor(vg, black(0.26f));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRect(vg, x + 2, top + 1.6f, kGripW - 4, 1.4f);
        nvgFillColor(vg, white(0.32f));
        nvgFill(vg);
    }
    nvgRestore(vg);
}

void label(NVGcontext* vg, int tex, int texW, int texH) {
    // The recess: darker walls, lit from above, so its lower lip catches light.
    nvgBeginPath(vg);
    nvgRoundedRect(vg, kLabelX - kWell, kLabelY - kWell, kLabelW + 2 * kWell,
                   kLabelH + 2 * kWell, 6);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0, kLabelY - kWell, 0, kLabelY + kLabelH + kWell,
                                       black(0.24f), black(0.06f)));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, kLabelX - kWell + 4, kLabelY + kLabelH + kWell - 1, kLabelW + 2 * kWell - 8, 1.2f);
    nvgFillColor(vg, white(0.45f));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, kLabelX, kLabelY, kLabelW, kLabelH, 2.5f);
    nvgFillColor(vg, nvgRGB(0x26, 0x26, 0x2c));
    nvgFill(vg);
    if (tex > 0 && texW > 0 && texH > 0) {
        const float fit = std::max(kLabelW / texW, kLabelH / texH);
        const float iw = texW * fit;
        const float ih = texH * fit;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, kLabelX, kLabelY, kLabelW, kLabelH, 2.5f);
        nvgFillPaint(vg, nvgImagePattern(vg, kLabelX + (kLabelW - iw) / 2,
                                         kLabelY + (kLabelH - ih) / 2, iw, ih, 0, tex, 1));
        nvgFill(vg);
    }
    // The paper's edge and a soft sheen across its top.
    nvgBeginPath(vg);
    nvgRoundedRect(vg, kLabelX + 0.5f, kLabelY + 0.5f, kLabelW - 1, kLabelH - 1, 2.5f);
    nvgStrokeWidth(vg, 1);
    nvgStrokeColor(vg, black(0.35f));
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, kLabelX, kLabelY, kLabelW, kLabelH, 2.5f);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0, kLabelY, 0, kLabelY + kLabelH * 0.5f, white(0.12f),
                                       white(0)));
    nvgFill(vg);
}

void arrow(NVGcontext* vg) {
    // Moulded into the shell: the shadow sits above, the lit edge below.
    const float cx = kW / 2;
    const float top = kLabelY + kLabelH + kWell + 12;
    const auto tri = [&](float dy) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 11, top + dy);
        nvgLineTo(vg, cx + 11, top + dy);
        nvgLineTo(vg, cx, top + 12 + dy);
        nvgClosePath(vg);
    };
    tri(1.2f);
    nvgFillColor(vg, white(0.5f));
    nvgFill(vg);
    tri(0);
    nvgFillColor(vg, black(0.2f));
    nvgFill(vg);
}
} // namespace

void drawCartridge(NVGcontext* vg, float x, float y, float w, float h, int coverTexture,
                   int coverW, int coverH, NVGcolor body, NVGcolor focusRing, NVGcolor focusGap,
                   float focusAlpha, float shadow) {
    const float s = std::min(w / kW, h / kH);

    nvgSave(vg);
    nvgTranslate(vg, x + (w - kW * s) / 2, y + (h - kH * s) / 2);
    nvgScale(vg, s, s);

    if (shadow > 0.01f) {
        nvgBeginPath(vg);
        nvgRect(vg, -40, -30, kW + 80, kH + 90);
        nvgFillPaint(vg, nvgBoxGradient(vg, 4, 14, kW - 8, kH - 4, kTopRadius, 22,
                                        black(0.42f * shadow), black(0)));
        nvgFill(vg);
    }

    // The ring is the outline stroked wide behind the shell: only its outer
    // half shows. Strokes are in screen pixels, so they undo the scale.
    if (focusAlpha > 0.01f) {
        shellPath(vg);
        nvgStrokeWidth(vg, 2 * (kRingGap + kRingWidth) / s);
        nvgStrokeColor(vg, faded(focusRing, focusAlpha));
        nvgStroke(vg);
        shellPath(vg);
        nvgStrokeWidth(vg, 2 * kRingGap / s);
        nvgStrokeColor(vg, faded(focusGap, focusAlpha));
        nvgStroke(vg);
    }

    shellPath(vg);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 0, kH, shade(body, 0.08f), shade(body, -0.06f)));
    nvgFill(vg);

    // Light along the top edge, the ridge under it, and the shell's
    // thickness along the bottom.
    shellBand(vg, 0, 0, kW, 2, nvgLinearGradient(vg, 0, 0, 0, 2, white(0.6f), white(0.6f)));
    nvgBeginPath(vg);
    nvgRect(vg, kTopRadius, 8, kW - 2 * kTopRadius, 1.2f);
    nvgFillColor(vg, black(0.16f));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, kTopRadius, 9.2f, kW - 2 * kTopRadius, 1);
    nvgFillColor(vg, white(0.4f));
    nvgFill(vg);
    shellBand(vg, 0, kH - 5, kW, 5, nvgLinearGradient(vg, 0, kH - 5, 0, kH, black(0.1f), black(0.3f)));
    // The waist's curve, shaded where it turns inwards.
    shellBand(vg, 0, kWaistTop, kW, kWaistBottom - kWaistTop + 2,
              nvgLinearGradient(vg, 0, kWaistTop, 0, kWaistBottom + 2, black(0), black(0.08f)));

    grip(vg, kGripX);
    grip(vg, kW - kGripX - kGripW);
    label(vg, coverTexture, coverW, coverH);
    arrow(vg);

    nvgRestore(vg);
}

#endif // EQX_EMU_UI
