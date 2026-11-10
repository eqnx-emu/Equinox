//
//  game_details_panel.cpp
//  Equinox
//

#include "game_details_panel.hpp"

#if defined(EQX_EMU_UI)

#include "Settings.hpp"
#include "bold_label.hpp"
#include "emu/PathUtf8.hpp"
#include "emu/SsCredentials.hpp"
#include "image_decode.hpp"
#include "library_widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>

using namespace brls;

namespace {
constexpr float kSnapW = 292, kSnapH = 256, kSnapRadius = 10;
constexpr float kTitleW = 148, kTitleH = 112, kTitleRadius = 8;
constexpr float kButtonHeight = 44;
constexpr float kBarWidth = 400;
constexpr int kFadeMs = 150;
constexpr float kPi = 3.14159265358979f;
constexpr float kStickDeadzone = 0.2f;
constexpr float kStickPixelsPerFrame = 8;
constexpr float kSynopsisIndicatorRoom = 20;
// Cells made up front; applyFacts adds rows if a game ever lists more.
constexpr size_t kUsualFacts = 8;

NVGcolor themeColor(const char* name) {
    return Application::getTheme().getColor(name);
}

std::string text(const std::string& key) {
    return getStr(key);
}

int64_t fileStamp(const std::string& path) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(emu::fromUtf8(path), ec);
    return ec ? 0 : static_cast<int64_t>(t.time_since_epoch().count());
}

void dashedFrame(NVGcontext* vg, float x, float y, float w, float h, float r, NVGcolor color) {
    constexpr float dash = 5, gap = 4;
    const auto edge = [&](float x0, float y0, float x1, float y1) {
        const float len = std::hypot(x1 - x0, y1 - y0);
        const float dx = (x1 - x0) / len, dy = (y1 - y0) / len;
        for (float t = 0; t < len; t += dash + gap) {
            const float end = std::min(t + dash, len);
            nvgMoveTo(vg, x0 + dx * t, y0 + dy * t);
            nvgLineTo(vg, x0 + dx * end, y0 + dy * end);
        }
    };
    const auto corner = [&](float cx, float cy, float from) {
        nvgMoveTo(vg, cx + r * std::cos(from), cy + r * std::sin(from));
        nvgArc(vg, cx, cy, r, from, from + kPi / 2, NVG_CW);
    };
    nvgBeginPath(vg);
    edge(x + r, y, x + w - r, y);
    edge(x + w, y + r, x + w, y + h - r);
    edge(x + w - r, y + h, x + r, y + h);
    edge(x, y + h - r, x, y + r);
    corner(x + r, y + r, kPi);
    corner(x + w - r, y + r, 1.5f * kPi);
    corner(x + w - r, y + h - r, 0);
    corner(x + r, y + h - r, kPi / 2);
    nvgStrokeWidth(vg, 1.5f);
    nvgStrokeColor(vg, color);
    nvgStroke(vg);
}

// An image cropped to fill its frame, or a dashed frame with a short text.
// The file is decoded off the UI thread.
class ArtFrame : public Box {
  public:
    ArtFrame(float w, float h, float r, float fontSize) : radius(r), frameW(w), frameH(h) {
        setWidth(w);
        setHeight(h);
        setShrink(0);
        setAlignItems(AlignItems::CENTER);
        setJustifyContent(JustifyContent::CENTER);
        label = new Label();
        label->setFontSize(fontSize);
        label->setWidth(w - 16);
        label->setHorizontalAlign(HorizontalAlign::CENTER);
        addView(label);
        applyColors();
    }

    // Kept, not looked up by name while drawing.
    void applyColors() {
        muted = themeColor("equinox/muted");
        empty = themeColor("equinox/card_inner");
        label->setTextColor(muted);
    }

    ~ArtFrame() override { freeTexture(); }

