//
//  cover_carousel.cpp
//  Equinox
//

#include "cover_carousel.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

using namespace brls;

namespace {
constexpr float kLift = 6; // focused cover sits this much higher
constexpr int kSlideMs = 460;
// Entering: the focused item grows a little on its way to the side.
constexpr int kEnterMs = 520;
constexpr float kEnterBump = 0.16f;
constexpr float kEnterBumpTime = 0.6f;
// Opacity of the entered item's ring while focus is on its options.
constexpr float kEnteredGlow = 0.55f;
// While the shelf moves, text is rasterized in 6% size steps: a new glyph
// size every frame would stall the slide.
constexpr float kTextSnap = 0.06f;

float seconds(Time t) { return (float)t / 1000000.0f; }
} // namespace

CoverCarousel::CoverCarousel(ShelfStyle style)
    : Box(Axis::ROW), shelf(style) {
    setHeight(shelf.height);
    setHideHighlight(true);

    // Swipe left/right to move through the shelf on the touch screen.
    addGestureRecognizer(new PanGestureRecognizer(
        [this](PanGestureStatus status, Sound* sound) {
            if (status.state != GestureState::END || entered)
                return;
            const float dx = status.position.x - status.startPosition.x;
            if (std::fabs(dx) < 50 || count() == 0)
                return;
            const int next = std::clamp(selected + (dx < 0 ? 1 : -1), 0,
                                        (int)count() - 1);
            if (next != selected)
                Application::giveFocus(getChildren()[next]);
        },
        PanAxis::HORIZONTAL));
}

void CoverCarousel::addView(View* view) {
    // Every cover sits at the same spot; draw() moves them with translation
    // so focus and touch areas follow the animation without relayouts.
    view->setPositionType(PositionType::ABSOLUTE);
    view->setPositionTop((shelf.height - view->getHeight()) / 2);
    view->setPositionLeft(0);
    view->setHideHighlight(true);
    // The pressed flash is a filled card: an object with no card only sinks.
    if (const auto* item = dynamic_cast<const ShelfItem*>(view))
        view->setHideClickAnimation(item->frameless());
    Box::addView(view);
}

void CoverCarousel::clearViews(bool free) {
    Box::clearViews(free);
    selected = 0;
    selectionTime = 0;
    position.reset(0.0f);
    entered = false;
    enter.reset(0.0f);
}

void CoverCarousel::setEntered(bool value, float offsetX) {
    if (value == entered)
        return;
    entered = value;
    if (value) {
        enterOffset = offsetX;
        enterTime = getCPUTimeUsec();
    }
    enter.reset(enter.getValue());
    enter.addStep(value ? 1.0f : 0.0f, kEnterMs, EasingFunction::cubicOut);
    enter.start();
}

void CoverCarousel::onChildFocusGained(View* directChild, View* focusedView) {
    Box::onChildFocusGained(directChild, focusedView);
    auto& children = getChildren();
    const auto it = std::find(children.begin(), children.end(), directChild);
    if (it != children.end())
        select((int)(it - children.begin()));
    if (!focused) {
        focused = true;
        focusGlow.reset(focusGlow.getValue());
        focusGlow.addStep(1.0f, 250, EasingFunction::quadraticOut);
        focusGlow.start();
    }
}

void CoverCarousel::onChildFocusLost(View* directChild, View* focusedView) {
    Box::onChildFocusLost(directChild, focusedView);
    focused = false;
    focusGlow.reset(focusGlow.getValue());
    focusGlow.addStep(0.0f, 250, EasingFunction::quadraticOut);
    focusGlow.start();
}

void CoverCarousel::select(int index) {
    if (index == selected && selectionTime != 0)
        return;
    selected = index;
    selectionTime = getCPUTimeUsec();
    position.reset(position.getValue());
    position.addStep((float)index, kSlideMs, EasingFunction::cubicOut);
    position.start();
    if (onSelect)
        onSelect(index);
}

