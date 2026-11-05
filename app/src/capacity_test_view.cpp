//
//  capacity_test_view.cpp
//  Moonlight
//
//  Three phases: latency (20 pings after a discarded warm-up), speed (two 5 s
//  downloads, best wins) and bitrate stages (2 s warm-up + 8 s sampled per
//  stage, stop at the first failure). The pure scoring lives in
//  StreamCapacity; this file only drives it and shows the result.
//
//  The video renderers draw full screen (they ignore the view frame), so the
//  live stream is shown behind a dimmed panel instead of in a thumbnail.
//

#include "capacity_test_view.hpp"
#include "AVFrameHolder.hpp"
#include "helper.hpp"
#include <Limelight.h>
#include <algorithm>
#include <cmath>

#if defined(PLATFORM_IOS) || defined(PLATFORM_VISIONOS)
extern void getWindowSize(int* w, int* h);
#endif

using namespace brls;
using namespace brls::literals;

namespace {
constexpr int kPingCount = 20;
constexpr long kPingIntervalMs = 100;
constexpr int kSpeedtestSeconds = 5;
constexpr int kSpeedtestRuns = 2;
constexpr double kSpeedWarmupS = 1.0;
constexpr double kStageWarmupS = 2.0;
constexpr double kStageMeasureS = 8.0;
constexpr auto kSampleInterval = std::chrono::milliseconds(250);
// No new frame for this long during the measured window = connection lost.
constexpr auto kStallLimit = std::chrono::seconds(2);
constexpr float kColumnWidth = 170;

const char* codec_name(VideoCodec codec) {
    switch (codec) {
    case H265:
        return "HEVC";
    case AV1:
        return "AV1";
    case H264:
    default:
        return "H.264";
    }
}

std::string grade_text(LatencyGrade grade) {
    switch (grade) {
    case LatencyGrade::Excellent:
        return "capacity/grade_excellent"_i18n;
    case LatencyGrade::Good:
        return "capacity/grade_good"_i18n;
    case LatencyGrade::Acceptable:
        return "capacity/grade_acceptable"_i18n;
    case LatencyGrade::Poor:
    default:
        return "capacity/grade_poor"_i18n;
    }
}

float finite_or_zero(float v) { return std::isfinite(v) ? v : 0.0f; }

Label* makeLabel(float fontSize, NVGcolor color, bool singleLine = true) {
    auto* label = new Label();
    label->setFontSize(fontSize);
    label->setTextColor(color);
    label->setSingleLine(singleLine);
    return label;
}
} // namespace

void CapacityTestView::open(const Host& host) {
    auto* view = new CapacityTestView(host);
    auto* frame = new AppletFrame(view);
    view->frame = frame;
    Application::pushActivity(new Activity(frame));
}

CapacityTestView::CapacityTestView(const Host& host) : host(host) {
    getAppletFrameItem()->title = "capacity/title"_i18n;
    Application::getPlatform()->disableScreenDimming(true);

    buildLayout();

    // B cancels at any point; it must not just pop the activity.
    registerAction("capacity/cancel"_i18n, BUTTON_B, [this](View*) {
        if (phase == Phase::Ready || phase == Phase::Done ||
            phase == Phase::Loading)
            close();
        else
            cancel();
        return true;
    });

    // HOME menu / sleep would stall the stream and fail the stage for the
    // wrong reason: abort instead.
    windowFocusSubscription =
        Application::getWindowFocusChangedEvent()->subscribe(
            [this](bool focused) {
                if (!focused && !gone() && phase != Phase::Ready &&
                    phase != Phase::Done && phase != Phase::Loading)
                    cancel();
            });

    loadHost();
}

CapacityTestView::~CapacityTestView() {
    *alive = false;
    Application::getWindowFocusChangedEvent()->unsubscribe(
        windowFocusSubscription);
    if (session) {
        session->stop(launchedByTest);
        delete session;
        session = nullptr;
    }
    Application::getPlatform()->disableScreenDimming(false);
}

