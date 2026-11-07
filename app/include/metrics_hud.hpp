//
//  metrics_hud.hpp
//  Equinox
//

#pragma once

#include <borealis.hpp>
#include <string>
#include <vector>

// The stream metrics pill drawn over the game, and its preview in Settings.
namespace MetricsHud {

enum Metric { FPS = 1, LATENCY = 2, BITRATE = 4, LOSS = 8, ALL = 15 };

struct Values {
    float fps = 0, latencyMs = 0, bitrateMbps = 0, lossPercent = 0;
};

struct Item {
    std::string text;
    bool warning = false; // drawn in its own color, whatever the text color
    NVGcolor warningColor{};
};

// Texts for the metrics enabled in Settings, in display order.
std::vector<Item> items(const Values& values);

// Draws the pill inside the screen area (sx, sy, sw, sh), placed, colored and
// backed as set in Settings. `scale` sizes everything (1 = full screen).
void draw(NVGcontext* vg, float sx, float sy, float sw, float sh,
          const std::vector<Item>& items, float scale = 1);

std::vector<std::string> positionNames();
std::vector<std::string> colorNames();
std::vector<std::string> backgroundNames();

} // namespace MetricsHud

// A small screen showing how the metrics will look over a game.
class MetricsPreview : public brls::View {
  public:
    MetricsPreview();
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;
    static brls::View* create();
};
