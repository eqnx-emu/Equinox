//
//  cover_ambient.cpp
//  Equinox
//

#include "cover_ambient.hpp"
#include "BoxArtManager.hpp"

#include <borealis/extern/nanovg/stb_image.h>

#include <algorithm>

using namespace brls;

namespace {
// Average colour of the top and bottom halves of a cover image. Runs off
// the main thread.
bool coverColors(const std::string& path, NVGcolor& top, NVGcolor& bottom) {
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px)
        return false;
    double sum[2][3] = {};
    long count[2] = {};
    const int step = std::max(1, std::min(w, h) / 48);
    for (int yy = 0; yy < h; yy += step) {
        const int half = yy < h / 2 ? 0 : 1;
        for (int xx = 0; xx < w; xx += step) {
            const unsigned char* p = px + 4 * (yy * w + xx);
            for (int c = 0; c < 3; c++)
                sum[half][c] += p[c];
            count[half]++;
        }
    }
    stbi_image_free(px);
    auto avg = [&](int half) {
        const double k = count[half] ? 1.0 / (255.0 * count[half]) : 0;
        return nvgRGBf((float)(sum[half][0] * k), (float)(sum[half][1] * k),
                       (float)(sum[half][2] * k));
    };
    top = avg(0);
    bottom = avg(1);
    return true;
}
} // namespace

CoverAmbient::CoverAmbient() = default;

CoverAmbient::~CoverAmbient() {
    *alive = false;
    detach();
}

void CoverAmbient::select(const Host& host, int appId) {
    // The cover file is unique per PC and game.
    select(BoxArtManager::get_texture_path(host, appId),
           BoxArtManager::instance().has_boxart(host, appId), true);
}

void CoverAmbient::select(const std::string& coverPath) {
    select(coverPath, true, false);
}

void CoverAmbient::select(const std::string& path, bool onDisk,
                          bool cacheFailure) {
    // Selecting the same game again (a refresh) keeps the glow as it is.
    if (path == selected && cache.count(path))
        return;
    selected = path;
    auto cached = cache.find(path);
    if (cached != cache.end()) {
        show(cached->second);
        return;
    }
    show(Colors{});
    if (!onDisk)
        return;

    std::shared_ptr<bool> token = alive;
    brls::async([this, token, path, cacheFailure]() {
        Colors colors;
        colors.valid = coverColors(path, colors.top, colors.bottom);
        brls::sync([this, token, path, colors, cacheFailure]() {
            if (!*token)
                return;
            // A console cover still downloading is read again once it is
            // there; a PC's unreadable boxart is not.
            if (!colors.valid && !cacheFailure)
                return;
            cache[path] = colors;
            if (selected == path)
                show(colors);
        });
    });
}

void CoverAmbient::clear(int fadeMs) {
    selected.clear();
    show(Colors{}, fadeMs);
}

void CoverAmbient::attach(View* owner) {
    frame = dynamic_cast<BackdropFrame*>(owner->getAppletFrame());
    if (frame)
        frame->setBackdrop(
            [this](NVGcontext* vg, float x, float y, float w, float h) {
                paint(vg, x, y, w, h);
            });
}

void CoverAmbient::detach() {
    if (frame)
        frame->setBackdrop(nullptr);
    frame = nullptr;
}

void CoverAmbient::drawFallback(NVGcontext* vg, float x, float y,
                                float width, float height) {
    if (!frame)
        paint(vg, x, y, width, height);
}

void CoverAmbient::show(const Colors& colors, int fadeMs) {
    if (!colors.valid && !shown.valid)
        return;
    // A glow still coming in goes out from where it stands, not from full.
    const float from = colors.valid ? 0.0f : 1.0f - fade.getValue();
    previous = shown;
    shown = colors;
    fade.reset(from);
    fade.addStep(1.0f, fadeMs, EasingFunction::quadraticInOut);
    fade.start();
}

void CoverAmbient::paint(NVGcontext* vg, float x, float y, float width,
                         float height) {
    const float f = fade.getValue();
    paintOne(vg, previous, 1.0f - f, x, y, width, height);
    paintOne(vg, shown, f, x, y, width, height);
}

void CoverAmbient::paintOne(NVGcontext* vg, const Colors& colors, float alpha,
                            float x, float y, float width, float height) {
    if (!colors.valid || alpha <= 0.01f)
        return;
    auto glow = [&](float cx, float cy, float r, NVGcolor c, float a) {
        // Past r the gradient is clear: fill only the circle's square, not
        // the whole screen (three of these, six while fading).
        const float left = std::max(x, cx - r), top = std::max(y, cy - r);
        const float right = std::min(x + width, cx + r), bottom = std::min(y + height, cy + r);
        if (right <= left || bottom <= top)
            return;
        nvgBeginPath(vg);
        nvgRect(vg, left, top, right - left, bottom - top);
        nvgFillPaint(vg, nvgRadialGradient(vg, cx, cy, 0, r,
                                           nvgRGBAf(c.r, c.g, c.b, a * alpha),
                                           nvgRGBAf(c.r, c.g, c.b, 0)));
        nvgFill(vg);
    };
    glow(x + width * 0.5f, y + height * 0.32f, width * 0.42f, colors.top, 0.28f);
    glow(x + width * 0.36f, y + height * 0.62f, width * 0.36f, colors.bottom, 0.2f);
    glow(x + width * 0.66f, y + height * 0.58f, width * 0.3f, colors.bottom, 0.14f);
}