// MARK: Layout

Box* CapacityTestView::makeCard(const std::string& title, Label** value,
                                Label** detail) {
    auto* card = new Box(Axis::COLUMN);
    card->setGrow(1);
    card->setPadding(14, 18, 14, 18);
    card->setCornerRadius(8);
    card->setBackgroundColor(nvgRGBA(128, 128, 128, 40));

    auto* titleLabel =
        makeLabel(18, Application::getTheme()["brls/text_disabled"]);
    titleLabel->setText(title);
    card->addView(titleLabel);

    *value = makeLabel(28, Application::getTheme()["brls/text"]);
    (*value)->setText("capacity/pending"_i18n);
    (*value)->setMarginTop(6);
    card->addView(*value);

    if (detail) {
        *detail = makeLabel(15, Application::getTheme()["brls/text_disabled"]);
        (*detail)->setText(" ");
        (*detail)->setMarginTop(4);
        card->addView(*detail);
    }
    return card;
}

void CapacityTestView::buildLayout() {
    setAxis(Axis::COLUMN);
    setAlignItems(AlignItems::STRETCH);
    setPadding(24, 40, 16, 40);
    setGrow(1);

    const NVGcolor text = Application::getTheme()["brls/text"];

    introLabel = makeLabel(18, Application::getTheme()["brls/text_disabled"],
                           false);
    introLabel->setText("capacity/intro"_i18n);
    addView(introLabel);

    planLabel = makeLabel(20, text);
    planLabel->setText("capacity/loading"_i18n);
    planLabel->setMarginTop(8);
    addView(planLabel);

    auto* cards = new Box(Axis::ROW);
    cards->setMarginTop(16);
    auto* latencyCard =
        makeCard("capacity/latency"_i18n, &latencyValue, &latencyDetail);
    latencyCard->setMarginRight(12);
    latencyCard->setGrow(1.6f);
    auto* speedCard = makeCard("capacity/speed"_i18n, &speedValue, nullptr);
    speedCard->setMarginRight(12);
    auto* bitrateCard =
        makeCard("capacity/bitrate"_i18n, &bitrateValue, nullptr);
    cards->addView(latencyCard);
    cards->addView(speedCard);
    cards->addView(bitrateCard);
    addView(cards);

    statusLabel = makeLabel(22, Application::getTheme()["brls/accent"]);
    statusLabel->setText(" ");
    statusLabel->setMarginTop(14);
    addView(statusLabel);

    table = new Box(Axis::COLUMN);
    table->setMarginTop(6);
    addView(table);

    verdictLabel = makeLabel(22, text, false);
    verdictLabel->setMarginTop(10);
    verdictLabel->setVisibility(Visibility::GONE);
    addView(verdictLabel);

    warningLabel = makeLabel(18, nvgRGB(255, 167, 38), false);
    warningLabel->setMarginTop(4);
    warningLabel->setVisibility(Visibility::GONE);
    addView(warningLabel);

    auto* spacer = new Box();
    spacer->setGrow(1);
    addView(spacer);

    auto* buttons = new Box(Axis::ROW);
    buttons->setJustifyContent(JustifyContent::FLEX_END);
    buttons->setMarginTop(10);

    startButton = new Button();
    startButton->setStyle(&BUTTONSTYLE_PRIMARY);
    startButton->setText("capacity/start"_i18n);
    startButton->setMarginRight(12);
    startButton->setVisibility(Visibility::GONE);
    startButton->registerClickAction([this](View*) {
        onStartPressed();
        return true;
    });
    buttons->addView(startButton);

    applyButton = new Button();
    applyButton->setStyle(&BUTTONSTYLE_PRIMARY);
    applyButton->setMarginRight(12);
    applyButton->setVisibility(Visibility::GONE);
    applyButton->registerClickAction([this](View*) {
        applyRecommendation();
        return true;
    });
    buttons->addView(applyButton);

    closeButton = new Button();
    closeButton->setStyle(&BUTTONSTYLE_BORDERED);
    closeButton->setText("capacity/close"_i18n);
    closeButton->registerClickAction([this](View*) {
        if (phase == Phase::Ready || phase == Phase::Done ||
            phase == Phase::Loading)
            close();
        else
            cancel();
        return true;
    });
    buttons->addView(closeButton);
    addView(buttons);
}

