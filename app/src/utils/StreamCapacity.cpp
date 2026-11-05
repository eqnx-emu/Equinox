#include "StreamCapacity.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr int kMaxBitrateKbps = 100000;
constexpr int kMinBitrateKbps = 1000;
constexpr int kRoundStepKbps = 500;

struct TablePoint {
    double pixels;
    double mbps;
};

// H.264, 60 FPS reference points (16:9).
const std::vector<TablePoint>& table() {
    static const std::vector<TablePoint> t = {
        {640.0 * 360.0, 2.0},    {854.0 * 480.0, 4.0},
        {960.0 * 540.0, 5.0},    {1280.0 * 720.0, 8.0},
        {1920.0 * 1080.0, 15.0}, {2560.0 * 1440.0, 30.0},
    };
    return t;
}

double base_mbps_for_pixels(double pixels) {
    const auto& t = table();
    if (pixels <= t.front().pixels) {
        return t.front().mbps * pixels / t.front().pixels;
    }
    if (pixels >= t.back().pixels) {
        return t.back().mbps * pixels / t.back().pixels;
    }
    for (size_t i = 1; i < t.size(); i++) {
        if (pixels <= t[i].pixels) {
            double f = (pixels - t[i - 1].pixels) /
                       (t[i].pixels - t[i - 1].pixels);
            return t[i - 1].mbps + f * (t[i].mbps - t[i - 1].mbps);
        }
    }
    return t.back().mbps;
}

double fps_factor(int fps) {
    if (fps <= 0) return 0.0;
    if (fps <= 60) return fps / 60.0;
    return std::sqrt(fps / 60.0);
}

double codec_factor(VideoCodec codec) {
    switch (codec) {
    case H265:
        return 0.7;
    case AV1:
        return 0.6;
    case H264:
    default:
        return 1.0;
    }
}

} // namespace

int min_recommended_bitrate_kbps(int width, int height, int fps,
                                 VideoCodec codec) {
    double pixels = static_cast<double>(std::max(width, 0)) *
                    static_cast<double>(std::max(height, 0));
    double kbps = base_mbps_for_pixels(pixels) * 1000.0 * fps_factor(fps) *
                  codec_factor(codec);
    // Round up to a multiple of 500 kbps (epsilon guards float noise).
    double steps = std::ceil(kbps / kRoundStepKbps - 1e-6);
    double rounded = steps * kRoundStepKbps;
    rounded = std::min<double>(rounded, kMaxBitrateKbps);
    rounded = std::max<double>(rounded, kMinBitrateKbps);
    return static_cast<int>(rounded);
}

std::pair<int, int> stream_size_for(int resolution_setting, int native_w,
                                    int native_h, int native_scale) {
    int h = resolution_setting;
    int w = h * 16 / 9;
    if (resolution_setting == -1) {
        w = native_w;
        h = native_h;
        if (native_scale != 100 && w > 0 && h > 0) {
            w = (w * native_scale + 50) / 100;
            h = (h * native_scale + 50) / 100;
        }
    }

    // Prohibit odd values rounding up
    if ((h & 1) == 1) {
        h += 1;
    }
    if ((w & 1) == 1) {
        w += 1;
    }
    return {w, h};
}
