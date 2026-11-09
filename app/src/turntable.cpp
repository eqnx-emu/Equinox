//
//  turntable.cpp
//  Equinox
//

#include "turntable.hpp"

#include "mesh_raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace brls;

namespace {
constexpr int kFrames = 72;
constexpr int kCols = 12;
constexpr float kTurnSeconds = 6.0f;
constexpr float kBobSeconds = 2.6f;
constexpr float kRockSeconds = 4.2f;
constexpr float kRockDegrees = 9.0f;
constexpr float kLiveMargin = 1.3f; // room for poses past the still's framing

// The Switch's CPU takes long enough to draw these that doing it during the
// shelf's slide makes the slide stutter. The objects hold still while
// selection changes (they keep moving as images) and carry on after it.
// Matches kSlideMs in cover_carousel.cpp.
#ifdef __SWITCH__
constexpr brls::Time kHoldOnSelectUs = 460000;
#else
constexpr brls::Time kHoldOnSelectUs = 0;
#endif
brls::Time holdUntil = 0; // shared: the slide moves every object at once

std::string fullPath(const std::string& path) {
    return std::string(BRLS_RESOURCES) + path;
}
} // namespace

Turntable::Turntable(std::string path) : file(fullPath(path)) {
    const std::string ext = ".mesh";
    if (path.size() > ext.size() &&
        path.compare(path.size() - ext.size(), ext.size(), ext) == 0) {
        mesh = std::make_unique<MeshRaster>();
        if (!mesh->load(file))
            image = -1;
    }
}

Turntable::~Turntable() {
    if (image > 0)
        nvgDeleteImage(Application::getNVGContext(), image);
}

bool Turntable::exists(const std::string& path) {
    FILE* f = std::fopen(fullPath(path).c_str(), "rb");
    if (!f)
        return false;
    std::fclose(f);
    return true;
}

void Turntable::setGlow(NVGcolor color, float emit) {
    if (!mesh)
        return;
    mesh->setGlow(color.r, color.g, color.b, emit);
    stale = true;
}

void Turntable::setActive(bool on) {
    active = on;
    holdUntil = getCPUTimeUsec() + kHoldOnSelectUs;
    focus.stop();
    focus.reset();
    focus.addStep(on ? 1.0f : 0.0f, 220, EasingFunction::quadraticOut);
    focus.start();

    hop.stop();
    hop.reset();
    if (on)
        hop.addStep(1.0f, 650, EasingFunction::backOut);
    else
        hop.addStep(0.0f, 260, EasingFunction::quadraticOut);
    hop.start();
}

