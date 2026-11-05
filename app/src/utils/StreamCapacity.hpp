#pragma once

#include "VideoCodec.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Minimum recommended bitrate in kbps for the given stream parameters.
int min_recommended_bitrate_kbps(int width, int height, int fps,
                                 VideoCodec codec);

// Stream size for a resolution setting (height, or -1 for native window size
// scaled by native_scale percent). Width derived as height * 16 / 9 for fixed
// heights. Both dimensions are rounded up to even values.
std::pair<int, int> stream_size_for(int resolution_setting, int native_w,
                                    int native_h, int native_scale);

// ---- Connection capacity test: pure logic (no network / UI) ----

struct StageConfig {
    int width, height, fps, bitrate_kbps;
    VideoCodec codec;
};

// One reading taken during a measured stage. frames / dropped_frames are
// cumulative counters.
struct StageSample {
    uint32_t rtt_ms, rtt_variance_ms;
    float decode_ms, total_latency_ms;
    uint32_t frames, dropped_frames;
    bool poor_connection;
};

struct StageResult {
    StageConfig config;
    float avg_rtt_ms, avg_rtt_variance_ms, avg_decode_ms, avg_latency_ms,
        drop_pct;
    bool disconnected;
    bool passed;
};

// min, min*1.5, ... rounded to 500 kbps; at most 6 values, all <= max_kbps.
std::vector<int> plan_stage_bitrates(int min_kbps, int max_kbps);

// Pass: drop <= 1%, avg decode < 80% of frame time, avg RTT variance <= 10 ms,
// not disconnected and poor-connection flag not set in most samples.
StageResult evaluate_stage(const StageConfig& config,
                           const std::vector<StageSample>& samples,
                           bool disconnected);

struct LatencyResult {
    float min_ms, avg_ms, max_ms, jitter_ms, loss_pct;
};

// nullopt = timeout. Jitter pairs consecutive valid samples only.
LatencyResult
summarize_latency(const std::vector<std::optional<float>>& samples_ms);

enum class LatencyGrade { Excellent, Good, Acceptable, Poor };
LatencyGrade grade_latency(float avg_ms);

// progress = (seconds since start, cumulative bytes). Samples before warmup_s
// are ignored (first sample at/after warmup is the baseline).
float throughput_mbps(const std::vector<std::pair<double, uint64_t>>& progress,
                      double warmup_s);

// Phase-3 bitrate ceiling in kbps. With the Solstice test app (optimistic =
// false) the measured speed is not a cap: always 100000. When optimistic
// (Desktop/first-app fallback): min(100000, 0.8 x speed), 100000 without a
// speed measurement.
int stage_ceiling_kbps(std::optional<float> speed_mbps, bool optimistic);

// Largest height of {1440,1080,720,540,480,360} whose minimum bitrate fits the
// 0.8 x speed (capped at 100000) kbps. nullopt when speed is unknown or current_height already fits.
std::optional<int> suggest_resolution(std::optional<float> speed_mbps,
                                      int current_height, int fps,
                                      VideoCodec codec);

struct CapacityVerdict {
    std::optional<LatencyResult> latency;
    std::optional<float> speed_mbps;
    std::optional<StageResult> best;
    bool optimistic;
    int recommended_kbps;
    std::optional<int> suggested_resolution;
};

CapacityVerdict summarize(std::optional<LatencyResult> latency,
                          std::optional<float> speed_mbps,
                          const std::vector<StageResult>& results,
                          bool used_fallback_app, const StageConfig& requested);

enum class TestAppChoice { SolsticeTestApp, Desktop, FirstApp };
TestAppChoice choose_test_app(const std::vector<std::string>& app_names);

bool needs_confirmation(int running_app_id, int test_app_id);

enum class ProfileTarget { Docked, Handheld };
ProfileTarget apply_target(bool mode_profiles, bool handheld_when_tested);

// value with at most max_decimals decimals, trailing zeros dropped, using
// decimal_sep as the separator ("12", "7,5").
std::string format_decimal(double value, int max_decimals, char decimal_sep);
