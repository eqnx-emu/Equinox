#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "StreamCapacity.hpp"

#include <optional>
#include <string>
#include <vector>

TEST_CASE("min bitrate table H264 60fps") {
    CHECK(min_recommended_bitrate_kbps(1280, 720, 60, H264) == 8000);
    CHECK(min_recommended_bitrate_kbps(1920, 1080, 60, H264) == 15000);
    CHECK(min_recommended_bitrate_kbps(2560, 1440, 60, H264) == 30000);
    CHECK(min_recommended_bitrate_kbps(854, 480, 30, H264) == 2000);
}

TEST_CASE("codec factors") {
    CHECK(min_recommended_bitrate_kbps(1920, 1080, 60, H265) == 10500);
    CHECK(min_recommended_bitrate_kbps(1920, 1080, 60, AV1) == 9000);
}

TEST_CASE("fps factor above 60") {
    CHECK(min_recommended_bitrate_kbps(1920, 1080, 120, H264) == 21500);
}

TEST_CASE("floor and ceiling") {
    CHECK(min_recommended_bitrate_kbps(640, 360, 30, H265) >= 1000);
    CHECK(min_recommended_bitrate_kbps(2560, 1440, 120, H264) <= 100000);
    CHECK(min_recommended_bitrate_kbps(7680, 4320, 120, H264) == 100000);
}

TEST_CASE("interpolation between table points") {
    int v = min_recommended_bitrate_kbps(1600, 900, 60, H264);
    CHECK(v > 8000);
    CHECK(v < 15000);
}

TEST_CASE("stream_size_for") {
    CHECK(stream_size_for(720, 0, 0, 100) == std::make_pair(1280, 720));
    CHECK(stream_size_for(1080, 0, 0, 100) == std::make_pair(1920, 1080));
    CHECK(stream_size_for(-1, 1280, 720, 75) == std::make_pair(960, 540));
    CHECK(stream_size_for(-1, 1280, 720, 100) == std::make_pair(1280, 720));
    CHECK(stream_size_for(-1, 1281, 721, 100) == std::make_pair(1282, 722));
    CHECK(stream_size_for(-1, 0, 0, 50) == std::make_pair(0, 0));
}

// ---- Capacity test logic ----

static StageSample sample(uint32_t var, float decode, uint32_t frames,
                          uint32_t dropped, bool poor = false) {
    StageSample s{};
    s.rtt_ms = 5;
    s.rtt_variance_ms = var;
    s.decode_ms = decode;
    s.total_latency_ms = 20.0f;
    s.frames = frames;
    s.dropped_frames = dropped;
    s.poor_connection = poor;
    return s;
}

static StageConfig cfg(int h, int fps, int kbps) {
    return {h * 16 / 9, h, fps, kbps, H264};
}

static StageResult res(int kbps, bool passed, bool disc = false) {
    StageResult r{};
    r.config = cfg(1080, 60, kbps);
    r.passed = passed;
    r.disconnected = disc;
    return r;
}

TEST_CASE("summarize_latency") {
    auto r = summarize_latency({2.f, 4.f, 3.f, std::nullopt, 5.f});
    CHECK(r.min_ms == doctest::Approx(2.0f));
    CHECK(r.max_ms == doctest::Approx(5.0f));
    CHECK(r.avg_ms == doctest::Approx(3.5f));
    CHECK(r.loss_pct == doctest::Approx(20.0f));
    CHECK(r.jitter_ms == doctest::Approx(5.0f / 3.0f));
    auto e = summarize_latency({});
    CHECK(e.loss_pct == doctest::Approx(0.0f));
    auto all = summarize_latency({std::nullopt, std::nullopt});
    CHECK(all.loss_pct == doctest::Approx(100.0f));
}

TEST_CASE("grade_latency") {
    CHECK(grade_latency(4.9f) == LatencyGrade::Excellent);
    CHECK(grade_latency(14.9f) == LatencyGrade::Good);
    CHECK(grade_latency(29.9f) == LatencyGrade::Acceptable);
    CHECK(grade_latency(30.0f) == LatencyGrade::Poor);
}