void CapacityTestView::setStreamingBackdrop(bool streaming) {
    // Video is drawn under the UI layer: drop the opaque frame background
    // while streaming so the stream shows behind a dimmed panel.
    if (frame) {
        if (streaming)
            frame->setBackground(ViewBackground::NONE);
        else
            frame->setBackgroundColor(
                Application::getTheme()["brls/background"]);
    }
    if (streaming)
        setBackgroundColor(nvgRGBA(0, 0, 0, 150));
    else
        setBackground(ViewBackground::NONE);
}

std::string CapacityTestView::dec(double value, int decimals) const {
    const char sep =
        Application::getLocale().rfind("pt", 0) == 0 ? ',' : '.';
    return format_decimal(value, decimals, sep);
}

std::string CapacityTestView::mbps(int kbps) const {
    return dec(kbps / 1000.0);
}

// MARK: Setup

void CapacityTestView::loadHost() {
    phase = Phase::Loading;
    auto token = alive;
    GameStreamClient::instance().connect(
        host, [this, token](const GSResult<SERVER_DATA>& result) {
            if (!*token || closing)
                return;
            if (!result.isSuccess()) {
                showError(result.error(), [this, token] {
                    if (*token)
                        close();
                });
                return;
            }
            address = GameStreamClient::instance().active_address(host);
            isSunshine = result.value().isSunshine();
            runningAppId = result.value().currentGame;

            GameStreamClient::instance().applist(
                host, [this, token](const GSResult<AppInfoList>& list) {
                    if (!*token || closing)
                        return;
                    if (!list.isSuccess()) {
                        showError(list.error(), [this, token] {
                            if (*token)
                                close();
                        });
                        return;
                    }
                    apps = list.value();
                    if (apps.empty()) {
                        showError("capacity/no_apps"_i18n, [this, token] {
                            if (*token)
                                close();
                        });
                        return;
                    }
                    std::vector<std::string> names;
                    for (const auto& app : apps)
                        names.push_back(app.name);
                    const auto choice = choose_test_app(names);
                    const char* wanted =
                        choice == TestAppChoice::SolsticeTestApp
                            ? "Solstice Network Test"
                        : choice == TestAppChoice::Desktop ? "Desktop"
                                                           : nullptr;
                    testApp = apps.front();
                    if (wanted) {
                        for (const auto& app : apps)
                            if (app.name == wanted) {
                                testApp = app;
                                break;
                            }
                    }
                    usedFallbackApp =
                        choice != TestAppChoice::SolsticeTestApp;
                    showReady();
                });
        });
}

void CapacityTestView::computeRequestedConfig() {
    auto& settings = Settings::instance();
    handheldWhenTested = settings.use_handheld_profile();
    const int resolution = handheldWhenTested ? settings.handheld_resolution()
                                              : settings.resolution();
    int nativeW = 0, nativeH = 0;
    if (resolution == -1) {
#if defined(PLATFORM_IOS) || defined(PLATFORM_VISIONOS)
        getWindowSize(&nativeW, &nativeH);
#else
        nativeW = Application::windowWidth;
        nativeH = Application::windowHeight;
#endif
    }
    auto [w, h] = stream_size_for(resolution, nativeW, nativeH,
                                  settings.native_resolution_scale());
    requested.width = w;
    requested.height = h;
    requested.fps = settings.fps();
    requested.codec = settings.video_codec();
    requested.bitrate_kbps = 0;
    downgradedFrom.reset();
}