    // An empty path shows the dashed frame, with placeholder in it.
    void set(const std::string& imagePath, const std::string& placeholder) {
        label->setText(placeholder);
        label->setVisibility(imagePath.empty() && !placeholder.empty() ? Visibility::VISIBLE
                                                                       : Visibility::GONE);
        // Same file: only a new download changes it. A different file is
        // stamped by the worker, so moving through the list costs no stat.
        // Still decoding it: that decode stays, a new one would only queue
        // behind it.
        if (imagePath == path) {
            if (path.empty() || loadedStamp == kPending || fileStamp(path) == loadedStamp)
                return;
        }
        path = imagePath;
        loadedStamp = kPending;
        const unsigned wanted = ticket->fetch_add(1) + 1;
        freeTexture();
        if (path.empty())
            return;

        const std::string file = path;
        // No bigger than the frame on screen.
        const int minW = static_cast<int>(std::ceil(frameW * Application::windowScale));
        const int minH = static_cast<int>(std::ceil(frameH * Application::windowScale));
        ASYNC_RETAIN
        async([ASYNC_TOKEN, wanted, file, minW, minH, current = ticket]() {
            // Passed over while queued: the pool has few threads, and a
            // decode nobody waits for would hold up the one that is wanted.
            const int64_t stamp = current->load() == wanted ? fileStamp(file) : 0;
            DecodedImage image = decodeIfWanted(current, wanted, file, minW, minH);
            sync([ASYNC_TOKEN, wanted, image, stamp]() {
                ASYNC_RELEASE
                if (wanted != ticket->load())
                    return;
                loadedStamp = stamp;
                if (!image.pixels)
                    return;
                tex = nvgCreateImageRGBA(Application::getNVGContext(), image.width,
                                         image.height, NVG_IMAGE_NEAREST, image.pixels.get());
                texW = image.width;
                texH = image.height;
            });
        });
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, Style style,
              FrameContext* ctx) override {
        if (tex > 0 && texW > 0 && texH > 0) {
            const float fit = std::max(width / texW, height / texH);
            const float iw = texW * fit, ih = texH * fit;
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, width, height, radius);
            nvgFillPaint(vg, a(nvgImagePattern(vg, x + (width - iw) / 2, y + (height - ih) / 2, iw,
                                               ih, 0, tex, 1)));
            nvgFill(vg);
        } else if (!path.empty()) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, width, height, radius);
            nvgFillColor(vg, a(empty));
            nvgFill(vg);
        } else {
            NVGcolor dash = muted;
            dash.a *= 0.3f;
            dashedFrame(vg, x, y, width, height, radius, a(dash));
        }
        Box::draw(vg, x, y, width, height, style, ctx);
    }

  private:
    float radius;
    float frameW, frameH;
    NVGcolor muted{}, empty{};
    Label* label = nullptr;
    static constexpr int64_t kPending = -1; // decoding: its stamp is not known yet
    std::string path;
    int64_t loadedStamp = 0;
    // Bumped when the frame stops wanting the image being decoded.
    ImageTicket ticket = makeImageTicket();
    int tex = 0, texW = 0, texH = 0;

    void freeTexture() {
        if (tex > 0)
            nvgDeleteImage(Application::getNVGContext(), tex);
        tex = texW = texH = 0;
    }
};

Label* makeLabel(float size) {
    auto* label = new Label();
    label->setFontSize(size);
    return label;
}

Button* makeButton(const ButtonStyle* style, const std::string& caption,
                   const std::function<void()>& onClick) {
    auto* button = new Button();
    button->setStyle(style);
    button->setText(caption);
    button->setFontSize(15);
    button->setHeight(kButtonHeight);
    button->setShrink(0);
    button->registerClickAction([onClick](View*) {
        onClick();
        return true;
    });
    return button;
}

std::string stepText(emu::DataStep step) {
    switch (step) {
    case emu::DataStep::Identify:
        return text("library/details/step_identify");
    case emu::DataStep::Snap:
        return text("library/details/step_snap");
    case emu::DataStep::Title:
        return text("library/details/step_title");
    case emu::DataStep::Synopsis:
        return text("library/details/step_synopsis");
    case emu::DataStep::Done:
        break;
    }
    return text("library/details/step_done");
}

// Follows the right stick; the focus stays on the list, so it never takes it.
class SynopsisScroll : public ScrollingFrame {
  public:
    SynopsisScroll() {
        setFocusable(false);
        setGrow(1);
    }

    View* getDefaultFocus() override { return nullptr; }

    void draw(NVGcontext* vg, float x, float y, float width, float height, Style style,
              FrameContext* ctx) override {
        const float stick = Application::getControllerState().axes[RIGHT_Y];
        if (std::abs(stick) > kStickDeadzone)
            setContentOffsetY(getContentOffsetY() + stick * kStickPixelsPerFrame, false);
        ScrollingFrame::draw(vg, x, y, width, height, style, ctx);
    }
};