void CoverCarousel::draw(NVGcontext* vg, float x, float y, float width,
                         float height, Style style, FrameContext* ctx) {
    auto& children = getChildren();
    if (children.empty())
        return;

    const Time now = getCPUTimeUsec();
    const float pos = position.getValue();
    const float e = enter.getValue();
    const float glow = std::max(focusGlow.getValue(), kEnteredGlow * e);
    const NVGcolor accent = Application::getTheme()["brls/accent"];

    float bump = 0;
    if (entered) {
        const float since = seconds(now - enterTime);
        if (since < kEnterBumpTime)
            bump = kEnterBump * std::sin(3.14159265f * since / kEnterBumpTime);
    }
    const bool moving = std::fabs(pos - (float)selected) > 0.0005f ||
                        (e > 0.0005f && e < 0.9995f) || bump != 0;

    // Covers more than ~4 steps away are fully faded out. Only the visible
    // window is sorted and drawn; the others just follow along, so a large
    // library costs nothing extra per frame.
    constexpr int kVisibleRange = 5;
    const int count = (int)children.size();
    const int lo = std::max(0, (int)std::floor(pos) - kVisibleRange);
    const int hi = std::min(count - 1, (int)std::ceil(pos) + kVisibleRange);
    for (int i = 0; i < count; i++) {
        if (i >= lo && i <= hi)
            continue;
        const float d = (float)i - pos;
        const float a = std::fabs(d);
        View* cell = children[i];
        cell->setTranslationX(width / 2 - cell->getWidth() / 2 +
                              (d < 0 ? -1.0f : 1.0f) *
                                  (shelf.firstStep + (a - 1) * shelf.step));
        cell->setTranslationY(0);
    }

    // Far covers first, the focused one last (on top).
    drawOrder.clear();
    for (int i = lo; i <= hi; i++)
        drawOrder.push_back((size_t)i);
    std::sort(drawOrder.begin(), drawOrder.end(), [&](size_t a, size_t b) {
        return std::fabs((float)a - pos) > std::fabs((float)b - pos);
    });

    for (size_t i : drawOrder) {
        View* cell = children[i];
        const float cw = cell->getWidth();
        const float ch = cell->getHeight();
        const float d = (float)i - pos;
        const float a = std::fabs(d);
        const float side = d < 0 ? -1.0f : 1.0f;
        // 1 for the focused cover, fading to 0 one step away.
        const float focus = std::max(0.0f, 1.0f - a);

        float offset = side * (a < 1 ? a * shelf.firstStep
                                     : shelf.firstStep + (a - 1) * shelf.step);
        float scale =
            a < 1 ? shelf.focusScale + (shelf.neighbourScale - shelf.focusScale) * a
                  : std::max(shelf.minScale,
                             shelf.neighbourScale - (a - 1) * shelf.scaleStep);
        // Neighbours look turned towards the centre: a little narrower.
        const float squash = 1.0f - std::min(a, 1.0f) * shelf.squash;
        float alpha = a < 1 ? 1.0f - a * (1.0f - shelf.neighbourAlpha)
                            : std::max(0.0f, shelf.neighbourAlpha -
                                                 (a - 1) * shelf.alphaStep);
        float lift = -kLift * focus;

        if (e > 0.001f) {
            if ((int)i == selected) {
                offset += (enterOffset - offset) * e;
                scale += (1.0f - scale) * e + bump;
                lift = lift * (1 - e) - 8 * e;
            } else {
                offset = offset * (1 + 0.6f * e) + side * 300 * e;
                scale *= 1 - 0.5f * e;
                alpha *= 1 - e;
            }
        }

        cell->setTranslationX(width / 2 - cw / 2 + offset);
        cell->setTranslationY(lift);
        // Also keeps faded-out items out of hit tests.
        cell->setAlpha(alpha);
        if (alpha <= 0.01f)
            continue;

        const float cx = x + width / 2 + offset;
        const float cy = cell->getY() + ch / 2;
        const float sw = cw * scale * squash;
        const float sh = ch * scale;
        const auto* item = dynamic_cast<const ShelfItem*>(cell);
        const bool frameless = item && item->frameless();

        // Shadow on the ground: softer on the light theme's white ground.
        const float groundShade =
            Application::getThemeVariant() == ThemeVariant::LIGHT ? 70 : 150;
        const float groundY = y + (shelf.height + ch * shelf.focusScale) / 2 + 4;
        const float gw = sw * 0.42f;
        nvgSave(vg);
        nvgTranslate(vg, cx, groundY);
        nvgScale(vg, 1.0f, 0.16f);
        nvgBeginPath(vg);
        nvgCircle(vg, 0, 0, gw);
        nvgFillPaint(vg, nvgRadialGradient(vg, 0, 0, 0, gw,
                                           nvgRGBA(0, 0, 0, (unsigned char)(groundShade * alpha)),
                                           nvgRGBA(0, 0, 0, 0)));
        nvgFill(vg);
        nvgRestore(vg);

        // Amber glow behind the focused cover.
        if (!frameless && focus > 0.01f && glow > 0.01f) {
            const float k = focus * glow;
            nvgBeginPath(vg);
            nvgRect(vg, cx - sw / 2 - 40, cy - sh / 2 - 40, sw + 80, sh + 80);
            nvgFillPaint(vg, nvgBoxGradient(vg, cx - sw / 2, cy - sh / 2, sw,
                                            sh, shelf.radius * scale, 22,
                                            nvgRGBAf(accent.r, accent.g, accent.b, 0.2f * k),
                                            nvgRGBAf(accent.r, accent.g, accent.b, 0)));
            nvgFill(vg);
        }

        nvgSave(vg);
        if (moving)
            nvgTextScaleSnap(vg, kTextSnap);
        nvgTranslate(vg, cx, cy);
        nvgScale(vg, scale * squash, scale);
        nvgTranslate(vg, -cx, -cy);

        cell->frame(ctx);
        if (frameless) {
            nvgRestore(vg);
            continue;
        }

        const float left = cx - cw / 2;
        const float top = cy - ch / 2;

        // Dim the covers away from the centre.
        const float dim = std::min(a, 1.0f) * shelf.dim;
        if (dim > 0.01f) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, left, top, cw, ch, shelf.radius);
            nvgFillColor(vg, nvgRGBAf(0.04f, 0.05f, 0.11f, dim * alpha));
            nvgFill(vg);
        }

        if (shelf.edge > 0) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, left + 0.5f, top + 0.5f, cw - 1, ch - 1,
                           shelf.radius);
            nvgStrokeColor(vg, nvgRGBAf(0.62f, 0.72f, 1.0f, shelf.edge * alpha));
            nvgStrokeWidth(vg, 1.5f / scale);
            nvgStroke(vg);
        }

        if (focus > 0.5f) {
            // Focus ring.
            if (glow > 0.01f) {
                nvgBeginPath(vg);
                nvgRoundedRect(vg, left - 2, top - 2, cw + 4, ch + 4,
                               shelf.radius + 2);
                nvgStrokeColor(vg, nvgRGBAf(accent.r, accent.g, accent.b, glow * focus));
                nvgStrokeWidth(vg, 3.0f / scale);
                nvgStroke(vg);
            }
        }
        nvgRestore(vg);
    }
}