void CapacityTestView::showReady() {
    phase = Phase::Ready;
    computeRequestedConfig();
    planLabel->setText(brls::getStr("capacity/plan", requested.height,
                                    requested.fps,
                                    codec_name(requested.codec),
                                    testApp.name));
    startButton->setVisibility(Visibility::VISIBLE);
    closeButton->setText("capacity/close"_i18n);
    Application::giveFocus(startButton);
}

void CapacityTestView::onStartPressed() {
    if (phase != Phase::Ready)
        return;
    computeRequestedConfig();
    planLabel->setText(brls::getStr("capacity/plan", requested.height,
                                    requested.fps,
                                    codec_name(requested.codec),
                                    testApp.name));

    if (needs_confirmation(runningAppId, testApp.app_id)) {
        std::string running = std::to_string(runningAppId);
        for (const auto& app : apps)
            if (app.app_id == runningAppId)
                running = app.name;

        auto token = alive;
        auto* dialog =
            new Dialog(brls::getStr("capacity/confirm", running));
        dialog->addButton("capacity/cancel"_i18n, [] {});
        dialog->addButton("capacity/continue"_i18n, [this, token] {
            if (*token && !closing && phase == Phase::Ready)
                quitRunningAppThenStart();
        });
        dialog->open();
        return;
    }

    // Already running the test app: resume it and leave it running after.
    launchedByTest = runningAppId != testApp.app_id;
    beginProbes();
}

void CapacityTestView::quitRunningAppThenStart() {
    phase = Phase::Preparing;
    startButton->setVisibility(Visibility::GONE);
    closeButton->setText("capacity/cancel"_i18n);
    Application::giveFocus(closeButton);
    statusLabel->setText("capacity/loading"_i18n);

    auto token = alive;
    GameStreamClient::instance().quit(
        address, [this, token](const GSResult<bool>& result) {
            if (!*token || closing)
                return;
            if (!result.isSuccess()) {
                statusLabel->setText(" ");
                showError(result.error());
                showReady();
                return;
            }
            // The cached server data still lists the old app; refresh it so
            // the first stage launches instead of resuming.
            GameStreamClient::instance().connect(
                host, [this, token](const GSResult<SERVER_DATA>& result) {
                    if (!*token || closing)
                        return;
                    if (!result.isSuccess()) {
                        statusLabel->setText(" ");
                        showError(result.error());
                        showReady();
                        return;
                    }
                    address =
                        GameStreamClient::instance().active_address(host);
                    runningAppId = result.value().currentGame;
                    launchedByTest = runningAppId != testApp.app_id;
                    beginProbes();
                });
        });
}

// MARK: Phase 1 - latency

void CapacityTestView::beginProbes() {
    phase = Phase::Latency;
    startButton->setVisibility(Visibility::GONE);
    closeButton->setText("capacity/cancel"_i18n);
    Application::giveFocus(closeButton);
    statusLabel->setText(brls::getStr("capacity/latency_progress", 0,
                                      kPingCount));
    pingSamples.clear();
    pingWarmup();
}

void CapacityTestView::markProbesUnsupported() {
    probesSupported = false;
    latency.reset();
    speedMbps.reset();
    latencyValue->setText("capacity/not_supported"_i18n);
    latencyDetail->setText(" ");
    speedValue->setText("capacity/not_supported"_i18n);
}

void CapacityTestView::pingWarmup() {
    // Pays the TLS handshake under the 1 s timeout: never counted, and its
    // failure is ignored unless the host says the endpoint doesn't exist.
    auto token = alive;
    GameStreamClient::instance().ping(
        address, [this, token](const GSResult<float>& result) {
            if (!*token || closing)
                return;
            if (!result.isSuccess() && result.error() == "not_supported") {
                markProbesUnsupported();
                afterSpeed();
                return;
            }
            pingNext();
        });
}

