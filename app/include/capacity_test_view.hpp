//
//  capacity_test_view.hpp
//  Moonlight
//
//  Connection capacity test: latency (/solstice/ping), download speed
//  (/solstice/speedtest) and stepped bitrate stages streamed from the host.
//

#pragma once

#include "GameStreamClient.hpp"
#include "MoonlightSession.hpp"
#include "StreamCapacity.hpp"
#include <Settings.hpp>
#include <borealis.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class CapacityTestView : public brls::Box {
  public:
    // Pushes the test screen for the host as a new activity.
    static void open(const Host& host);

    explicit CapacityTestView(const Host& host);
    ~CapacityTestView() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

  private:
    enum class Phase {
        Loading,
        Ready,
        Preparing,
        Latency,
        Speed,
        Stages,
        Done,
        Cancelling
    };

    struct SpeedProgress {
        std::mutex mutex;
        std::vector<std::pair<double, uint64_t>> samples;
    };

    Host host;
    std::string address;
    bool isSunshine = false;
    int runningAppId = 0;
    AppInfoList apps;
    AppInfo testApp{};
    bool usedFallbackApp = false;
    bool launchedByTest = false;
    bool handheldWhenTested = false;
    StageConfig requested{};
    // Configured height, set when the user accepted a lower test resolution.
    std::optional<int> downgradedFrom;
    Phase phase = Phase::Loading;
    bool closing = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    brls::AppletFrame* frame = nullptr;
    brls::Event<bool>::Subscription windowFocusSubscription;

    // Phase 1
    bool probesSupported = true;
    std::vector<std::optional<float>> pingSamples;
    std::optional<LatencyResult> latency;
    // Phase 2
    int speedRun = 0;
    std::optional<float> speedMbps;
    float bestRunMbps = 0;
    std::shared_ptr<SpeedProgress> speedProgress;
    // Phase 3
    std::vector<int> plan;
    size_t stageIndex = 0;
    StageConfig stageConfig{};
    std::vector<StageResult> results;
    MoonlightSession* session = nullptr;
    bool sessionStarting = false;
    bool sessionStreaming = false;
    bool stageEnding = false;
    std::vector<StageSample> samples;
    std::chrono::steady_clock::time_point stageStart, lastSample,
        lastFrameAdvance;
    uint32_t lastFrames = 0;
    std::optional<CapacityVerdict> verdict;
    // Phase 3 not run (close-app confirmation declined or host error).
    bool stagesSkipped = false;

    // Widgets
    brls::Label* introLabel = nullptr;
    brls::Label* planLabel = nullptr;
    brls::Label* latencyValue = nullptr;
    brls::Label* latencyDetail = nullptr;
    brls::Label* speedValue = nullptr;
    brls::Label* bitrateValue = nullptr;
    brls::Label* statusLabel = nullptr;
    brls::Box* table = nullptr;
    brls::Label* verdictLabel = nullptr;
    brls::Label* warningLabel = nullptr;
    brls::Button* startButton = nullptr;
    brls::Button* applyButton = nullptr;
    brls::Button* closeButton = nullptr;

    bool gone() const { return !*alive || closing; }
    void buildLayout();
    brls::Box* makeCard(const std::string& title, brls::Label** value,
                        brls::Label** detail);
    void setStreamingBackdrop(bool streaming);
    std::string dec(double value, int decimals = 1) const;
    std::string mbps(int kbps) const;

    void loadHost();
    void showReady();
    void computeRequestedConfig();
    void onStartPressed();
    void beginProbes();
    void markProbesUnsupported();
    void pingWarmup();
    void pingNext();
    void beginSpeed();
    void runSpeedtest();
    void updateSpeedProgress();
    void afterSpeed();
    void beginStages();
    void quitRunningAppThenStages();
    void startStages();
    void skipStages(const std::string& error = "");
    bool isTopActivity();
    void startStage();
    void onStageStarted(bool ok);
    void tickStage();
    void requestStageEnd(bool disconnected);
    void finishStage(bool disconnected);
    void closeSession(bool terminateApp);
    void finish();
    void refreshTable();
    void applyRecommendation();
    void cancel();
    void close();
};
