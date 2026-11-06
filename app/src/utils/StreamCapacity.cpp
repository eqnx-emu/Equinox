#include "StreamCapacity.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
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

std::vector<int> plan_stage_bitrates(int min_kbps, int max_kbps) {
    std::vector<int> out;
    double v = std::min(min_kbps, max_kbps);
    while (out.size() < 6) {
        int r = static_cast<int>(std::lround(v / kRoundStepKbps)) *
                kRoundStepKbps;
        // Starting at the ceiling (min > max): round down so it still fits.
        if (out.empty() && r > max_kbps)
            r = max_kbps / kRoundStepKbps * kRoundStepKbps;
        if (r > max_kbps || r < kRoundStepKbps) break;
        out.push_back(r);
        v *= 1.5;
    }
    return out;
}

StageResult evaluate_stage(const StageConfig& config,
                           const std::vector<StageSample>& samples,
                           bool disconnected) {
    StageResult r{};
    r.config = config;
    r.disconnected = disconnected;
    if (samples.empty()) {
        r.passed = false;
        return r;
    }
    double rtt = 0, var = 0, dec = 0, lat = 0;
    size_t poor = 0;
    for (const auto& s : samples) {
        rtt += s.rtt_ms;
        var += s.rtt_variance_ms;
        dec += s.decode_ms;
        lat += s.total_latency_ms;
        if (s.poor_connection) poor++;
    }
    double n = static_cast<double>(samples.size());
    r.avg_rtt_ms = static_cast<float>(rtt / n);
    r.avg_rtt_variance_ms = static_cast<float>(var / n);
    r.avg_decode_ms = static_cast<float>(dec / n);
    r.avg_latency_ms = static_cast<float>(lat / n);

    const auto& first = samples.front();
    const auto& last = samples.back();
    double dframes =
        last.frames > first.frames ? last.frames - first.frames : 0;
    double ddropped = last.dropped_frames > first.dropped_frames
                          ? last.dropped_frames - first.dropped_frames
                          : 0;
    r.drop_pct =
        dframes > 0 ? static_cast<float>(ddropped * 100.0 / dframes) : 0.0f;

    bool decode_ok =
        config.fps > 0 && r.avg_decode_ms < 0.8 * 1000.0 / config.fps;
    bool poor_ok = poor * 2 <= samples.size();
    r.passed = !disconnected && r.drop_pct <= 1.0f && decode_ok &&
               r.avg_rtt_variance_ms <= 10.0f && poor_ok;
    return r;
}

LatencyResult
summarize_latency(const std::vector<std::optional<float>>& samples_ms) {
    LatencyResult r{};
    if (samples_ms.empty()) return r;
    std::vector<float> valid;
    for (const auto& s : samples_ms) {
        if (s) valid.push_back(*s);
    }
    r.loss_pct = 100.0f * static_cast<float>(samples_ms.size() - valid.size()) /
                 static_cast<float>(samples_ms.size());
    if (valid.empty()) return r;
    r.min_ms = *std::min_element(valid.begin(), valid.end());
    r.max_ms = *std::max_element(valid.begin(), valid.end());
    r.avg_ms = std::accumulate(valid.begin(), valid.end(), 0.0f) /
               static_cast<float>(valid.size());
    if (valid.size() > 1) {
        float diff = 0;
        for (size_t i = 1; i < valid.size(); i++) {
            diff += std::fabs(valid[i] - valid[i - 1]);
        }
        r.jitter_ms = diff / static_cast<float>(valid.size() - 1);
    }
    return r;
}

LatencyGrade grade_latency(float avg_ms) {
    if (avg_ms < 5.0f) return LatencyGrade::Excellent;
    if (avg_ms < 15.0f) return LatencyGrade::Good;
    if (avg_ms < 30.0f) return LatencyGrade::Acceptable;
    return LatencyGrade::Poor;
}

float throughput_mbps(const std::vector<std::pair<double, uint64_t>>& progress,
                      double warmup_s) {
    const std::pair<double, uint64_t>* base = nullptr;
    for (const auto& p : progress) {
        if (p.first >= warmup_s) {
            base = &p;
            break;
        }
    }
    if (!base) return 0.0f;
    const auto& last = progress.back();
    double dt = last.first - base->first;
    if (dt <= 0.0 || last.second < base->second) return 0.0f;
    return static_cast<float>((last.second - base->second) * 8.0 / dt / 1e6);
}

