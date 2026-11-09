//
//  download_service.hpp
//  Equinox
//
//  The app-wide download queue: one worker thread, one download at a time. It
//  outlives every view and restores its tasks after a restart. All methods
//  are thread-safe; listeners and `stopped` callbacks run on the UI thread.
//

#pragma once

#include "emu/Console.hpp"
#include "emu/DownloadQueue.hpp"
#include "emu/GameDataJob.hpp"
#include "emu/RomScan.hpp"
#include "emu/ScreenScraper.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct DlContext {
    std::shared_ptr<std::atomic<bool>> stop;
    std::function<void(float)> progress;
    // Set by the runner when the result blames a host other than the task's first.
    std::string failedHost;
    std::optional<emu::DataOutcome> data;  // GameData only
};

struct DlEvent {
    std::string id;
    emu::DlState state;  // Done also stands for "removed" (Gone, Cancelled)
    float progress;
    emu::DlResult result;  // meaningful once the run ended; otherwise Done
    std::optional<emu::DataOutcome> data;
};

class DownloadService {
  public:
    static DownloadService& instance();

    void start();     // main, before the loop
    void shutdown();  // main, after the loop: stops, saves and joins

    using Runner = std::function<emu::DlResult(const emu::DlTask&, DlContext&)>;
    void registerKind(emu::DlKind kind, Runner runner);

    bool submit(emu::DlTask task);
    // Takes the lock once and posts one event, with an empty id, however many
    // tasks it queued: for thousands of them on the UI thread.
    void submitAll(std::vector<emu::DlTask> tasks);
    void setPriority(const std::string& id, emu::DlPriority priority);
    void pause(const std::string& id);
    void resume(const std::string& id);
    void cancel(const std::string& id);
    void cancelGroup(const std::string& group, emu::DlKind kind);
    void retryFailed();
    void credentialsChanged();

    // Nested: each suspend needs one resumeAll. UI thread; `stopped` runs on the
    // UI thread once nothing is transferring.
    void suspend(std::function<void()> stopped);
    void resumeAll();

    std::vector<emu::DlTask> snapshot() const;
    // A host of the task is blocked by a bad login or the quota.
    bool blocked(const emu::DlTask& task) const;

    int subscribe(std::function<void(const DlEvent&)> listener);
    void unsubscribe(int token);

  private:
    enum class StopReason { None, Requeue, Paused, Abandoned };
    using Clock = std::chrono::steady_clock;

    DownloadService() = default;

    void worker();
    void runTask(const emu::DlTask& task, std::unique_lock<std::mutex>& lock);
    void removeOrphans();
    void requestStop(StopReason reason);
    // True when it wrote. Unlocks while writing.
    bool saveQueue(std::unique_lock<std::mutex>& lock, bool force);
    void post(std::vector<DlEvent> events);
    void post(DlEvent event);
    std::optional<emu::DlState> stateOf(const std::string& id) const;
    emu::SsConfig ssConfig() const;
    emu::DlResult runGameData(const emu::DlTask& task, DlContext& context);
    emu::DlResult runCover(const emu::DlTask& task, DlContext& context);

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    bool started_ = false;
    bool quitting_ = false;
    int suspended_ = 0;
    std::vector<std::function<void()>> stoppedWaiters_;

    emu::DownloadQueue queue_;
    std::map<emu::DlKind, Runner> runners_;
    emu::SsConfig ss_;

    std::optional<std::string> runningId_;
    std::shared_ptr<std::atomic<bool>> runningStop_;
    StopReason stopReason_ = StopReason::None;

    bool dirty_ = false;
    Clock::time_point nextSave_{};

    int nextToken_ = 1;
    std::map<int, std::function<void(const DlEvent&)>> listeners_;
};

namespace emu {

std::string gameDataId(const std::string& slug, const std::string& fileName);
// UI thread: reads the ScreenScraper settings to fill the hosts.
DlTask gameDataTask(const Console& console, const RomEntry& rom, DlPriority priority);

std::string coverId(const std::string& slug, const std::string& title);
// `canonical` is the name the game's data found, or empty.
DlTask coverTask(const Console& console, const std::string& title, const std::string& canonical,
                 DlPriority priority);

}  // namespace emu