void CapacityTestView::pingNext() {
    auto token = alive;
    GameStreamClient::instance().ping(
        address, [this, token](const GSResult<float>& result) {
            if (!*token || closing)
                return;
            if (result.isSuccess()) {
                pingSamples.push_back(result.value());
            } else if (result.error() == "not_supported") {
                markProbesUnsupported();
                afterSpeed();
                return;
            } else {
                pingSamples.push_back(std::nullopt);
            }

            statusLabel->setText(brls::getStr("capacity/latency_progress",
                                              (int)pingSamples.size(),
                                              kPingCount));

            if ((int)pingSamples.size() < kPingCount) {
                brls::delay(kPingIntervalMs, [this, token] {
                    if (*token && !closing)
                        pingNext();
                });
                return;
            }

            latency = summarize_latency(pingSamples);
            if (latency->loss_pct >= 100.0f) {
                latencyValue->setText("capacity/pending"_i18n);
            } else {
                latencyValue->setText(brls::getStr(
                    "capacity/latency_value", dec(latency->avg_ms),
                    grade_text(grade_latency(latency->avg_ms))));
            }
            latencyDetail->setText(brls::getStr(
                "capacity/latency_detail", dec(latency->min_ms),
                dec(latency->max_ms), dec(latency->jitter_ms),
                dec(latency->loss_pct)));
            beginSpeed();
        });
}

// MARK: Phase 2 - speed

void CapacityTestView::beginSpeed() {
    phase = Phase::Speed;
    speedRun = 0;
    bestRunMbps = 0;
    runSpeedtest();
}

void CapacityTestView::runSpeedtest() {
    speedProgress = std::make_shared<SpeedProgress>();
    statusLabel->setText(
        brls::getStr("capacity/speed_progress", dec(0), dec(0)));

    auto token = alive;
    auto progress = speedProgress;
    GameStreamClient::instance().speedtest(
        address, kSpeedtestSeconds,
        // Worker thread: record, then hop to the UI thread to redraw.
        [this, token, progress](double elapsed, uint64_t bytes) {
            {
                std::lock_guard<std::mutex> lock(progress->mutex);
                progress->samples.emplace_back(elapsed, bytes);
            }
            brls::sync([this, token, progress] {
                if (*token && !closing && progress == speedProgress)
                    updateSpeedProgress();
            });
        },
        [this, token, progress](const GSResult<bool>& result) {
            if (!*token || closing)
                return;
            if (!result.isSuccess() && result.error() == "not_supported") {
                speedMbps.reset();
                speedValue->setText("capacity/not_supported"_i18n);
                afterSpeed();
                return;
            }
            if (result.isSuccess()) {
                std::lock_guard<std::mutex> lock(progress->mutex);
                bestRunMbps = std::max(
                    bestRunMbps,
                    throughput_mbps(progress->samples, kSpeedWarmupS));
            }
            if (++speedRun < kSpeedtestRuns) {
                runSpeedtest();
                return;
            }
            if (bestRunMbps > 0) {
                speedMbps = bestRunMbps;
                speedValue->setText(
                    brls::getStr("capacity/mbps", dec(bestRunMbps)));
            } else {
                speedMbps.reset();
                speedValue->setText("capacity/pending"_i18n);
            }
            afterSpeed();
        });
}

void CapacityTestView::updateSpeedProgress() {
    double elapsed = 0;
    float live = 0;
    {
        std::lock_guard<std::mutex> lock(speedProgress->mutex);
        if (!speedProgress->samples.empty())
            elapsed = speedProgress->samples.back().first;
        live = throughput_mbps(speedProgress->samples, kSpeedWarmupS);
    }
    statusLabel->setText(
        brls::getStr("capacity/speed_progress", dec(elapsed), dec(live)));
}