void Turntable::draw(NVGcontext* vg, float cx, float cy, float width,
                     float groundY, float alpha) {
    if (image == 0 && !mesh) {
        image = nvgCreateImage(vg, file.c_str(), 0);
        if (image <= 0)
            image = -1;
    }
    if (image < 0)
        return;

    const Time now = getCPUTimeUsec();
    const float dt =
        lastFrame && now >= holdUntil ? std::min((float)(now - lastFrame) / 1000000.0f, 0.1f) : 0.0f;
    lastFrame = now;
    if (active) {
        spin = std::fmod(spin + 360.0f * dt / kTurnSeconds, 360.0f);
    } else {
        // Back to the front by the shorter way.
        float d = spin > 180 ? spin - 360 : spin;
        d -= d * std::min(1.0f, dt * 5);
        spin = d < 0 ? d + 360 : d;
    }
    int atlasW = 0, atlasH = 0;
    if (!mesh)
        nvgImageSize(vg, image, &atlasW, &atlasH);
    const int rows = (kFrames + kCols - 1) / kCols;
    const float frameW = mesh ? mesh->aspect() : (float)atlasW / kCols;
    const float frameH = mesh ? 1.0f : (float)atlasH / rows;
    if (frameW <= 0 || frameH <= 0)
        return;

    const float f = focus;
    const float t = (float)(now % 100000000) / 1000000.0f;
    const float bob = std::sin(t * 2 * NVG_PI / kBobSeconds);
    const float lift = 4 * f * bob + 8 * (float)hop;
    const float w = width * (1 + 0.06f * f);
    const float h = w * frameH / frameW;

    // Soft shadow, smaller while the object is higher.
    const float sw = w * (0.36f - 0.02f * f * bob);
    nvgSave(vg);
    nvgTranslate(vg, cx, groundY);
    nvgScale(vg, 1.0f, 0.14f);
    nvgBeginPath(vg);
    nvgCircle(vg, 0, 0, sw);
    nvgFillPaint(vg, nvgRadialGradient(vg, 0, 0, 0, sw,
                                       nvgRGBAf(0, 0, 0, (0.45f - 0.12f * f) * alpha),
                                       nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    nvgRestore(vg);

    if (mesh) {
        // Eased, not set: while the object holds still the rock's target
        // keeps changing, and picking it up directly would make it jump.
        const float rock = kRockDegrees * f * std::sin(t * 2 * NVG_PI / kRockSeconds);
        shownTilt += (rock - shownTilt) * std::min(1.0f, dt * 5);
        const float tilt = shownTilt;
        // Rendered at the focused size, so growing on focus only scales it.
        drawLive(vg, cx, cy - lift, w, h, width * 1.06f, tilt, (0.85f + 0.15f * f) * alpha);
        return;
    }

    const int frame = (int)std::lround(spin / 360.0f * kFrames) % kFrames;
    const float k = w / frameW;
    const float left = cx - w / 2, top = cy - lift - h / 2;
    const int col = frame % kCols, row = frame / kCols;
    nvgBeginPath(vg);
    nvgRect(vg, left, top, w, h);
    nvgFillPaint(vg, nvgImagePattern(vg, left - col * w, top - row * h,
                                     atlasW * k, atlasH * k, 0, image,
                                     (0.85f + 0.15f * f) * alpha));
    nvgFill(vg);
}

void Turntable::drawLive(NVGcontext* vg, float cx, float cy, float w, float h,
                         float renderW, float tilt, float alpha) {
    // Rendered at the screen's own pixel size, again only when it moved.
    const float dw = w * kLiveMargin, dh = h * kLiveMargin;
    const int pw = (int)std::lround(renderW * kLiveMargin * Application::windowScale);
    const int ph = (int)std::lround(pw * h / w);
    if (pw <= 0 || ph <= 0)
        return;
    const bool resized = pw != imageW || ph != imageH;
    const bool mustRender = image == 0 || resized || stale;
    const bool moved = std::fabs(spin - drawnSpin) > 0.02f ||
                       std::fabs(tilt - drawnTilt) > 0.02f;
    if (mustRender || (moved && getCPUTimeUsec() >= holdUntil)) {
        mesh->render(pw, ph, spin, tilt, kLiveMargin, 2, pixels);
        if (image > 0 && resized) {
            nvgDeleteImage(vg, image);
            image = 0;
        }
        if (image == 0) {
            // COPY_SWAP: the D3D11 backend's plain update path gets the row
            // pitch of RGBA images wrong; its staging-texture path doesn't.
            // Other backends ignore the flag.
            image = nvgCreateImageRGBA(vg, pw, ph, NVG_IMAGE_COPY_SWAP, pixels.data());
            if (image <= 0) {
                image = -1;
                return;
            }
        } else {
            nvgUpdateImage(vg, image, pixels.data());
        }
        imageW = pw;
        imageH = ph;
        stale = false;
        drawnSpin = spin;
        drawnTilt = tilt;
    }

    const float left = cx - dw / 2, top = cy - dh / 2;
    nvgBeginPath(vg);
    nvgRect(vg, left, top, dw, dh);
    nvgFillPaint(vg, nvgImagePattern(vg, left, top, dw, dh, 0, image, alpha));
    nvgFill(vg);
}