TEST_CASE("throughput_mbps") {
    CHECK(throughput_mbps({{0, 0}, {1, 1'000'000}, {5, 51'000'000}}, 1.0) ==
          doctest::Approx(100.0f));
    CHECK(throughput_mbps({{0, 0}}, 1.0) == doctest::Approx(0.0f));
}

TEST_CASE("stage ceiling") {
    // optimistic: min(100000, 0.8 x speed)
    CHECK(stage_ceiling_kbps(50.0f, true) == 40000);
    CHECK(stage_ceiling_kbps(std::nullopt, true) == 100000);
    CHECK(stage_ceiling_kbps(500.0f, true) == 100000);
    // Solstice test app: speed is not a cap
    CHECK(stage_ceiling_kbps(50.0f, false) == 100000);
    CHECK(stage_ceiling_kbps(5.0f, false) == 100000);
    CHECK(stage_ceiling_kbps(std::nullopt, false) == 100000);
}

TEST_CASE("plan_stage_bitrates") {
    CHECK(plan_stage_bitrates(8000, 100000) ==
          std::vector<int>{8000, 12000, 18000, 27000, 40500, 61000});
    CHECK(plan_stage_bitrates(30000, 40000) == std::vector<int>{30000});
}

TEST_CASE("plan_stage_bitrates starts at the ceiling when min exceeds it") {
    // 9800 is not a 500 multiple: rounding to nearest would give 10000 > max.
    CHECK(plan_stage_bitrates(15000, 9800) == std::vector<int>{9500});
    CHECK(plan_stage_bitrates(15000, 9600) == std::vector<int>{9500});
    CHECK(plan_stage_bitrates(15000, 400).empty());
}

TEST_CASE("format_decimal") {
    CHECK(format_decimal(12.0, 1, '.') == "12");
    CHECK(format_decimal(7.5, 1, ',') == "7,5");
    CHECK(format_decimal(3.26, 1, '.') == "3.3");
    CHECK(format_decimal(84.04, 1, ',') == "84");
    CHECK(format_decimal(0.0, 1, '.') == "0");
    CHECK(format_decimal(1.05, 2, ',') == "1,05");
}

TEST_CASE("evaluate_stage") {
    auto c = cfg(1080, 60, 15000);
    auto good = [](uint32_t var, float dec, uint32_t drop) {
        return std::vector<StageSample>{sample(var, dec, 0, 0),
                                        sample(var, dec, 1000, drop)};
    };
    CHECK(evaluate_stage(c, good(2, 5.f, 5), false).passed);   // 0.5%
    CHECK_FALSE(evaluate_stage(c, good(2, 5.f, 15), false).passed); // 1.5%
    CHECK_FALSE(evaluate_stage(c, good(2, 14.f, 0), false).passed);
    CHECK(evaluate_stage(c, good(2, 13.f, 0), false).passed);
    CHECK_FALSE(evaluate_stage(c, good(12, 5.f, 0), false).passed);
    CHECK_FALSE(evaluate_stage(c, good(2, 5.f, 0), true).passed);
    CHECK_FALSE(evaluate_stage(c, {}, false).passed);
    auto poor = std::vector<StageSample>{sample(2, 5.f, 0, 0, true),
                                         sample(2, 5.f, 500, 0, true),
                                         sample(2, 5.f, 1000, 0, false)};
    CHECK_FALSE(evaluate_stage(c, poor, false).passed);
    // cumulative counters use first->last delta
    auto cum = std::vector<StageSample>{sample(2, 5.f, 1000, 500),
                                        sample(2, 5.f, 2000, 505)};
    auto r = evaluate_stage(c, cum, false);
    CHECK(r.drop_pct == doctest::Approx(0.5f));
    CHECK(r.passed);
}