void CapacityTestView::afterSpeed() {
    const auto suggestion = suggest_resolution(speedMbps, requested.height,
                                               requested.fps, requested.codec);
    if (!suggestion) {
        beginStages();
        return;
    }

    // Not cancelable: B would leave the test stuck between phases.
    auto token = alive;
    const int suggested = *suggestion;
    auto* dialog = new Dialog(brls::getStr("capacity/suggest", requested.height,
                                           requested.fps, suggested));
    dialog->setCancelable(false);
    dialog->addButton("capacity/no"_i18n, [this, token] {
        if (*token && !closing)
            beginStages();
    });
    dialog->addButton("capacity/yes"_i18n, [this, token, suggested] {
        if (!*token || closing)
            return;
        downgradedFrom = requested.height;
        auto [w, h] = stream_size_for(suggested, 0, 0, 100);
        requested.width = w;
        requested.height = h;
        planLabel->setText(brls::getStr("capacity/plan", requested.height,
                                        requested.fps,
                                        codec_name(requested.codec),
                                        testApp.name));
        beginStages();
    });
    dialog->open();
}

// MARK: Phase 3 - bitrate stages

void CapacityTestView::beginStages() {
    if (closing)
        return;
    phase = Phase::Stages;
    const int minKbps = min_recommended_bitrate_kbps(
        requested.width, requested.height, requested.fps, requested.codec);
    plan = plan_stage_bitrates(minKbps, stage_ceiling_kbps(speedMbps));
    stageIndex = 0;
    results.clear();
    if (plan.empty()) {
        finish();
        return;
    }
    startStage();
}

void CapacityTestView::startStage() {
    if (closing)
        return;
    stageConfig = requested;
    stageConfig.bitrate_kbps = plan[stageIndex];
    statusLabel->setText(brls::getStr("capacity/stage_progress",
                                      (int)stageIndex + 1, (int)plan.size(),
                                      mbps(stageConfig.bitrate_kbps)));
    setStreamingBackdrop(true);

    // One session per stage: fresh decoder stats and generation. The
    // previous one is already deleted (its destructor clears the active
    // session pointer, so order matters).
    session = new MoonlightSession(address, testApp.app_id);
    session->set_config_override(stageConfig);
    sessionStarting = true;
    sessionStreaming = false;
    stageEnding = false;
    samples.clear();

    auto token = alive;
    // The success path calls back from a worker thread.
    session->start(
        [this, token](const GSResult<bool>& result) {
            const bool ok = result.isSuccess();
            brls::sync([this, token, ok] {
                if (*token)
                    onStageStarted(ok);
            });
        },
        isSunshine);
}

void CapacityTestView::onStageStarted(bool ok) {
    sessionStarting = false;
    if (phase == Phase::Cancelling) {
        // Cancel waited for the start to finish before tearing down.
        closeSession(launchedByTest);
        close();
        return;
    }
    if (!ok) {
        finishStage(true);
        return;
    }
    sessionStreaming = true;
    stageStart = std::chrono::steady_clock::now();
    lastSample = {};
    lastFrames = 0;
}

void CapacityTestView::draw(NVGcontext* vg, float x, float y, float width,
                            float height, Style style, FrameContext* ctx) {
    if (session && sessionStreaming && !stageEnding) {
        // Also refreshes session_stats() (every 250 ms).
        session->draw(vg, (int)Application::contentWidth,
                      (int)Application::contentHeight);
        tickStage();
    }
    Box::draw(vg, x, y, width, height, style, ctx);
}

