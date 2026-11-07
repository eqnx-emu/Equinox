//
//  metrics_hud.cpp
//  Equinox
//

#include "metrics_hud.hpp"

#include "Settings.hpp"

using namespace brls;
using namespace brls::literals;

namespace MetricsHud {
namespace {
enum Position { TOP_LEFT, TOP_CENTER, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_CENTER, BOTTOM_RIGHT };
enum Background { NONE, TRANSLUCENT, SOLID };

NVGcolor textColor(int index) {
    switch (index) {
        case 1: return nvgRGB(244, 184, 96);  // sun
        case 2: return nvgRGB(157, 184, 255); // moon
        case 3: return nvgRGB(86, 200, 124);  // green
        default: return nvgRGB(236, 238, 248);
    }
}
} // namespace

std::vector<Item> items(const Values& v) {
    const int mask = Settings::instance().metrics_mask();
    std::vector<Item> list;
    if (mask & FPS)
        list.push_back({fmt::format("{:.0f} FPS", v.fps)});
    if (mask & LATENCY)
        list.push_back({fmt::format("{:.0f} ms", v.latencyMs)});
    if (mask & BITRATE)
        list.push_back({fmt::format("{:.1f} Mbps", v.bitrateMbps)});
    if (mask & LOSS) {
        Item loss{fmt::format("{:.1f}% {}", v.lossPercent,
                              "streaming/metrics_loss"_i18n)};
        if (v.lossPercent >= 1) {
            loss.warning = true;
            loss.warningColor = v.lossPercent >= 5 ? nvgRGB(236, 84, 84)
                                                   : nvgRGB(245, 160, 64);
        }
        list.push_back(loss);
    }
    return list;
}

void draw(NVGcontext* vg, float sx, float sy, float sw, float sh,
          const std::vector<Item>& list, float scale) {
    if (list.empty())
        return;
    const auto& settings = Settings::instance();
    const int position = settings.metrics_position();
    const int background = settings.metrics_background();
    const NVGcolor color = textColor(settings.metrics_color());

    nvgSave(vg);
    nvgFontFaceId(vg, Application::getFont(FONT_REGULAR));
    nvgFontSize(vg, 16 * scale);
    nvgFontBlur(vg, 0);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

    const float gap = 18 * scale, padX = 14 * scale, h = 30 * scale,
                margin = 16 * scale;
    std::vector<float> widths;
    float total = gap * (list.size() - 1) + padX * 2;
    for (const auto& item : list) {
        widths.push_back(nvgTextBounds(vg, 0, 0, item.text.c_str(), nullptr, nullptr));
        total += widths.back();
    }

    float left;
    switch (position % 3) {
        case 0: left = sx + margin; break;
        case 1: left = sx + (sw - total) / 2; break;
        default: left = sx + sw - margin - total; break;
    }
    const float top = position < BOTTOM_LEFT ? sy + margin : sy + sh - margin - h;

    if (background != NONE) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, left, top, total, h, h / 2);
        nvgFillColor(vg, background == SOLID ? nvgRGB(16, 19, 40)
                                             : nvgRGBA(10, 12, 28, 170));
        nvgFill(vg);
    }

    float x = left + padX;
    const float cy = top + h / 2;
    for (size_t i = 0; i < list.size(); i++) {
        if (i > 0) {
            nvgBeginPath(vg);
            nvgCircle(vg, x - gap / 2, cy, 1.5f * scale);
            NVGcolor dot = color;
            dot.a = 0.4f;
            nvgFillColor(vg, dot);
            nvgFill(vg);
        }
        const char* text = list[i].text.c_str();
        // Without a backdrop, a soft shadow keeps the text readable.
        if (background == NONE) {
            nvgFontBlur(vg, 3 * scale);
            nvgFillColor(vg, nvgRGBA(0, 0, 0, 230));
            nvgText(vg, x, cy + scale, text, nullptr);
            nvgFontBlur(vg, 0);
        }
        nvgFillColor(vg, list[i].warning ? list[i].warningColor : color);
        nvgText(vg, x, cy, text, nullptr);
        x += widths[i] + gap;
    }
    nvgRestore(vg);
}

std::vector<std::string> positionNames() {
    return {"settings/metrics_positions/top_left"_i18n,
            "settings/metrics_positions/top_center"_i18n,
            "settings/metrics_positions/top_right"_i18n,
            "settings/metrics_positions/bottom_left"_i18n,
            "settings/metrics_positions/bottom_center"_i18n,
            "settings/metrics_positions/bottom_right"_i18n};
}

std::vector<std::string> colorNames() {
    return {"settings/metrics_colors/white"_i18n,
            "settings/metrics_colors/sun"_i18n,
            "settings/metrics_colors/moon"_i18n,
            "settings/metrics_colors/green"_i18n};
}

std::vector<std::string> backgroundNames() {
    return {"settings/metrics_backgrounds/none"_i18n,
            "settings/metrics_backgrounds/translucent"_i18n,
            "settings/metrics_backgrounds/solid"_i18n};
}

} // namespace MetricsHud

MetricsPreview::MetricsPreview() {
    // Sits right of the metric options.
    setWidth(420);
    setHeight(236);
    setMarginTop(17);
    setMarginLeft(24);
    setShrink(0);
}

void MetricsPreview::draw(NVGcontext* vg, float x, float y, float width,
                          float height, Style style, FrameContext* ctx) {
    // A 16:9 screen, centered, with a dusk sky and hills standing in for a game.
    const float sh = height, sw = sh * 16 / 9;
    const float sx = x + (width - sw) / 2, sy = y;
    const float r = 10;

    nvgSave(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, sx, sy, sw, sh, r);
    nvgFillPaint(vg, nvgLinearGradient(vg, sx, sy, sx, sy + sh,
                                       nvgRGB(64, 78, 150), nvgRGB(236, 150, 110)));
    nvgFill(vg);
    nvgIntersectScissor(vg, sx, sy, sw, sh);

    nvgBeginPath(vg);
    nvgCircle(vg, sx + sw * 0.7f, sy + sh * 0.62f, sh * 0.16f);
    nvgFillColor(vg, nvgRGBA(255, 214, 160, 200));
    nvgFill(vg);

    auto hill = [&](float base, float peak, float shift, NVGcolor c) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, sx, sy + sh);
        nvgLineTo(vg, sx, sy + sh * base);
        nvgBezierTo(vg, sx + sw * (0.25f + shift), sy + sh * peak,
                    sx + sw * (0.55f + shift), sy + sh * (base + 0.12f),
                    sx + sw, sy + sh * (base - 0.05f));
        nvgLineTo(vg, sx + sw, sy + sh);
        nvgClosePath(vg);
        nvgFillColor(vg, c);
        nvgFill(vg);
    };
    hill(0.72f, 0.5f, 0.0f, nvgRGB(58, 52, 104));
    hill(0.84f, 0.66f, 0.15f, nvgRGB(30, 30, 66));

    MetricsHud::Values sample{60, 18, 44.8f, 0.0f};
    MetricsHud::draw(vg, sx, sy, sw, sh, MetricsHud::items(sample),
                     sw / 1280 * 2.0f);
    nvgRestore(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, sx, sy, sw, sh, r);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 40));
    nvgStrokeWidth(vg, 1);
    nvgStroke(vg);
}

brls::View* MetricsPreview::create() { return new MetricsPreview(); }