// Up to maxRows lines of text from (x, y), the last one cut with an
// ellipsis when the text goes on. Returns the y below the last line.
float drawLines(NVGcontext* vg, const std::string& s, float x, float y, float width, float lineHeight,
                int maxRows) {
    if (s.empty())
        return y;
    std::vector<NVGtextRow> rows(maxRows + 1);
    const int n = nvgTextBreakLines(vg, s.c_str(), nullptr, width, rows.data(), maxRows + 1);
    for (int i = 0; i < n && i < maxRows; i++) {
        std::string line(rows[i].start, rows[i].end);
        if (i == maxRows - 1 && n > maxRows) {
            const std::string dots = "…";
            while (!line.empty() && nvgTextBounds(vg, 0, 0, (line + dots).c_str(), nullptr, nullptr) > width)
                line.pop_back();
            while (!line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0) == 0x80)
                line.pop_back(); // never half a UTF-8 character
            if (!line.empty() && (static_cast<unsigned char>(line.back()) & 0xC0) == 0xC0)
                line.pop_back(); // the lead byte left behind
            while (!line.empty() && line.back() == ' ')
                line.pop_back();
            line += dots;
        }
        nvgText(vg, x, y, line.c_str(), nullptr);
        y += lineHeight;
    }
    return y;
}

// The picked game on a retro TV: a pre-rendered TV
// with a hole where the glass is, and the screen drawn under it. A different
// game tunes the TV to it: the picture tears, rolls and turns to static,
// then settles on the next game.
class RetroTv : public Box {
  public:
    struct Screen {
        std::string name;
        std::vector<std::pair<std::string, std::string>> facts; // label, value
        std::string note;     // shown when there are no facts
        std::string synopsis; // already the placeholder when there is none
        std::string snapPath; // empty: colour bars
    };

    RetroTv() {
        setGrow(1);
        setShrink(1);
        setFocusable(false);
    }

    ~RetroTv() override {
        NVGcontext* vg = Application::getNVGContext();
        for (int* image : {&scene, &noise, &lines})
            if (*image > 0)
                nvgDeleteImage(vg, *image);
        freeTex(snap);
        freeTex(previousSnap);
    }

