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
#include "StageRetry.hpp"
#include "helper.hpp"
#include <Limelight.h>
#include <algorithm>
#include <cmath>


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
constexpr float kColumnWidth = 104;
// Left column of the body (progress and results); the preview takes the rest.
constexpr float kLogColumnWidth = 540;

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

namespace {
// While a stage streams, the frame paints the theme background itself with
// a hole at the preview rectangle, so only that rectangle shows the video
// (drawn under the UI layer) and the rest of the screen keeps its colour.
class PreviewFrame : public AppletFrame {
  public:
    PreviewFrame(CapacityTestView* view) : AppletFrame(view), view(view) {}

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              Style style, FrameContext* ctx) override {
        float px = 0, py = 0, pw = 0, ph = 0;
        if (view->streamingHole(px, py, pw, ph)) {
            nvgBeginPath(vg);
            nvgRect(vg, x, y, width, height);
            nvgRoundedRect(vg, px, py, pw, ph, 6);
            nvgPathWinding(vg, NVG_HOLE);
            nvgFillColor(vg, Application::getTheme()["brls/background"]);
            nvgFill(vg);
        }
        AppletFrame::draw(vg, x, y, width, height, style, ctx);
    }

  private:
    CapacityTestView* view;
};
} // namespace

bool CapacityTestView::streamingHole(float& x, float& y, float& w,
                                     float& h) const {
    if (!streamingBackdrop)
        return false;
    previewRect(x, y, w, h);
    return w >= 32 && h >= 18;
}

void CapacityTestView::open(const Host& host) {
    auto* view = new CapacityTestView(host);
    auto* frame = new PreviewFrame(view);
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
                // Only while our screen is on top: with a dialog open,
                // dismiss() would pop the dialog instead and leave this
                // view stuck. Nothing streams while a dialog is shown.
                if (!focused && !gone() && isTopActivity() &&
                    phase != Phase::Ready && phase != Phase::Done &&
                    phase != Phase::Loading)
                    cancel();
            });

    loadHost();
}

CapacityTestView::~CapacityTestView() {
    *alive = false;
    speedCancel->store(true);
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

    // Body: test log on the left, stream preview (then the recommended
    // settings) on the right.
    auto* body = new Box(Axis::ROW);
    body->setGrow(1);
    body->setMarginTop(14);
    addView(body);

    auto* log = new Box(Axis::COLUMN);
    log->setWidth(kLogColumnWidth);
    log->setShrink(0);
    body->addView(log);

    statusLabel = makeLabel(22, Application::getTheme()["brls/accent"]);
    statusLabel->setText(" ");
    log->addView(statusLabel);

    table = new Box(Axis::COLUMN);
    table->setMarginTop(6);
    log->addView(table);

    verdictLabel = makeLabel(20, text, false);
    verdictLabel->setMarginTop(10);
    verdictLabel->setVisibility(Visibility::GONE);
    log->addView(verdictLabel);

    warningLabel = makeLabel(18, nvgRGB(255, 167, 38), false);
    warningLabel->setMarginTop(4);
    warningLabel->setVisibility(Visibility::GONE);
    log->addView(warningLabel);

    preview = new Box(Axis::COLUMN);
    preview->setGrow(1);
    preview->setMarginLeft(24);
    preview->setAlignItems(AlignItems::STRETCH);
    body->addView(preview);

    recoBox = new Box(Axis::COLUMN);
    recoBox->setPadding(14, 18, 14, 18);
    recoBox->setCornerRadius(8);
    recoBox->setBackgroundColor(nvgRGBA(128, 128, 128, 40));
    recoBox->setVisibility(Visibility::GONE);
    auto* recoTitle = makeLabel(18, Application::getTheme()["brls/text_disabled"]);
    recoTitle->setText("capacity/reco_title"_i18n);
    recoBox->addView(recoTitle);
    recoRows = new Box(Axis::COLUMN);
    recoRows->setMarginTop(4);
    recoBox->addView(recoRows);
    preview->addView(recoBox);

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
    // Video is drawn under the UI layer, into the preview rectangle only.
    // While streaming, PreviewFrame paints the background around it.
    streamingBackdrop = streaming;
    if (frame) {
        if (streaming)
            frame->setBackground(ViewBackground::NONE);
        else
            frame->setBackgroundColor(
                Application::getTheme()["brls/background"]);
    }
}