TEST_CASE("CapacityTest.SuggestsLowerResolutionWhenSpeedTooLow") {
    auto h = suggest_resolution(12.0f, 1080, 60, H264);
    REQUIRE(h.has_value());
    CHECK(*h == 720);
    CHECK_FALSE(suggest_resolution(100.0f, 1080, 60, H264).has_value());
    CHECK_FALSE(suggest_resolution(std::nullopt, 1080, 60, H264).has_value());

    StageResult p = res(8000, true);
    auto v = summarize(std::nullopt, 12.0f, {p}, false, cfg(1080, 60, 15000));
    REQUIRE(v.suggested_resolution.has_value());
    CHECK(*v.suggested_resolution == 720);
    CHECK(v.recommended_kbps == 8000);
    auto none = summarize(std::nullopt, 12.0f, {}, false, cfg(1080, 60, 15000));
    CHECK(none.recommended_kbps == 0);
}

TEST_CASE("summarize optimistic caps at 0.7 x speed") {
    auto v = summarize(std::nullopt, 30.0f, {res(27000, true)}, true,
                       cfg(1080, 60, 15000));
    CHECK(v.optimistic);
    CHECK(v.recommended_kbps == 21000);
    auto n = summarize(std::nullopt, 60.0f, {res(27000, true)}, false,
                       cfg(1080, 60, 15000));
    CHECK_FALSE(n.optimistic);
    CHECK(n.recommended_kbps == 27000);
}

TEST_CASE("summarize non-optimistic does not cap by speed") {
    auto v = summarize(std::nullopt, 10.0f, {res(27000, true)}, false,
                       cfg(1080, 60, 15000));
    CHECK(v.recommended_kbps == 27000);
    // the speed still drives the resolution suggestion
    REQUIRE(v.suggested_resolution.has_value());
}

TEST_CASE("summarize optimistic rounding survives float noise") {
    // 0.7 * 45 * 1000 is 31499.999999999996 in double: must still give 31500.
    auto v = summarize(std::nullopt, 45.0f, {res(40500, true)}, true,
                       cfg(1080, 60, 15000));
    CHECK(v.recommended_kbps == 31500);
}

TEST_CASE("CapacityTest.PicksDesktopFallbackAndFlagsOptimistic") {
    CHECK(choose_test_app({"Steam", "Desktop", "Solstice Network Test"}) ==
          TestAppChoice::SolsticeTestApp);
    CHECK(choose_test_app({"Steam", "Desktop"}) == TestAppChoice::Desktop);
    CHECK(choose_test_app({"Steam", "Game"}) == TestAppChoice::FirstApp);
    auto v = summarize(std::nullopt, std::nullopt, {res(15000, true)}, true,
                       cfg(1080, 60, 15000));
    CHECK(v.optimistic);
    CHECK(v.recommended_kbps == 15000);
}

TEST_CASE("CapacityTest.RequiresConfirmationWhenOtherAppRunning") {
    CHECK_FALSE(needs_confirmation(0, 5));
    CHECK_FALSE(needs_confirmation(5, 5));
    CHECK(needs_confirmation(7, 5));
}

TEST_CASE("CapacityTest.StopsOnDisconnectAndKeepsBestPass") {
    auto v = summarize(std::nullopt, std::nullopt,
                       {res(8000, true), res(12000, true),
                        res(18000, false, true)},
                       false, cfg(1080, 60, 15000));
    REQUIRE(v.best.has_value());
    CHECK(v.best->config.bitrate_kbps == 12000);
    CHECK(v.recommended_kbps == 12000);
}

TEST_CASE("CapacityTest.CancelDoesNotTouchSettings") {
    auto v = summarize(std::nullopt, std::nullopt, {}, false,
                       cfg(1080, 60, 15000));
    CHECK_FALSE(v.best.has_value());
    CHECK(v.recommended_kbps == 0);
}

TEST_CASE("CapacityTest.ApplyTargetsProfileUsedByTest") {
    CHECK(apply_target(true, true) == ProfileTarget::Handheld);
    CHECK(apply_target(true, false) == ProfileTarget::Docked);
    CHECK(apply_target(false, true) == ProfileTarget::Docked);
    CHECK(apply_target(false, false) == ProfileTarget::Docked);
}