    void set(const Screen& next, bool tune) {
        if (tune && shown && !Settings::instance().reduce_motion()) {
            // The old picture stays on until halfway through the tuning.
            freeTex(previousSnap);
            previous = shown;
            previousSnap = snap;
            snap = {};
            snapFile.clear();
            tuneStart = getCPUTimeUsec();
        } else if (tune) {
            tuneStart = 0;
            previous.reset();
            freeTex(previousSnap);
        }
        shown = next;
        if (next.snapPath != snapFile || (tune && snap.id <= 0))
            loadSnap(next.snapPath);
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height, Style style,
              FrameContext* ctx) override {
        if (scene == 0) {
            scene = nvgCreateImage(vg, (std::string(BRLS_RESOURCES) + "img/tv/crt.png").c_str(), 0);
            if (scene > 0)
                nvgImageSize(vg, scene, &sceneW, &sceneH);
            else
                scene = -1;
        }
        if (scene < 0 || sceneW <= 0 || sceneH <= 0)
            return;
        makeTextures(vg);

        // The TV fits the view, centred.
        const float fit = std::min(width / sceneW, height / sceneH);
        const float sw = sceneW * fit, sh = sceneH * fit;
        const float sx = x + (width - sw) / 2, sy = y + (height - sh) / 2;
        const auto at = [&](const float* p) { return std::pair<float, float>{sx + p[0] * sw, sy + p[1] * sh}; };
        const auto tl = at(kTl), tr = at(kTr), br = at(kBr), bl = at(kBl);
        const float alpha = getAlpha();

        // The glass, a little past the hole: the bezel covers the edge.
        nvgBeginPath(vg);
        nvgMoveTo(vg, tl.first - 3, tl.second - 3);
        nvgLineTo(vg, tr.first + 3, tr.second - 3);
        nvgLineTo(vg, br.first + 3, br.second + 3);
        nvgLineTo(vg, bl.first - 3, bl.second + 3);
        nvgClosePath(vg);
        nvgFillColor(vg, nvgRGBAf(0.03f, 0.035f, 0.07f, alpha));
        nvgFill(vg);

        // Tuning: k is 0 clean, 1 all static. The new game shows from halfway.
        float k = 0;
        bool old = false;
        const float t = tuneStart ? (getCPUTimeUsec() - tuneStart) / 1e6f : 0;
        if (tuneStart) {
            const float p = t / kTuneSeconds;
            if (p >= 1) {
                tuneStart = 0;
                freeTex(previousSnap);
                previous.reset();
            } else {
                k = std::pow(std::sin(p * kPi), 0.6f);
                old = p < 0.5f && previous;
            }
        }

        nvgSave(vg);
        // Screen space: kW x kH, mapped onto the glass.
        nvgTransform(vg, (tr.first - tl.first) / kW, (tr.second - tl.second) / kW,
                     (bl.first - tl.first) / kH, (bl.second - tl.second) / kH, tl.first, tl.second);
        nvgScissor(vg, 0, 0, kW, kH);
        const Screen* content = old ? &*previous : (shown ? &*shown : nullptr);
        const Tex& image = old ? previousSnap : snap;
        if (content) {
            if (k <= 0) {
                drawScreen(vg, *content, image, 0, 0, alpha);
            } else {
                // Torn: bands slide sideways while the picture rolls.
                const float roll = std::fmod(k * k * 220 * std::sin(t * 9) + kH, kH);
                constexpr int bands = 12;
                for (int i = 0; i < bands; i++) {
                    const float shift = k * (60 * std::sin(i * 1.7f + t * 40) + jitter(i, t) * 25);
                    nvgSave(vg);
                    nvgIntersectScissor(vg, 0, kH * i / bands, kW, kH / bands + 1);
                    for (float wrap : {0.0f, -kH})
                        drawScreen(vg, *content, image, shift, roll + wrap, alpha);
                    nvgRestore(vg);
                }
            }
        }
        // Static.
        if (k > 0 && noise > 0) {
            const float ox = jitter(1, t) * 300, oy = jitter(2, t) * 200;
            nvgBeginPath(vg);
            nvgRect(vg, 0, 0, kW, kH);
            nvgFillPaint(vg, nvgImagePattern(vg, ox, oy, kNoiseW * 4, kNoiseH * 4, 0, noise, alpha * k));
            nvgFill(vg);
        }
        // Scanlines, one about every three pixels on screen.
        const float pxPerUnit = std::hypot(bl.first - tl.first, bl.second - tl.second) / kH;
        if (lines > 0 && pxPerUnit > 0) {
            nvgBeginPath(vg);
            nvgRect(vg, 0, 0, kW, kH);
            nvgFillPaint(vg, nvgImagePattern(vg, 0, 0, kW, 3 / pxPerUnit, 0, lines, alpha * 0.5f));
            nvgFill(vg);
        }
        // Darker corners and a faint reflection, like curved glass.
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, kW, kH);
        nvgFillPaint(vg, nvgBoxGradient(vg, 40, 30, kW - 80, kH - 60, 120, 160, nvgRGBAf(0, 0, 0, 0),
                                        nvgRGBAf(0, 0, 0, 0.5f * alpha)));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, kW, kH);
        nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, kW * 0.5f, kH * 0.6f, nvgRGBAf(1, 1, 1, 0.06f * alpha),
                                           nvgRGBAf(1, 1, 1, 0)));
        nvgFill(vg);
        nvgRestore(vg);

        nvgBeginPath(vg);
        nvgRect(vg, sx, sy, sw, sh);
        nvgFillPaint(vg, nvgImagePattern(vg, sx, sy, sw, sh, 0, scene, alpha));
        nvgFill(vg);

        Box::draw(vg, x, y, width, height, style, ctx);
    }

  private:
    // The glass corners in the scene image (0..1), from its Blender render.
    static constexpr float kTl[2] = {0.03642f, 0.33156f};
    static constexpr float kTr[2] = {0.70581f, 0.34089f};
    static constexpr float kBr[2] = {0.69834f, 0.87344f};
    static constexpr float kBl[2] = {0.05706f, 0.82701f};
    static constexpr float kW = 800, kH = 600;
    static constexpr float kTuneSeconds = 0.55f;
    static constexpr int kNoiseW = 128, kNoiseH = 96;

    struct Tex {
        int id = 0, w = 0, h = 0;
    };

    int scene = 0, sceneW = 0, sceneH = 0;
    int noise = 0, lines = 0;
    std::optional<Screen> shown, previous;
    Tex snap, previousSnap;
    std::string snapFile;
    ImageTicket ticket = makeImageTicket();
    Time tuneStart = 0;

    // A value in -1..1 that changes 30 times a second.
    static float jitter(int seed, float t) {
        const float v = std::sin(seed * 12.9898f + std::floor(t * 30) * 78.233f) * 43758.5453f;
        return (v - std::floor(v)) * 2 - 1;
    }

    void makeTextures(NVGcontext* vg) {
        if (noise == 0) {
            std::vector<unsigned char> px(kNoiseW * kNoiseH * 4);
            uint32_t s = 2463534242u;
            for (size_t i = 0; i < px.size(); i += 4) {
                s ^= s << 13;
                s ^= s >> 17;
                s ^= s << 5;
                px[i] = px[i + 1] = px[i + 2] = static_cast<unsigned char>(s & 0xff);
                px[i + 3] = 255;
            }
            noise = nvgCreateImageRGBA(vg, kNoiseW, kNoiseH,
                                       NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY | NVG_IMAGE_NEAREST, px.data());
        }
        if (lines == 0) {
            const unsigned char px[3 * 4] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 170};
            lines = nvgCreateImageRGBA(vg, 1, 3, NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY, px);
        }
    }

    static void freeTex(Tex& tex) {
        if (tex.id > 0)
            nvgDeleteImage(Application::getNVGContext(), tex.id);
        tex = {};
    }

    void loadSnap(const std::string& path) {
        snapFile = path;
        const unsigned wanted = ticket->fetch_add(1) + 1;
        if (path.empty()) {
            freeTex(snap);
            return;
        }
        ASYNC_RETAIN
        async([ASYNC_TOKEN, wanted, path, current = ticket]() {
            DecodedImage image = decodeIfWanted(current, wanted, path, 384, 288);
            sync([ASYNC_TOKEN, wanted, image]() {
                ASYNC_RELEASE
                if (wanted != ticket->load() || !image.pixels)
                    return;
                freeTex(snap);
                snap.id = nvgCreateImageRGBA(Application::getNVGContext(), image.width, image.height, 0,
                                             image.pixels.get());
                snap.w = image.width;
                snap.h = image.height;
            });
        });
    }

    // One picture of the screen, moved by dx, dy (tearing and rolling).
    void drawScreen(NVGcontext* vg, const Screen& s, const Tex& image, float dx, float dy, float alpha) {
        nvgSave(vg);
        nvgTranslate(vg, dx, dy);
        nvgBeginPath(vg);
        nvgRect(vg, -kW, 0, kW * 3, kH);
        nvgFillColor(vg, nvgRGBAf(0.055f, 0.07f, 0.16f, alpha));
        nvgFill(vg);

        // The gameplay picture, or colour bars while there is none.
        constexpr float px = 40, py = 50, pw = 384, ph = 288;
        if (image.id > 0 && image.w > 0 && image.h > 0) {
            const float fit = std::max(pw / image.w, ph / image.h);
            const float iw = image.w * fit, ih = image.h * fit;
            nvgBeginPath(vg);
            nvgRect(vg, px, py, pw, ph);
            nvgFillPaint(vg, nvgImagePattern(vg, px + (pw - iw) / 2, py + (ph - ih) / 2, iw, ih, 0, image.id,
                                             alpha));
            nvgFill(vg);
        } else {
            static const float bars[7][3] = {{.75f, .75f, .75f}, {.75f, .75f, 0}, {0, .75f, .75f}, {0, .75f, 0},
                                             {.75f, 0, .75f},    {.75f, 0, 0},    {0, 0, .75f}};
            for (int i = 0; i < 7; i++) {
                nvgBeginPath(vg);
                nvgRect(vg, px + pw * i / 7, py, pw / 7 + 0.5f, ph);
                nvgFillColor(vg, nvgRGBAf(bars[i][0], bars[i][1], bars[i][2], alpha));
                nvgFill(vg);
            }
        }
        nvgBeginPath(vg);
        nvgRect(vg, px - 2, py - 2, pw + 4, ph + 4);
        nvgStrokeWidth(vg, 3);
        nvgStrokeColor(vg, nvgRGBAf(1, 0.65f, 0.16f, alpha));
        nvgStroke(vg);

        const int bold = Application::getFont(EQUINOX_BOLD_FONT);
        const int regular = Application::getFont(FONT_REGULAR);
        constexpr float tx = 456, tw = kW - tx - 32;
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgFontFaceId(vg, bold != FONT_INVALID ? bold : regular);
        nvgFontSize(vg, 38);
        nvgFillColor(vg, nvgRGBAf(1, 1, 1, alpha));
        float fy = drawLines(vg, s.name, tx, py - 4, tw, 42, 3) + 16;

        nvgFontFaceId(vg, regular);
        if (s.facts.empty()) {
            nvgFontSize(vg, 22);
            nvgFillColor(vg, nvgRGBAf(0.62f, 0.65f, 0.8f, alpha));
            drawLines(vg, s.note, tx, fy, tw, 28, 4);
        }
        for (size_t i = 0; i < s.facts.size() && fy < py + ph - 40; i++) {
            nvgFontSize(vg, 18);
            nvgFillColor(vg, nvgRGBAf(0.55f, 0.58f, 0.72f, alpha));
            drawLines(vg, s.facts[i].first, tx, fy, tw, 20, 1);
            nvgFontSize(vg, 23);
            nvgFillColor(vg, nvgRGBAf(1, 1, 1, alpha));
            drawLines(vg, s.facts[i].second, tx, fy + 21, tw, 26, 1);
            fy += 56;
        }

        // The synopsis: two lines, the second cut with an ellipsis.
        nvgFontSize(vg, 26);
        nvgFillColor(vg, nvgRGBAf(0.85f, 0.87f, 0.95f, alpha));
        drawLines(vg, s.synopsis, 40, 384, kW - 80, 34, 2);
        nvgRestore(vg);
    }
};

} // namespace