void CapacityTestView::previewRect(float& x, float& y, float& w,
                                   float& h) const {
    const float boxX = preview->getX();
    const float boxY = preview->getY();
    const float boxW = preview->getWidth();
    const float boxH = preview->getHeight();
    // Largest 16:9 rectangle that fits, top-aligned with the log column.
    w = std::min(boxW, boxH * 16.0f / 9.0f);
    h = w * 9.0f / 16.0f;
    x = boxX + (boxW - w) / 2.0f;
    y = boxY;
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
        nativeW = Application::windowWidth;
        nativeH = Application::windowHeight;
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

    // The running app is only touched right before phase 3 (beginStages).
    beginProbes();
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
    // Pays the TLS handshake (5 s timeout instead of the 1 s of measured pings): never counted, and its
    // failure is ignored unless the host says the endpoint doesn't exist.
    auto token = alive;
    GameStreamClient::instance().ping(
        address,
        [this, token](const GSResult<float>& result) {
            if (!*token || closing)
                return;
            if (!result.isSuccess() && result.error() == "not_supported") {
                markProbesUnsupported();
                afterSpeed();
                return;
            }
            pingNext();
        },
        true);
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
        },
        speedCancel);
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
    const int minKbps = min_recommended_bitrate_kbps(
        requested.width, requested.height, requested.fps, requested.codec);
    plan = plan_stage_bitrates(minKbps, stage_ceiling_kbps(speedMbps, usedFallbackApp));
    stageIndex = 0;
    results.clear();
    if (plan.empty()) {
        // Speed too low for any stage (only possible with the fallback app).
        tooSlow = true;
        finish();
        return;
    }

    // Phases 1-2 took a while: refresh which app is running before asking.
    phase = Phase::Preparing;
    statusLabel->setText("capacity/loading"_i18n);
    auto token = alive;
    GameStreamClient::instance().connect(
        host, [this, token](const GSResult<SERVER_DATA>& result) {
            if (!*token || closing)
                return;
            if (!result.isSuccess()) {
                skipStages(result.error());
                return;
            }
            address = GameStreamClient::instance().active_address(host);
            runningAppId = result.value().currentGame;

            if (!needs_confirmation(runningAppId, testApp.app_id)) {
                // Already running the test app: resume it and leave it
                // running after.
                launchedByTest = runningAppId != testApp.app_id;
                startStages();
                return;
            }

            std::string running = std::to_string(runningAppId);
            for (const auto& app : apps)
                if (app.app_id == runningAppId)
                    running = app.name;

            // Not cancelable: the choice must be explicit (B would leave the
            // test waiting between phases).
            auto* dialog =
                new Dialog(brls::getStr("capacity/confirm", running));
            dialog->setCancelable(false);
            dialog->addButton("capacity/cancel"_i18n, [this, token] {
                if (*token && !closing)
                    skipStages();
            });
            dialog->addButton("capacity/continue"_i18n, [this, token] {
                if (*token && !closing)
                    quitRunningAppThenStages();
            });
            dialog->open();
        });
}

void CapacityTestView::quitRunningAppThenStages() {
    auto token = alive;
    GameStreamClient::instance().quit(
        address, [this, token](const GSResult<bool>& result) {
            if (!*token || closing)
                return;
            if (!result.isSuccess()) {
                skipStages(result.error());
                return;
            }
            // The cached server data still lists the old app; refresh it so
            // the first stage launches instead of resuming.
            GameStreamClient::instance().connect(
                host, [this, token](const GSResult<SERVER_DATA>& result) {
                    if (!*token || closing)
                        return;
                    if (!result.isSuccess()) {
                        skipStages(result.error());
                        return;
                    }
                    address =
                        GameStreamClient::instance().active_address(host);
                    runningAppId = result.value().currentGame;
                    launchedByTest = runningAppId != testApp.app_id;
                    startStages();
                });
        });
}

void CapacityTestView::startStages() {
    if (closing)
        return;
    phase = Phase::Stages;
    startStage();
}

void CapacityTestView::skipStages(const std::string& error) {
    // Show phases 1-2 only; nothing launched, nothing written.
    stagesSkipped = true;
    finish();
    if (!error.empty())
        showError(error);
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
    // Called back on the main thread; the token covers a closed view.
    session->start(
        [this, token](const GSResult<bool>& result) {
            if (*token)
                onStageStarted(result.isSuccess(),
                               result.isSuccess() ? "" : result.error());
        },
        isSunshine);
}

