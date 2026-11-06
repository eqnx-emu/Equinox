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
constexpr float kHeight = 340;
// Scale of the focused cover and of its direct neighbours.
constexpr float kFocusScale = 1.32f;
constexpr float kNeighbourScale = 0.84f;
constexpr float kMinScale = 0.62f;
// Distance from the centre to the first neighbour, then between neighbours.
constexpr float kFirstStep = 190;
constexpr float kStep = 132;
constexpr float kLift = 6;        // focused cover sits this much higher
constexpr float kFloat = 2.5f;      // amplitude of the floating motion
constexpr float kFloatPeriod = 4.8f;
constexpr int kSlideMs = 460;
// Reflection that sweeps over a newly focused cover.
constexpr float kShineDelay = 0.18f;
constexpr float kShineTime = 0.9f;
constexpr float kRadius = 12;

float seconds(Time t) { return (float)t / 1000000.0f; }
} // namespace

CoverCarousel::CoverCarousel() : Box(Axis::ROW) {
    setHeight(kHeight);
    setHideHighlight(true);

    // Swipe left/right to move through the shelf on the touch screen.
    addGestureRecognizer(new PanGestureRecognizer(
        [this](PanGestureStatus status, Sound* sound) {
            if (status.state != GestureState::END)
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
    view->setPositionTop((kHeight - view->getHeight()) / 2);
    view->setPositionLeft(0);
    view->setHideHighlight(true);
    Box::addView(view);
}

void CoverCarousel::clearViews(bool free) {
    Box::clearViews(free);
    selected = 0;
    position.reset(0.0f);
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
    const float t = seconds(now);
    const float pos = position.getValue();
    const float glow = focusGlow.getValue();
    const NVGcolor accent = Application::getTheme()["brls/accent"];

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
                                  (kFirstStep + (a - 1) * kStep));
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

        const float offset =
            side * (a < 1 ? a * kFirstStep : kFirstStep + (a - 1) * kStep);
        const float scale =
            a < 1 ? kFocusScale + (kNeighbourScale - kFocusScale) * a
                  : std::max(kMinScale, kNeighbourScale - (a - 1) * 0.1f);
        // Neighbours look turned towards the centre: a little narrower.
        const float squash = 1.0f - std::min(a, 1.0f) * 0.1f;
        const float alpha =
            a < 1 ? 1.0f - a * 0.3f : std::max(0.0f, 0.7f - (a - 1) * 0.22f);
        const float bob = std::sin(t * 6.2831853f / kFloatPeriod);
        const float lift = -(kLift + kFloat * bob) * focus;

        cell->setTranslationX(width / 2 - cw / 2 + offset);
        cell->setTranslationY(lift);
        if (alpha <= 0.01f)
            continue;

        const float cx = x + width / 2 + offset;
        const float cy = cell->getY() + ch / 2;
        const float sw = cw * scale * squash;
        const float sh = ch * scale;

        // Shadow on the ground; smaller while the cover floats up.
        const float groundY = y + (kHeight + ch * kFocusScale) / 2 + 4;
        const float gw = sw * (0.42f - 0.06f * bob * focus);
        nvgSave(vg);
        nvgTranslate(vg, cx, groundY);
        nvgScale(vg, 1.0f, 0.16f);
        nvgBeginPath(vg);
        nvgCircle(vg, 0, 0, gw);
        nvgFillPaint(vg, nvgRadialGradient(vg, 0, 0, 0, gw,
                                           nvgRGBA(0, 0, 0, (unsigned char)(150 * alpha)),
                                           nvgRGBA(0, 0, 0, 0)));
        nvgFill(vg);
        nvgRestore(vg);

        // Amber glow behind the focused cover.
        if (focus > 0.01f && glow > 0.01f) {
            const float k = focus * glow;
            nvgBeginPath(vg);
            nvgRect(vg, cx - sw / 2 - 40, cy - sh / 2 - 40, sw + 80, sh + 80);
            nvgFillPaint(vg, nvgBoxGradient(vg, cx - sw / 2, cy - sh / 2, sw,
                                            sh, kRadius * scale, 22,
                                            nvgRGBAf(accent.r, accent.g, accent.b, 0.2f * k),
                                            nvgRGBAf(accent.r, accent.g, accent.b, 0)));
            nvgFill(vg);
        }

        nvgSave(vg);
        nvgTranslate(vg, cx, cy);
        nvgScale(vg, scale * squash, scale);
        nvgTranslate(vg, -cx, -cy);

        cell->setAlpha(alpha);
        cell->frame(ctx);

        const float left = cx - cw / 2;
        const float top = cy - ch / 2;

        // Dim the covers away from the centre.
        const float dim = std::min(a, 1.0f) * 0.35f;
        if (dim > 0.01f) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, left, top, cw, ch, kRadius);
            nvgFillColor(vg, nvgRGBAf(0.04f, 0.05f, 0.11f, dim * alpha));
            nvgFill(vg);
        }

        if (focus > 0.5f) {
            // One sweep of light after the cover becomes the focused one.
            const float s = seconds(now - selectionTime) - kShineDelay;
            if (s > 0 && s < kShineTime) {
                const float p = s / kShineTime;
                const float eased = 1.0f - (1.0f - p) * (1.0f - p);
                const float band = left - cw * 0.6f + eased * cw * 2.2f;
                nvgSave(vg);
                nvgIntersectScissor(vg, left, top, cw, ch);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, left, top, cw, ch, kRadius);
                // Two halves give a soft bright band in the middle.
                nvgFillPaint(vg, nvgLinearGradient(vg, band - cw * 0.22f, top,
                                                   band, top + ch * 0.18f,
                                                   nvgRGBA(255, 255, 255, 0),
                                                   nvgRGBA(255, 255, 255, 40)));
                nvgFill(vg);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, left, top, cw, ch, kRadius);
                nvgFillPaint(vg, nvgLinearGradient(vg, band, top + ch * 0.18f,
                                                   band + cw * 0.22f, top + ch * 0.36f,
                                                   nvgRGBA(255, 255, 255, 40),
                                                   nvgRGBA(255, 255, 255, 0)));
                nvgFill(vg);
                nvgRestore(vg);
            }

            // Focus ring.
            if (glow > 0.01f) {
                nvgBeginPath(vg);
                nvgRoundedRect(vg, left - 2, top - 2, cw + 4, ch + 4, kRadius + 2);
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
    const NVGcolor idle = Application::getTheme()["brls/applet_frame/separator"];
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
