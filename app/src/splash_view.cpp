//
//  splash_view.cpp
//  Equinox
//

#include "splash_view.hpp"

#include <algorithm>
#include <cmath>

using namespace brls;

namespace {
// Timeline, in seconds from the first drawn frame.
constexpr float kLogoIn = 0.10f, kLogoInTime = 0.50f;
constexpr float kSunGlow = 0.20f, kMoonGlow = 0.32f, kGlowTime = 1.50f;
constexpr float kShine = 0.30f, kShineTime = 1.10f;
constexpr float kOpen = 1.25f, kOpenTime = 0.72f;
constexpr float kHomeIn = 1.35f, kHomeInTime = 0.80f;
constexpr float kEnd = 2.15f;
constexpr float kLogoSize = 0.20f; // of the screen width
constexpr float kPi = 3.14159265f;

const NVGcolor kDusk = nvgRGB(22, 26, 51);
const NVGcolor kSun = nvgRGB(244, 184, 96);
const NVGcolor kMoon = nvgRGB(157, 184, 255);

float progress(float t, float from, float length) {
    return std::clamp((t - from) / length, 0.0f, 1.0f);
}
float cubicOut(float k) { return 1 - std::pow(1 - k, 3.0f); }
float inOut(float k) {
    return k < 0.5f ? 4 * k * k * k : 1 - std::pow(-2 * k + 2, 3.0f) / 2;
}

// A glow over its own time: grows in, holds, then fades while spreading out.
void glowAt(float k, float& alpha, float& scale) {
    alpha = 0;
    scale = 1;
    if (k <= 0 || k >= 1)
        return;
    if (k < 0.45f) {
        const float e = cubicOut(k / 0.45f);
        alpha = e;
        scale = 0.45f + 0.6f * e;
    } else if (k < 0.7f) {
        const float e = (k - 0.45f) / 0.25f;
        alpha = 1 - 0.15f * e;
        scale = 1.05f - 0.05f * e;
    } else {
        const float e = (k - 0.7f) / 0.3f;
        alpha = 0.85f * (1 - e);
        scale = 1 + 0.25f * e;
    }
}

NVGcolor withAlpha(NVGcolor c, float alpha) {
    c.a = alpha;
    return c;
}

NVGcolor lit(NVGcolor c, float shine, float alpha) {
    return withAlpha(nvgLerpRGBA(c, nvgRGB(255, 255, 255), shine), alpha);
}

void drawGlow(NVGcontext* vg, float cx, float cy, float size, NVGcolor color,
              NVGcolor core, float alpha, float scale) {
    if (alpha <= 0.01f)
        return;
    const float r = size * 1.45f * scale;
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r * 0.6f);
    nvgFillPaint(vg, nvgRadialGradient(vg, cx, cy, 0, r * 0.6f,
                                       withAlpha(color, 0.45f * alpha),
                                       withAlpha(color, 0)));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r * 0.3f);
    nvgFillPaint(vg, nvgRadialGradient(vg, cx, cy, 0, r * 0.3f,
                                       withAlpha(core, 0.6f * alpha),
                                       withAlpha(core, 0)));
    nvgFill(vg);
}

// The two halves of art/logo/equinox.svg, on its 100-unit grid.
void drawSun(NVGcontext* vg, NVGcolor color) {
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, 5);
    nvgLineCap(vg, NVG_ROUND);
    const float rays[5][4] = {{14, 50, 4, 50},
                              {21.7f, 27, 14.6f, 19.9f},
                              {21.7f, 73, 14.6f, 80.1f},
                              {47, 10, 47, 2},
                              {47, 90, 47, 98}};
    for (const auto& ray : rays) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, ray[0], ray[1]);
        nvgLineTo(vg, ray[2], ray[3]);
        nvgStroke(vg);
    }
    nvgBeginPath(vg);
    nvgArc(vg, 47, 50, 30, -kPi / 2, kPi / 2, NVG_CCW);
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void drawMoon(NVGcontext* vg, NVGcolor color, float alpha) {
    nvgBeginPath(vg);
    nvgArc(vg, 53, 50, 30, -kPi / 2, kPi / 2, NVG_CW);
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgCircle(vg, 62, 38, 4);
    nvgFillColor(vg, withAlpha(kDusk, 0.35f * alpha));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, 66, 60, 6);
    nvgFillColor(vg, withAlpha(kDusk, 0.25f * alpha));
    nvgFill(vg);

    const float star[8][2] = {{88, 22},       {89.6f, 26},    {93.6f, 27.6f},
                              {89.6f, 29.2f}, {88, 33.2f},    {86.4f, 29.2f},
                              {82.4f, 27.6f}, {86.4f, 26}};
    nvgBeginPath(vg);
    nvgMoveTo(vg, star[0][0], star[0][1]);
    for (int i = 1; i < 8; i++)
        nvgLineTo(vg, star[i][0], star[i][1]);
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, 92, 72, 2.2f);
    nvgFill(vg);
}
} // namespace

