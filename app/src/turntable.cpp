//
//  turntable.cpp
//  Equinox
//

#include "turntable.hpp"

#include "mesh_raster.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#ifdef __SWITCH__
#include <switch.h>
#endif

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
// Moving, the object is drawn about 30 times a second with no supersampling
// (a little under 33 ms, or a frame arriving early would skip to 20 a second);
// once it rests it is drawn again at full quality.
#ifdef __SWITCH__
constexpr brls::Time kHoldOnSelectUs = 460000;
constexpr brls::Time kRenderIntervalUs = 30000;
constexpr int kMovingSamples = 1;
#else
constexpr brls::Time kHoldOnSelectUs = 0;
constexpr brls::Time kRenderIntervalUs = 0;
constexpr int kMovingSamples = 2;
#endif
constexpr int kRestingSamples = 2;
brls::Time holdUntil = 0; // shared: the slide moves every object at once

constexpr brls::Time kAppearUs = 150000; // fade in of the first render

std::string fullPath(const std::string& path) {
    return std::string(BRLS_RESOURCES) + path;
}

// One thread of its own for the renders. brls::async's pool is small on the
// Switch and also runs network calls that block for seconds; a render queued
// behind them leaves the object missing or frozen.
class RenderThread {
  public:
    static void post(std::function<void()> job) {
        // Never destroyed: the thread may still wait on it while the app exits.
        static RenderThread* instance = new RenderThread();
        {
            std::lock_guard<std::mutex> lock(instance->mutex);
            instance->jobs.push_back(std::move(job));
        }
        instance->wake.notify_one();
    }

  private:
    // Kept, not detached: libnx has no pthread_detach, so detach() throws.
    // The instance is never destroyed, so the thread is never joined.
    RenderThread() : thread([this] { run(); }) {}

    void run() {
#ifdef __SWITCH__
        // Off the UI thread's core when there is another, below the pool's
        // priority (0x3A) so network replies are not held up.
        s32 preferred = -1;
        u64 mask = 0;
        if (R_SUCCEEDED(svcGetThreadCoreMask(&preferred, &mask, CUR_THREAD_HANDLE))) {
            s32 seen = 0;
            for (s32 core = 0; core < 4; core++) {
                if (!(mask & (1ULL << core)))
                    continue;
                if (seen++ == 1) {
                    svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, static_cast<u32>(mask));
                    break;
                }
            }
        }
        svcSetThreadPriority(CUR_THREAD_HANDLE, 0x3B);
#endif
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [this] { return !jobs.empty(); });
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            job();
            // Dropped here, not when the next job replaces it: it holds the
            // model's buffers, which may belong to a Turntable already gone.
        }
    }

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void()>> jobs;
    std::thread thread; // last: run() uses the members above
};
} // namespace

// Shared with the worker, so a Turntable can go while a render is running.
// While working is set the worker owns mesh and pixels; the UI thread touches
// them only after seeing it clear.
struct Turntable::LiveRender {
    MeshRaster mesh;
    std::vector<uint8_t> pixels;
    std::atomic<bool> working{false};
};

Turntable::Turntable(std::string path) : file(fullPath(path)) {
    const std::string ext = ".mesh";
    if (path.size() > ext.size() &&
        path.compare(path.size() - ext.size(), ext.size(), ext) == 0) {
        live = std::make_shared<LiveRender>();
        if (!live->mesh.load(file))
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
    if (!live)
        return;
    glowColor = color;
    glowEmit = emit;
    glowPending = true;
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
    if (image == 0 && !live) {
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
    if (!live)
        nvgImageSize(vg, image, &atlasW, &atlasH);
    const int rows = (kFrames + kCols - 1) / kCols;
    const float frameW = live ? live->mesh.aspect() : (float)atlasW / kCols;
    const float frameH = live ? 1.0f : (float)atlasH / rows;
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

    if (live) {
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

// Takes a finished render to the GPU. Here, not on the worker: the nanovg
// context belongs to the UI thread.
void Turntable::collectRender(NVGcontext* vg) {
    if (!rendering || live->working.load(std::memory_order_acquire))
        return;
    rendering = false;
    if (image > 0 && (jobW != imageW || jobH != imageH)) {
        nvgDeleteImage(vg, image);
        image = 0;
    }
    if (image == 0) {
        // COPY_SWAP: the D3D11 backend's plain update path gets the row
        // pitch of RGBA images wrong; its staging-texture path doesn't.
        // Other backends ignore the flag.
        image = nvgCreateImageRGBA(vg, jobW, jobH, NVG_IMAGE_COPY_SWAP,
                                   live->pixels.data());
        if (image <= 0) {
            image = -1;
            return;
        }
        if (appearedAt == 0)
            appearedAt = getCPUTimeUsec();
    } else {
        nvgUpdateImage(vg, image, live->pixels.data());
    }
    imageW = jobW;
    imageH = jobH;
}

void Turntable::startRender(int w, int h, float tilt, int samples) {
    if (glowPending) {
        live->mesh.setGlow(glowColor.r, glowColor.g, glowColor.b, glowEmit);
        glowPending = false;
    }
    rendering = true;
    jobW = w;
    jobH = h;
    stale = false;
    drawnSpin = spin;
    drawnTilt = tilt;
    drawnSamples = samples;
    nextRenderAt = getCPUTimeUsec() + kRenderIntervalUs;

    live->working.store(true, std::memory_order_relaxed);
    RenderThread::post([job = live, w, h, pose = spin, tilt, samples] {
        job->mesh.render(w, h, pose, tilt, kLiveMargin, samples, job->pixels);
        job->working.store(false, std::memory_order_release);
    });
}

void Turntable::drawLive(NVGcontext* vg, float cx, float cy, float w, float h,
                         float renderW, float tilt, float alpha) {
    collectRender(vg);
    if (image < 0)
        return;

    // Rendered at the screen's own pixel size, again only when it moved.
    const float dw = w * kLiveMargin, dh = h * kLiveMargin;
    const int pw = (int)std::lround(renderW * kLiveMargin * Application::windowScale);
    const int ph = (int)std::lround(pw * h / w);
    if (pw <= 0 || ph <= 0)
        return;
    if (!rendering) {
        const bool mustRender = image == 0 || pw != imageW || ph != imageH || stale;
        const bool moved = std::fabs(spin - drawnSpin) > 0.02f ||
                           std::fabs(tilt - drawnTilt) > 0.02f;
        const Time now = getCPUTimeUsec();
        if (mustRender) {
            startRender(pw, ph, tilt, kRestingSamples);
        } else if (now >= holdUntil && now >= nextRenderAt) {
            if (moved)
                startRender(pw, ph, tilt, kMovingSamples);
            else if (drawnSamples < kRestingSamples)
                startRender(pw, ph, tilt, kRestingSamples);
        }
    }
    // The first render is still on its way; once there it fades in rather
    // than popping up over the shadow.
    if (image <= 0)
        return;
    const Time sinceAppeared = getCPUTimeUsec() - appearedAt;
    if (sinceAppeared < kAppearUs)
        alpha *= (float)sinceAppeared / kAppearUs;

    const float left = cx - dw / 2, top = cy - dh / 2;
    nvgBeginPath(vg);
    nvgRect(vg, left, top, dw, dh);
    nvgFillPaint(vg, nvgImagePattern(vg, left, top, dw, dh, 0, image, alpha));
    nvgFill(vg);
}