CarouselDots::CarouselDots() {
    setHeight(10);
    setWidth(View::AUTO);
}

void CarouselDots::setState(size_t count, int index) {
    total = count;
    current = index;
    slide.reset(slide.getValue());
    slide.addStep((float)index, 300, EasingFunction::cubicOut);
    slide.start();
}

void CarouselDots::draw(NVGcontext* vg, float x, float y, float width,
                        float height, Style style, FrameContext* ctx) {
    if (total < 2)
        return;
    constexpr float dot = 7, wide = 22, gap = 7;
    const float pos = slide.getValue();
    // On the light ground the separator line is too faint for a dot.
    const NVGcolor idle =
        Application::getTheme()[Application::getThemeVariant() == ThemeVariant::LIGHT
                                    ? "brls/slider/pointer_border_color"
                                    : "brls/applet_frame/separator"];
    const NVGcolor accent = Application::getTheme()["brls/accent"];

    float full = 0;
    std::vector<float> widths(total);
    for (size_t i = 0; i < total; i++) {
        const float k = std::max(0.0f, 1.0f - std::fabs((float)i - pos));
        widths[i] = dot + (wide - dot) * k;
        full += widths[i] + (i ? gap : 0);
    }
    float cx = x + (width - full) / 2;
    for (size_t i = 0; i < total; i++) {
        const float k = std::max(0.0f, 1.0f - std::fabs((float)i - pos));
        nvgBeginPath(vg);
        nvgRoundedRect(vg, cx, y + (height - dot) / 2, widths[i], dot, dot / 2);
        nvgFillColor(vg, nvgLerpRGBA(idle, accent, k));
        nvgFill(vg);
        cx += widths[i] + gap;
    }
}