void CapacityTestView::tickStage() {
    using namespace std::chrono;
    if (session->is_terminated()) {
        requestStageEnd(true);
        return;
    }

    const auto now = steady_clock::now();
    const double elapsed = duration<double>(now - stageStart).count();
    if (elapsed < kStageWarmupS)
        return;

    if (samples.empty() || now - lastSample >= kSampleInterval) {
        lastSample = now;
        // Same fields as StreamingView::buildStatsText.
        const auto* stats = session->session_stats();
        const auto& ds = stats->video_decode_stats;
        const auto& rs = stats->video_render_stats;
        uint32_t rtt = 0, rttVariance = 0;
        const bool hasRtt = LiGetEstimatedRttInfo(&rtt, &rttVariance);
        const float networkMs = hasRtt ? rtt / 2.0f : 0.0f;
        const float queueMs =
            AVFrameHolder::instance().getFrameQueueSize() *
            (1000.0f / (float)std::max(session->stream_fps(), 1));
        const float decodeMs = finite_or_zero(ds.current_decoding_time);
        const float totalMs =
            networkMs + finite_or_zero(ds.current_receive_time) + decodeMs +
            queueMs + finite_or_zero(rs.rendering_time);

        StageSample sample{};
        sample.rtt_ms = hasRtt ? rtt : 0;
        sample.rtt_variance_ms = hasRtt ? rttVariance : 0;
        sample.decode_ms = decodeMs;
        sample.total_latency_ms = totalMs;
        // Cumulative: frames received so far plus the ones the network lost.
        sample.dropped_frames = ds.network_dropped_frames;
        sample.frames = ds.total_received_frames + ds.current_received_frames +
                        ds.network_dropped_frames;
        sample.poor_connection = session->connection_status_is_poor();

        if (samples.empty() || sample.frames != lastFrames) {
            lastFrames = sample.frames;
            lastFrameAdvance = now;
        }
        samples.push_back(sample);

        if (now - lastFrameAdvance >= kStallLimit) {
            requestStageEnd(true);
            return;
        }
    }

    if (elapsed >= kStageWarmupS + kStageMeasureS)
        requestStageEnd(false);
}

void CapacityTestView::requestStageEnd(bool disconnected) {
    if (stageEnding)
        return;
    stageEnding = true;
    // Leave draw() before tearing the renderer down.
    auto token = alive;
    brls::sync([this, token, disconnected] {
        if (*token && !closing && phase == Phase::Stages)
            finishStage(disconnected);
    });
}

void CapacityTestView::finishStage(bool disconnected) {
    if (closing)
        return;
    sessionStreaming = false;
    const StageResult result =
        evaluate_stage(stageConfig, samples, disconnected);
    results.push_back(result);
    refreshTable();

    const bool next = result.passed && stageIndex + 1 < plan.size();
    // Between stages keep the app running (the next stage resumes it); after
    // the last one close it only if the test opened it.
    closeSession(next ? false : launchedByTest);

    if (next) {
        stageIndex++;
        startStage();
    } else {
        finish();
    }
}

void CapacityTestView::closeSession(bool terminateApp) {
    if (!session)
        return;
    session->stop(terminateApp ? 1 : 0);
    delete session;
    session = nullptr;
}

// MARK: Result

void CapacityTestView::refreshTable() {
    table->clearViews();
    const NVGcolor dim = Application::getTheme()["brls/text_disabled"];
    const NVGcolor text = Application::getTheme()["brls/text"];

    auto addRow = [&](const std::vector<std::string>& cells, NVGcolor color,
                      std::optional<NVGcolor> lastColor = std::nullopt) {
        auto* row = new Box(Axis::ROW);
        row->setMarginTop(2);
        for (size_t i = 0; i < cells.size(); i++) {
            auto* label = makeLabel(17, (i + 1 == cells.size() && lastColor)
                                            ? *lastColor
                                            : color);
            label->setWidth(kColumnWidth);
            label->setText(cells[i]);
            row->addView(label);
        }
        table->addView(row);
    };

    addRow({"capacity/col_bitrate"_i18n, "capacity/col_rtt"_i18n,
            "capacity/col_latency"_i18n, "capacity/col_loss"_i18n,
            "capacity/col_result"_i18n},
           dim);
    for (const auto& r : results) {
        const bool hasData = r.avg_rtt_ms > 0 || r.avg_latency_ms > 0;
        addRow({mbps(r.config.bitrate_kbps),
                hasData ? dec(r.avg_rtt_ms) + " ms" : "capacity/pending"_i18n,
                hasData ? dec(r.avg_latency_ms) + " ms"
                        : "capacity/pending"_i18n,
                dec(r.drop_pct) + "%",
                // Material icons: check / close.
                r.passed ? "" : ""},
               text, r.passed ? nvgRGB(76, 175, 80) : nvgRGB(229, 57, 53));
    }
}