static LatencyResult lat(float avg, float jitter, float loss = 0.0f) {
    LatencyResult l{};
    l.min_ms = avg;
    l.avg_ms = avg;
    l.max_ms = avg;
    l.jitter_ms = jitter;
    l.loss_pct = loss;
    return l;
}

static CapacityVerdict verdict_with(std::optional<LatencyResult> latency,
                                    int best_kbps, float rtt_var = 0.0f,
                                    float drop = 0.0f) {
    CapacityVerdict v{};
    v.latency = latency;
    if (best_kbps > 0) {
        StageResult r = res(best_kbps, true);
        r.avg_rtt_variance_ms = rtt_var;
        r.drop_pct = drop;
        v.best = r;
        v.recommended_kbps = best_kbps;
    }
    return v;
}

TEST_CASE("Recommend.StableLinkPrefersLowestLatencyPacing") {
    auto r = recommend_settings(verdict_with(lat(2, 0.3f), 40000), std::nullopt);
    CHECK(r.bitrate_kbps == 40000);
    CHECK_FALSE(r.resolution.has_value());
    REQUIRE(r.frame_pacing.has_value());
    CHECK(*r.frame_pacing == 1);
}

TEST_CASE("Recommend.SomeJitterPicksBalancedPacing") {
    auto r = recommend_settings(verdict_with(lat(8, 4.0f), 20000), std::nullopt);
    REQUIRE(r.frame_pacing.has_value());
    CHECK(*r.frame_pacing == 2);
}

TEST_CASE("Recommend.HighJitterOrLossPicksSmoothPacing") {
    auto jittery = recommend_settings(verdict_with(lat(20, 12.0f), 15000),
                                      std::nullopt);
    REQUIRE(jittery.frame_pacing.has_value());
    CHECK(*jittery.frame_pacing == 3);

    auto lossy = recommend_settings(verdict_with(lat(5, 0.5f, 3.0f), 15000),
                                    std::nullopt);
    REQUIRE(lossy.frame_pacing.has_value());
    CHECK(*lossy.frame_pacing == 3);
}

TEST_CASE("Recommend.StreamJitterCountsToo") {
    // Ping jitter is low but the stream itself saw RTT variance.
    auto r = recommend_settings(verdict_with(lat(2, 0.3f), 20000, 5.0f),
                                std::nullopt);
    REQUIRE(r.frame_pacing.has_value());
    CHECK(*r.frame_pacing == 2);
}

TEST_CASE("Recommend.DroppedFramesNeverGetLowestLatency") {
    auto r = recommend_settings(verdict_with(lat(2, 0.3f), 20000, 0.0f, 0.5f),
                                std::nullopt);
    REQUIRE(r.frame_pacing.has_value());
    CHECK(*r.frame_pacing >= 2);
}

TEST_CASE("Recommend.ResolutionFromDowngradeThenSpeedSuggestion") {
    auto v = verdict_with(lat(2, 0.3f), 10000);
    v.suggested_resolution = 720;
    CHECK(recommend_settings(v, 540).resolution == std::optional<int>(540));
    CHECK(recommend_settings(v, std::nullopt).resolution ==
          std::optional<int>(720));
}

TEST_CASE("Recommend.NothingWhenNoStagePassed") {
    auto r = recommend_settings(verdict_with(lat(2, 0.3f), 0), std::nullopt);
    CHECK(r.bitrate_kbps == 0);
    CHECK_FALSE(r.frame_pacing.has_value());
    CHECK_FALSE(r.resolution.has_value());
}

TEST_CASE("Recommend.NoLatencyDataKeepsPacingFromStream") {
    auto r = recommend_settings(verdict_with(std::nullopt, 20000, 0.0f),
                                std::nullopt);
    REQUIRE(r.frame_pacing.has_value());
    CHECK(*r.frame_pacing == 1);
}