GameDetailsPanel::GameDetailsPanel() : Box(Axis::COLUMN) {
    setPadding(8, 36, 12, 36);
    setGrow(1);

    // The game on the TV; the download action under it.
    tv = new RetroTv();
    addView(tv);

    content = new Box(Axis::COLUMN);
    content->setShrink(0);
    addView(content);

    auto download = [this]() {
        if (onDownload)
            onDownload();
    };

    title = new BoldLabel();
    title->setFontSize(30);
    title->setShrink(0);
    title->setVisibility(Visibility::GONE); // on the TV
    content->addView(title);

    fileLine = makeLabel(15);
    fileLine->setMarginTop(8);
    fileLine->setShrink(0);
    content->addView(fileLine);

    auto* action = new Box(Axis::COLUMN);
    action->setAlignItems(AlignItems::FLEX_START);
    action->setMarginTop(12);
    action->setShrink(0);
    content->addView(action);

    downloadButton =
        makeButton(&BUTTONSTYLE_PRIMARY, text("library/details/download"), download);
    action->addView(downloadButton);

    queuedRow = new Box(Axis::ROW);
    queuedRow->setAlignItems(AlignItems::CENTER);
    waitingLabel = makeLabel(15);
    waitingLabel->setText(text("library/details/waiting"));
    queuedRow->addView(waitingLabel);
    queuedLabel = makeLabel(15);
    queuedLabel->setText(text("library/details/queued"));
    queuedLabel->setMarginLeft(12);
    queuedRow->addView(queuedLabel);
    action->addView(queuedRow);

    progressBox = new Box(Axis::COLUMN);
    progressBox->setWidth(kBarWidth);
    auto* progressLine = new Box(Axis::ROW);
    progressLine->setJustifyContent(JustifyContent::SPACE_BETWEEN);
    stepLabel = makeLabel(15);
    progressLine->addView(stepLabel);
    percentLabel = new BoldLabel();
    percentLabel->setFontSize(15);
    progressLine->addView(percentLabel);
    progressBox->addView(progressLine);
    auto* progress = new LibraryProgressBar();
    progress->setWidth(kBarWidth);
    bar = progress;
    bar->setMarginTop(6);
    progressBox->addView(bar);
    action->addView(progressBox);

    failedRow = new Box(Axis::ROW);
    failedRow->setAlignItems(AlignItems::CENTER);
    failedLabel = makeLabel(15);
    failedLabel->setShrink(1);
    failedLabel->setMarginRight(12);
    failedRow->addView(failedLabel);
    retryButton = makeButton(&BUTTONSTYLE_PRIMARY, text("library/details/retry"), download);
    failedRow->addView(retryButton);
    action->addView(failedRow);

    readyRow = new Box(Axis::ROW);
    readyRow->setAlignItems(AlignItems::CENTER);
    readyLabel = makeLabel(15);
    readyLabel->setText(text("library/details/ready"));
    readyLabel->setMarginRight(12);
    readyRow->addView(readyLabel);
    updateButton = makeButton(&BUTTONSTYLE_DEFAULT, text("library/details/update"), download);
    readyRow->addView(updateButton);
    action->addView(readyRow);

    auto* media = new Box(Axis::ROW);
    media->setAlignItems(AlignItems::FLEX_START);
    media->setMarginTop(20);
    media->setShrink(0);
    media->setVisibility(Visibility::GONE); // on the TV
    content->addView(media);

    snapFrame = new ArtFrame(kSnapW, kSnapH, kSnapRadius, 15);
    media->addView(snapFrame);

    auto* titleColumn = new Box(Axis::COLUMN);
    titleColumn->setMarginLeft(20);
    titleColumn->setShrink(0);
    titleFrame = new ArtFrame(kTitleW, kTitleH, kTitleRadius, 14);
    titleColumn->addView(titleFrame);
    titleCaption = makeLabel(14);
    titleCaption->setText(text("library/details/title_caption"));
    titleCaption->setMarginTop(8);
    titleColumn->addView(titleCaption);
    media->addView(titleColumn);

    facts = new Box(Axis::COLUMN);
    facts->setMarginLeft(20);
    facts->setGrow(1);
    facts->setShrink(1);
    facts->setWidth(0);
    media->addView(facts);

    factSentence = makeLabel(15);
    facts->addView(factSentence);
    while (factCells.size() < kUsualFacts)
        addFactRow();

    // A long synopsis scrolls inside the room left, so "Fontes" stays in place.
    auto* synopsis = new Box(Axis::COLUMN);
    synopsis->setMarginTop(20);
    synopsis->setMaxWidth(760);
    synopsis->setGrow(1);
    synopsis->setShrink(1);
    synopsisHeading = new BoldLabel();
    synopsisHeading->setFontSize(16);
    synopsisHeading->setText(text("library/details/synopsis"));
    synopsisHeading->setShrink(0);
    synopsis->addView(synopsisHeading);
    synopsisScroll = new SynopsisScroll();
    synopsisScroll->setMarginTop(8);
    auto* synopsisBody = new Box(Axis::COLUMN);
    synopsisBody->setPaddingRight(kSynopsisIndicatorRoom);
    synopsisText = makeLabel(16);
    synopsisBody->addView(synopsisText);
    synopsisScroll->setContentView(synopsisBody);
    synopsis->addView(synopsisScroll);
    synopsis->setVisibility(Visibility::GONE); // on the TV
    content->addView(synopsis);

    sources = makeLabel(14);
    sources->setMarginTop(12);
    sources->setShrink(0);
    content->addView(sources);

    fade.setTickCallback([this]() { content->setAlpha(fade.getValue()); });
    fade.setEndCallback([this](bool) { content->setAlpha(1.0f); });

    applyColors();
    applyDetails();
}