namespace {
// min(100000, 0.8 x speed) kbps; 100000 without a speed measurement.
int speed_ceiling_kbps(std::optional<float> speed_mbps) {
    if (!speed_mbps) return kMaxBitrateKbps;
    double c = 0.8 * static_cast<double>(*speed_mbps) * 1000.0;
    return static_cast<int>(std::min<double>(c, kMaxBitrateKbps));
}
} // namespace

int stage_ceiling_kbps(std::optional<float> speed_mbps, bool optimistic) {
    return optimistic ? speed_ceiling_kbps(speed_mbps) : kMaxBitrateKbps;
}

std::optional<int> suggest_resolution(std::optional<float> speed_mbps,
                                      int current_height, int fps,
                                      VideoCodec codec) {
    if (!speed_mbps) return std::nullopt;
    const int ceiling = speed_ceiling_kbps(speed_mbps);
    auto min_for = [&](int h) {
        auto sz = stream_size_for(h, 0, 0, 100);
        return min_recommended_bitrate_kbps(sz.first, sz.second, fps, codec);
    };
    if (min_for(current_height) <= ceiling) return std::nullopt;
    static const int heights[] = {1440, 1080, 720, 540, 480, 360};
    for (int h : heights) {
        if (h < current_height && min_for(h) <= ceiling) return h;
    }
    return std::nullopt;
}

CapacityVerdict summarize(std::optional<LatencyResult> latency,
                          std::optional<float> speed_mbps,
                          const std::vector<StageResult>& results,
                          bool used_fallback_app,
                          const StageConfig& requested) {
    CapacityVerdict v{};
    v.latency = latency;
    v.speed_mbps = speed_mbps;
    v.optimistic = used_fallback_app;
    for (const auto& r : results) {
        if (r.passed && !r.disconnected &&
            (!v.best || r.config.bitrate_kbps > v.best->config.bitrate_kbps)) {
            v.best = r;
        }
    }
    if (v.best) {
        int rec = std::min(v.best->config.bitrate_kbps,
                           stage_ceiling_kbps(speed_mbps, v.optimistic));
        if (v.optimistic && speed_mbps) {
            int cap = static_cast<int>(0.7 * *speed_mbps * 1000.0 + 1e-6);
            rec = std::min(rec, cap / kRoundStepKbps * kRoundStepKbps);
        }
        v.recommended_kbps = std::max(rec, 0);
    }
    v.suggested_resolution = suggest_resolution(speed_mbps, requested.height,
                                                requested.fps, requested.codec);
    return v;
}

StreamRecommendation
recommend_settings(const CapacityVerdict& verdict,
                   std::optional<int> downgraded_resolution) {
    StreamRecommendation r{};
    if (!verdict.best || verdict.recommended_kbps <= 0)
        return r;

    r.bitrate_kbps = verdict.recommended_kbps;
    r.resolution = downgraded_resolution ? downgraded_resolution
                                         : verdict.suggested_resolution;

    float jitter = verdict.best->avg_rtt_variance_ms;
    float loss = 0.0f;
    if (verdict.latency) {
        jitter = std::max(jitter, verdict.latency->jitter_ms);
        loss = verdict.latency->loss_pct;
    }
    int pacing = 1;
    if (jitter > 8.0f || loss > 2.0f)
        pacing = 3;
    else if (jitter > 2.0f || loss > 0.0f || verdict.best->drop_pct > 0.0f)
        pacing = 2;
    r.frame_pacing = pacing;
    return r;
}

TestAppChoice choose_test_app(const std::vector<std::string>& app_names) {
    for (const auto& n : app_names) {
        if (n == "Solstice Network Test") return TestAppChoice::SolsticeTestApp;
    }
    for (const auto& n : app_names) {
        if (n == "Desktop") return TestAppChoice::Desktop;
    }
    return TestAppChoice::FirstApp;
}

bool needs_confirmation(int running_app_id, int test_app_id) {
    return running_app_id != 0 && running_app_id != test_app_id;
}

// With per-mode profiles off there is a single profile; Docked stands for it.
ProfileTarget apply_target(bool mode_profiles, bool handheld_when_tested) {
    return (mode_profiles && handheld_when_tested) ? ProfileTarget::Handheld
                                                   : ProfileTarget::Docked;
}

std::string format_decimal(double value, int max_decimals, char decimal_sep) {
    if (max_decimals < 0) max_decimals = 0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", max_decimals, value);
    std::string out(buf);
    auto dot = out.find('.');
    if (dot != std::string::npos) {
        while (!out.empty() && out.back() == '0') out.pop_back();
        if (!out.empty() && out.back() == '.') out.pop_back();
        else out[dot] = decimal_sep;
    }
    return out;
}
