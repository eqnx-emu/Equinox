//
//  cartridge_art.cpp
//  Equinox
//

#include "cartridge_art.hpp"

#if defined(EQX_EMU_UI)

#include <algorithm>

namespace {
constexpr float kBaseW = 240;
constexpr float kBaseH = 140;
constexpr float kTopRadius = 12;
constexpr float kBottomRadius = 5;
constexpr float kRingGap = 3;
constexpr float kRingWidth = 3;

NVGcolor black(float alpha) { return nvgRGBAf(0, 0, 0, alpha); }
NVGcolor white(float alpha) { return nvgRGBAf(1, 1, 1, alpha); }

NVGcolor faded(NVGcolor c, float alpha) {
    c.a *= alpha;
    return c;
}

void bodyPath(NVGcontext* vg, float x, float y, float w, float h, float s,
              float grow) {
    const float top = kTopRadius * s + grow;
    const float bottom = kBottomRadius * s + grow;
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, x - grow, y - grow, w + 2 * grow, h + 2 * grow,
                          top, top, bottom, bottom);
}

void ridges(NVGcontext* vg, float x, float y, float w, float h, float s) {
    nvgSave(vg);
    nvgIntersectScissor(vg, x, y, w, h);
    const float period = 9 * s;
    for (float top = y; top < y + h; top += period) {
        nvgBeginPath(vg);
        nvgRect(vg, x, top, w, 2 * s);
        nvgFillColor(vg, black(0.22f));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRect(vg, x, top + 2 * s, w, 2 * s);
        nvgFillColor(vg, white(0.2f));
        nvgFill(vg);
    }
    nvgRestore(vg);
}
} // namespace

void drawCartridge(NVGcontext* vg, float x, float y, float w, float h,
                   int coverTexture, int coverW, int coverH, NVGcolor body,
                   NVGcolor focusRing, NVGcolor focusGap, float focusAlpha) {
    const float sx = w / kBaseW;
    const float sy = h / kBaseH;
    const float s = sx;

    nvgSave(vg);

    // Drop shadow.
    nvgBeginPath(vg);
    nvgRect(vg, x - 40 * s, y - 40 * s, w + 80 * s, h + 100 * s);
    nvgFillPaint(vg, nvgBoxGradient(vg, x, y + 14 * sy, w, h,
                                    kTopRadius * s, 24 * s, black(0.45f),
                                    black(0)));
    nvgFill(vg);

    if (focusAlpha > 0.01f) {
        const float gap = kRingGap * s;
        const float ring = kRingWidth * s;
        bodyPath(vg, x, y, w, h, s, gap / 2);
        nvgStrokeWidth(vg, gap);
        nvgStrokeColor(vg, faded(focusGap, focusAlpha));
        nvgStroke(vg);
        bodyPath(vg, x, y, w, h, s, gap + ring / 2);
        nvgStrokeWidth(vg, ring);
        nvgStrokeColor(vg, faded(focusRing, focusAlpha));
        nvgStroke(vg);
    }

    bodyPath(vg, x, y, w, h, s, 0);
    nvgFillColor(vg, body);
    nvgFill(vg);

    // Reflection along the top edge and shade along the bottom edge.
    nvgSave(vg);
    nvgIntersectScissor(vg, x, y, w, 2 * sy);
    bodyPath(vg, x, y, w, h, s, 0);
    nvgFillColor(vg, white(0.45f));
    nvgFill(vg);
    nvgRestore(vg);
    nvgSave(vg);
    nvgIntersectScissor(vg, x, y + h - 3 * sy, w, 3 * sy);
    bodyPath(vg, x, y, w, h, s, 0);
    nvgFillColor(vg, black(0.25f));
    nvgFill(vg);
    nvgRestore(vg);

    // Top notch.
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, x + 20 * sx, y, w - 40 * sx, 10 * sy, 0, 0,
                          6 * s, 6 * s);
    nvgFillColor(vg, black(0.14f));
    nvgFill(vg);

    const float rowY = y + 15 * sy;
    const float rowH = h - 36 * sy;
    const float slotW = 26 * sx;
    const float labelX = x + 40 * sx;
    const float labelW = w - 80 * sx;
    ridges(vg, x + 7 * sx, rowY, slotW, rowH, s);
    ridges(vg, x + w - 7 * sx - slotW, rowY, slotW, rowH, s);

    // Label: a light edge under it, the cover cropped to fill, an inner
    // frame and the gloss on the top half.
    const float labelR = 4 * s;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, labelX, rowY + 1 * sy, labelW, rowH, labelR);
    nvgFillColor(vg, white(0.5f));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, labelX, rowY, labelW, rowH, labelR);
    nvgFillColor(vg, black(1));
    nvgFill(vg);
    if (coverTexture > 0 && coverW > 0 && coverH > 0) {
        const float fit = std::max(labelW / coverW, rowH / coverH);
        const float iw = coverW * fit;
        const float ih = coverH * fit;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, labelX, rowY, labelW, rowH, labelR);
        nvgFillPaint(vg, nvgImagePattern(vg, labelX + (labelW - iw) / 2,
                                         rowY + (rowH - ih) / 2, iw, ih, 0,
                                         coverTexture, 1));
        nvgFill(vg);
    }
    nvgBeginPath(vg);
    nvgRoundedRect(vg, labelX + s, rowY + s, labelW - 2 * s, rowH - 2 * s,
                   labelR);
    nvgStrokeWidth(vg, 2 * s);
    nvgStrokeColor(vg, black(0.35f));
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, labelX, rowY, labelW, rowH * 0.4f, labelR,
                          labelR, 0, 0);
    nvgFillColor(vg, white(0.07f));
    nvgFill(vg);

    // Bottom strip with its arrow.
    const float stripH = 16 * sy;
    const float stripY = y + h - stripH;
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, x, stripY, w, stripH, 0, 0, kBottomRadius * s,
                          kBottomRadius * s);
    nvgFillColor(vg, black(0.08f));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, x, stripY, w, std::max(1.0f, s));
    nvgFillColor(vg, black(0.18f));
    nvgFill(vg);
    const float cx = x + w / 2;
    const float ay = stripY + (stripH - 7 * s) / 2 + 0.5f * s;
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx - 6 * s, ay);
    nvgLineTo(vg, cx + 6 * s, ay);
    nvgLineTo(vg, cx, ay + 7 * s);
    nvgClosePath(vg);
    nvgFillColor(vg, black(0.3f));
    nvgFill(vg);

    nvgRestore(vg);
}

#endif // EQX_EMU_UI