void GameDetailsPanel::willAppear(bool resetState) {
    Box::willAppear(resetState);
    applyColors();
}

void GameDetailsPanel::applyColors() {
    const NVGcolor muted = themeColor("equinox/muted");
    fileLine->setTextColor(muted);
    queuedLabel->setTextColor(muted);
    readyLabel->setTextColor(muted);
    titleCaption->setTextColor(muted);
    sources->setTextColor(muted);
    factSentence->setTextColor(muted);
    static_cast<ArtFrame*>(snapFrame)->applyColors();
    static_cast<ArtFrame*>(titleFrame)->applyColors();
    for (const FactCell& cell : factCells)
        cell.label->setTextColor(muted);
    updateButton->setBackgroundColor(themeColor("equinox/card_inner"));
    // The account can change in Settings while the panel is off screen.
    sources->setText(text(emu::ssConfigured(emu::appSsConfig()) ? "library/details/sources"
                                                                : "library/details/sources_free"));
}

void GameDetailsPanel::show(const GameDetails& next, bool animate) {
    details = next;
    if (animate) {
        step.reset();
        queued = false;
        failed.reset();
    }
    applyDetails();
    startFade(animate);
    applyTv(animate);
}

void GameDetailsPanel::applyTv(bool tune) {
    RetroTv::Screen screen;
    screen.name = details.name;
    screen.facts = shownFacts;
    if (screen.facts.empty())
        screen.note = !details.identified
                          ? brls::getStr("library/details/not_identified", details.fileName)
                          : text(details.hasData ? "library/details/facts_none" : "library/details/facts_pending");
    screen.synopsis = !details.synopsis.empty() ? details.synopsis
                      : details.hasData         ? text("library/details/synopsis_empty")
                                                : text("library/details/synopsis_none");
    screen.snapPath = details.hasData ? details.snapPath : "";
    static_cast<RetroTv*>(tv)->set(screen, tune);
}