SplashView::SplashView() {
    setWidth(Application::contentWidth);
    setHeight(Application::contentHeight);
    setFocusable(true);
    setHideHighlight(true);

    // Any button skips the animation.
    for (int b = 0; b < (int)_BUTTON_MAX; b++)
        registerAction(
            "", (ControllerButton)b,
            [this](View*) {
                finish();
                return true;
            },
            true);
    addGestureRecognizer(new TapGestureRecognizer(this, [this]() { finish(); }));
}

void SplashView::show() {
    Application::pushActivity(new Activity(new SplashView()),
                              TransitionAnimation::NONE);
}

void SplashView::finish() {
    if (closing)
        return;
    closing = true;
    // Not from inside draw or an action: the view would free itself mid-call.
    brls::sync([] { Application::popActivity(TransitionAnimation::NONE); });
}

void SplashView::draw(NVGcontext* vg, float x, float y, float width,
                      float height, Style style, FrameContext* ctx) {
    const Time now = getCPUTimeUsec();
    // Counted from the first frame, so a slow start doesn't eat the animation.
    if (start == 0)
        start = now;
    const float t = (float)(now - start) / 1000000.0f;
    if (t >= kEnd)
        finish();

    // The home fades in behind the doors as they open.
    const float home = cubicOut(progress(t, kHomeIn, kHomeInTime));
    if (home < 1) {
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, withAlpha(kDusk, 1 - home));
        nvgFill(vg);
    }

    const float open = inOut(progress(t, kOpen, kOpenTime));
    if (open >= 1)
        return;
    const float half = width / 2;
    const float logoIn = cubicOut(progress(t, kLogoIn, kLogoInTime));
    const float size = width * kLogoSize * (0.82f + 0.18f * logoIn);
    const float cy = y + height / 2;
    // Brightens the logo a little at the peak of the glow.
    const float shine = 0.18f * std::sin(kPi * progress(t, kShine, kShineTime));

    float sunAlpha, sunScale, moonAlpha, moonScale;
    glowAt(progress(t, kSunGlow, kGlowTime), sunAlpha, sunScale);
    glowAt(progress(t, kMoonGlow, kGlowTime), moonAlpha, moonScale);

    for (int side = 0; side < 2; side++) {
        const bool sun = side == 0;
        const float shift = (sun ? -1.0f : 1.0f) * open * half;
        const float doorX = x + (sun ? 0 : half) + shift;
        // The logo centre sits on the inner edge of each door.
        const float cx = x + half + shift;

        nvgSave(vg);
        nvgIntersectScissor(vg, doorX, y, half, height);

        nvgBeginPath(vg);
        nvgRect(vg, doorX, y, half, height);
        nvgFillColor(vg, kDusk);
        nvgFill(vg);

        if (sun)
            drawGlow(vg, cx, cy, size, kSun, nvgRGB(255, 213, 154), sunAlpha,
                     sunScale);
        else
            drawGlow(vg, cx, cy, size, kMoon, nvgRGB(223, 231, 255), moonAlpha,
                     moonScale);

        if (logoIn > 0.001f) {
            nvgTranslate(vg, cx - size / 2, cy - size / 2);
            nvgScale(vg, size / 100, size / 100);
            if (sun)
                drawSun(vg, lit(kSun, shine, logoIn));
            else
                drawMoon(vg, lit(kMoon, shine, logoIn), logoIn);
        }
        nvgRestore(vg);
    }
}