void CapacityTestView::onStageStarted(bool ok, const std::string& error) {
    sessionStarting = false;
    if (phase == Phase::Cancelling) {
        // Cancel waited for the start to finish before tearing down.
        closeSession(launchedByTest);
        close();
        return;
    }
    if (!ok) {
        // A launch the host refused is not a dropped connection: no data was
        // measured, so it must not count against the network.
        if (results.empty()) {
            closeSession(launchedByTest);
            skipStages(error.empty() ? "capacity/start_failed"_i18n : error);
            return;
        }
        if (!stageRetried) {
            // The host may still be tearing down the previous session.
            stageRetried = true;
            retryPending = true;
            closeSession(false);
            auto token = alive;
            const StageRetryTicket ticket{retryGeneration,
                                          (unsigned)stageIndex};
            retryDelayHandle = brls::delay(750, [this, token, ticket] {
                if (!*token)
                    return;
                if (!should_run_stage_retry(
                        ticket, {retryGeneration, (unsigned)stageIndex}, true,
                        phase == Phase::Cancelling, closing))
                    return;
                retryPending = false;
                startStage();
            });
            return;
        }
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
    float px = 0, py = 0, pw = 0, ph = 0;
    if (streamingBackdrop)
        previewRect(px, py, pw, ph);
    const bool showVideo = pw >= 32 && ph >= 18;

    if (session && sessionStreaming && !stageEnding) {
        // The renderer works in window pixels, the layout in UI units.
        const float scale = Application::windowScale;
        session->set_video_rect((int)(px * scale), (int)(py * scale),
                                showVideo ? (int)(pw * scale) : 0,
                                showVideo ? (int)(ph * scale) : 0);
        // Also refreshes session_stats() (every 250 ms).
        if (showVideo)
            session->draw(vg, (int)(pw * scale), (int)(ph * scale));
        else
            session->draw(vg, (int)Application::contentWidth,
                          (int)Application::contentHeight);
        tickStage();
    }
    Box::draw(vg, x, y, width, height, style, ctx);

    if (streamingBackdrop && showVideo) {
        // Frame around the preview (NanoVG draws after the video).
        nvgBeginPath(vg);
        nvgRoundedRect(vg, px - 1, py - 1, pw + 2, ph + 2, 6);
        nvgStrokeColor(vg, Application::getTheme()["brls/text_disabled"]);
        nvgStrokeWidth(vg, 2);
        nvgStroke(vg);
    }
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
        stageRetried = false;
        retryGeneration++;
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
    if (stagesSkipped) {
        bitrateValue->setText("capacity/skipped"_i18n);
        text = "capacity/skipped_detail"_i18n;
    } else if (tooSlow && results.empty()) {
        text = "capacity/too_slow"_i18n;
    } else if (verdict->recommended_kbps > 0) {
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

    if (verdict->optimistic && !stagesSkipped && !results.empty()) {
        warningLabel->setText("capacity/optimistic"_i18n);
        warningLabel->setVisibility(Visibility::VISIBLE);
    }

    closeButton->setText("capacity/close"_i18n);
    // A test that already ran at a lower resolution recommends that one.
    const auto rec = recommend_settings(
        *verdict, downgradedFrom ? std::optional<int>(requested.height)
                                 : std::nullopt);
    if (rec.bitrate_kbps > 0 && !stagesSkipped) {
        recommendation = rec;
        showRecommendation();
        applyButton->setText("capacity/apply_all"_i18n);
        applyButton->setVisibility(Visibility::VISIBLE);
        Application::giveFocus(applyButton);
    } else {
        recommendation.reset();
        Application::giveFocus(closeButton);
    }
}

std::string CapacityTestView::resolutionName(int height) const {
    if (height == -1)
        return "settings/resolution_native"_i18n;
    return std::to_string(height) + "p";
}

std::string CapacityTestView::pacingName(int framesQueue) {
    switch (std::clamp(framesQueue, 1, 3)) {
    case 1:
        return "settings/frame_pacing_latency"_i18n;
    case 2:
        return "settings/frame_pacing_balanced"_i18n;
    default:
        return "settings/frame_pacing_smooth"_i18n;
    }
}

void CapacityTestView::showRecommendation() {
    recoRows->clearViews();
    if (!recommendation)
        return;
    const auto& rec = *recommendation;
    auto& settings = Settings::instance();
    const bool handheld =
        apply_target(settings.mode_profiles(), handheldWhenTested) ==
        ProfileTarget::Handheld;
    const NVGcolor dim = Application::getTheme()["brls/text_disabled"];
    const NVGcolor text = Application::getTheme()["brls/text"];
    const NVGcolor accent = Application::getTheme()["brls/accent"];

    // "current -> recommended", or "current (already right)".
    auto addRow = [&](const std::string& name, const std::string& current,
                      const std::string& next) {
        auto* row = new Box(Axis::ROW);
        row->setMarginTop(6);
        auto* nameLabel = makeLabel(18, dim);
        nameLabel->setWidth(220);
        nameLabel->setText(name);
        row->addView(nameLabel);
        const bool same = current == next;
        auto* value = makeLabel(18, same ? text : accent);
        value->setText(same ? brls::getStr("capacity/reco_keep", current)
                            : brls::getStr("capacity/reco_change", current,
                                           next));
        row->addView(value);
        recoRows->addView(row);
    };

    const int currentKbps =
        handheld ? settings.handheld_bitrate() : settings.bitrate();
    addRow("capacity/reco_bitrate"_i18n,
           brls::getStr("capacity/mbps", mbps(currentKbps)),
           brls::getStr("capacity/mbps", mbps(rec.bitrate_kbps)));

    const int currentHeight =
        handheld ? settings.handheld_resolution() : settings.resolution();
    addRow("capacity/reco_resolution"_i18n, resolutionName(currentHeight),
           resolutionName(rec.resolution.value_or(currentHeight)));

    const int currentPacing = std::clamp(settings.frames_queue_size(), 1, 3);
    addRow("capacity/reco_pacing"_i18n, pacingName(currentPacing),
           pacingName(rec.frame_pacing.value_or(currentPacing)));

    recoBox->setVisibility(Visibility::VISIBLE);
}

void CapacityTestView::applyRecommendation() {
    if (phase != Phase::Done || !recommendation ||
        recommendation->bitrate_kbps <= 0)
        return;
    auto& settings = Settings::instance();
    const auto& rec = *recommendation;
    // Bitrate and resolution: only the profile the test ran with.
    if (apply_target(settings.mode_profiles(), handheldWhenTested) ==
        ProfileTarget::Handheld) {
        settings.set_handheld_bitrate(rec.bitrate_kbps);
        if (rec.resolution)
            settings.set_handheld_resolution(*rec.resolution);
    } else {
        settings.set_bitrate(rec.bitrate_kbps);
        if (rec.resolution)
            settings.set_resolution(*rec.resolution);
    }
    // Frame pacing is a single setting for both profiles.
    if (rec.frame_pacing)
        settings.set_frames_queue_size(*rec.frame_pacing);
    settings.save();
    Application::notify("capacity/applied_all"_i18n);
    close();
}

// MARK: Cancel / close

void CapacityTestView::cancel() {
    if (closing || phase == Phase::Cancelling)
        return;
    // Never touches settings. Stops the stream and closes the app only if
    // the test opened it.
    if (retryPending && !sessionStarting) {
        // Waiting out the retry delay: no stream exists, so none may start.
        brls::cancelDelay(retryDelayHandle);
        retryGeneration++;
        retryPending = false;
        phase = Phase::Cancelling;
        if (launchedByTest)
            GameStreamClient::instance().quit(address, [](auto) {});
        close();
        return;
    }
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

bool CapacityTestView::isTopActivity() {
    const auto& stack = Application::getActivitiesStack();
    return !stack.empty() && stack.back() == getParentActivity();
}

void CapacityTestView::close() {
    if (closing)
        return;
    if (!isTopActivity()) {
        // dismiss() pops the top activity: wait until a dialog over us is
        // gone instead of closing it in our place.
        auto token = alive;
        brls::delay(100, [this, token] {
            if (*token)
                close();
        });
        return;
    }
    closing = true;
    speedCancel->store(true);
    sessionStreaming = false;
    setStreamingBackdrop(false);
    this->dismiss();
}