void GameDetailsPanel::setProgress(std::optional<emu::DataStep> nextStep, bool nextQueued) {
    step = nextStep;
    queued = nextQueued;
    if (step || queued)
        failed.reset();
    applyAction();
}

void GameDetailsPanel::setFailed(std::optional<emu::DataStop> stop) {
    if (stop && (*stop == emu::DataStop::None || *stop == emu::DataStop::Cancelled))
        stop.reset();
    failed = stop;
    applyAction();
}

void GameDetailsPanel::startFade(bool animate) {
    if (!animate || Settings::instance().reduce_motion()) {
        fade.reset(1.0f);
        content->setAlpha(1.0f);
        return;
    }
    fade.reset(0.0f);
    content->setAlpha(0.0f);
    fade.addStep(1.0f, kFadeMs, EasingFunction::linear);
    fade.start();
}

void GameDetailsPanel::applyDetails() {
    title->setText(details.name);
    if (!details.identified)
        fileLine->setText(brls::getStr("library/details/not_identified", details.fileName));
    fileLine->setVisibility(Visibility::GONE); // the TV says it
    // With data downloaded, an empty synopsis is a fact, not an invitation.
    synopsisText->setText(!details.synopsis.empty() ? details.synopsis
                          : details.hasData         ? text("library/details/synopsis_empty")
                                                    : text("library/details/synopsis_none"));
    synopsisScroll->setContentOffsetY(0, false);
    applyAction();
    applyMedia();
    applyFacts();
}