void CapacityTestView::finish() {
    phase = Phase::Done;
    setStreamingBackdrop(false);
    statusLabel->setText(" ");

    verdict = summarize(latency, speedMbps, results, usedFallbackApp,
                        requested);

    if (verdict->best) {
        bitrateValue->setText(brls::getStr(
            "capacity/mbps", mbps(verdict->best->config.bitrate_kbps)));
    } else {
        bitrateValue->setText("capacity/pending"_i18n);
    }

    std::string text;
    if (verdict->recommended_kbps > 0) {
        const std::string avgLatency =
            verdict->latency && verdict->latency->loss_pct < 100.0f
                ? dec(verdict->latency->avg_ms)
                : "capacity/pending"_i18n;
        text = brls::getStr("capacity/verdict", mbps(verdict->recommended_kbps),
                            requested.height, requested.fps, avgLatency);
    } else {
        text = brls::getStr("capacity/verdict_none", requested.height,
                            requested.fps);
    }
    // The connection is too slow for the resolution the user configured:
    // say so, whether or not they accepted the lower test resolution.
    const int originalHeight = downgradedFrom.value_or(requested.height);
    if (downgradedFrom) {
        text += "\n" + brls::getStr("capacity/suggest_result", originalHeight,
                                    requested.fps, requested.height);
    } else if (verdict->suggested_resolution) {
        text += "\n" + brls::getStr("capacity/suggest_result", originalHeight,
                                    requested.fps,
                                    *verdict->suggested_resolution);
    }
    verdictLabel->setText(text);
    verdictLabel->setVisibility(Visibility::VISIBLE);

    if (verdict->optimistic) {
        warningLabel->setText("capacity/optimistic"_i18n);
        warningLabel->setVisibility(Visibility::VISIBLE);
    }

    closeButton->setText("capacity/close"_i18n);
    if (verdict->recommended_kbps > 0) {
        applyButton->setText(
            brls::getStr("capacity/apply", mbps(verdict->recommended_kbps)));
        applyButton->setVisibility(Visibility::VISIBLE);
        Application::giveFocus(applyButton);
    } else {
        Application::giveFocus(closeButton);
    }
}

void CapacityTestView::applyRecommendation() {
    if (phase != Phase::Done || !verdict || verdict->recommended_kbps <= 0)
        return;
    auto& settings = Settings::instance();
    const int kbps = verdict->recommended_kbps;
    // Only the profile the test ran with.
    if (apply_target(settings.mode_profiles(), handheldWhenTested) ==
        ProfileTarget::Handheld)
        settings.set_handheld_bitrate(kbps);
    else
        settings.set_bitrate(kbps);
    settings.save();
    Application::notify(brls::getStr("capacity/applied", mbps(kbps)));
    close();
}

// MARK: Cancel / close

void CapacityTestView::cancel() {
    if (closing || phase == Phase::Cancelling)
        return;
    // Never touches settings. Stops the stream and closes the app only if
    // the test opened it.
    if (sessionStarting) {
        // The session is still inside LiStartConnection on a worker thread;
        // tear it down once start() reports back (onStageStarted).
        phase = Phase::Cancelling;
        statusLabel->setText("capacity/cancelling"_i18n);
        return;
    }
    closeSession(launchedByTest);
    close();
}

void CapacityTestView::close() {
    if (closing)
        return;
    closing = true;
    sessionStreaming = false;
    setStreamingBackdrop(false);
    this->dismiss();
}