void GameDetailsPanel::applyAction() {
    Action state = Action::Missing;
    if (queued && !step)
        state = Action::Queued;
    else if (step)
        state = Action::Downloading;
    else if (failed)
        state = Action::Failed;
    else if (details.hasData)
        state = Action::Ready;

    const auto shown = [&](Action a) { return state == a ? Visibility::VISIBLE : Visibility::GONE; };
    downloadButton->setVisibility(shown(Action::Missing));
    queuedRow->setVisibility(shown(Action::Queued));
    progressBox->setVisibility(shown(Action::Downloading));
    failedRow->setVisibility(shown(Action::Failed));
    readyRow->setVisibility(shown(Action::Ready));

    if (step) {
        stepLabel->setText(stepText(*step));
        const float fraction = emu::stepProgress(*step);
        percentLabel->setText(std::to_string(static_cast<int>(std::lround(fraction * 100))) + "%");
        static_cast<LibraryProgressBar*>(bar)->setFraction(fraction);
    }
    if (failed)
        failedLabel->setText(brls::getStr("library/details/failed", libraryStopText(*failed)));
}

void GameDetailsPanel::applyMedia() {
    const bool data = details.hasData;
    std::string snapNote = text("library/details/snap_placeholder");
    std::string titleNote;
    if (data && details.snapPath.empty() && details.snap == emu::PartState::Missing)
        snapNote = text("library/details/snap_missing");
    if (data && details.titlePath.empty() && details.title == emu::PartState::Missing)
        titleNote = text("library/details/title_missing");

    static_cast<ArtFrame*>(snapFrame)->set(data ? details.snapPath : "", snapNote);
    static_cast<ArtFrame*>(titleFrame)->set(data ? details.titlePath : "", titleNote);
}

// Two cells side by side, half the width each.
void GameDetailsPanel::addFactRow() {
    auto* row = new Box(Axis::ROW);
    row->setMarginBottom(14);
    const NVGcolor muted = themeColor("equinox/muted");
    for (int c = 0; c < 2; c++) {
        FactCell cell;
        cell.box = new Box(Axis::COLUMN);
        cell.box->setWidthPercentage(50);
        cell.box->setPaddingRight(20);
        cell.label = makeLabel(14);
        cell.label->setTextColor(muted);
        cell.box->addView(cell.label);
        cell.value = makeLabel(16);
        cell.value->setMarginTop(3);
        cell.box->addView(cell.value);
        row->addView(cell.box);
        factCells.push_back(cell);
    }
    facts->addView(row);
    factRows.push_back(row);
}

void GameDetailsPanel::applyFacts() {
    struct Fact {
        const char* label;
        std::string value;
    };
    std::vector<Fact> list;
    if (details.hasData) {
        if (const emu::GameInfo* info = details.info) {
            if (!info->genre.empty())
                list.push_back({"library/fact/genre", info->genre});
            if (info->players > 0)
                list.push_back({"library/fact/players", std::to_string(info->players)});
            if (!info->developer.empty())
                list.push_back({"library/fact/developer", info->developer});
            if (!info->publisher.empty())
                list.push_back({"library/fact/publisher", info->publisher});
            if (info->year > 0)
                list.push_back({"library/fact/released", std::to_string(info->year)});
        }
        if (!details.regions.empty()) {
            std::string joined;
            for (const std::string& region : details.regions) {
                if (!joined.empty())
                    joined += ", ";
                joined += text("library/region/" + region);
            }
            list.push_back({"library/fact/region", joined});
        }
        if (details.info && !details.info->franchise.empty())
            list.push_back({"library/fact/franchise", details.info->franchise});
    }

    shownFacts.clear();
    for (const Fact& fact : list)
        shownFacts.emplace_back(text(fact.label), fact.value);

    if (list.empty())
        factSentence->setText(text(details.hasData ? "library/details/facts_none"
                                                   : "library/details/facts_pending"));
    factSentence->setVisibility(list.empty() ? Visibility::VISIBLE : Visibility::GONE);

    while (factCells.size() < list.size())
        addFactRow();

    for (size_t i = 0; i < factCells.size(); i++) {
        const FactCell& cell = factCells[i];
        // Emptied when hidden: an invisible cell still has a height, and the
        // previous game's long text would make its row taller.
        cell.label->setText(i < list.size() ? text(list[i].label) : "");
        cell.value->setText(i < list.size() ? list[i].value : "");
        // An odd last fact keeps its half width: the empty cell holds the room.
        cell.box->setVisibility(i < list.size() ? Visibility::VISIBLE : Visibility::INVISIBLE);
    }
    for (size_t r = 0; r < factRows.size(); r++)
        factRows[r]->setVisibility(r * 2 < list.size() ? Visibility::VISIBLE : Visibility::GONE);
}

#endif // EQX_EMU_UI
