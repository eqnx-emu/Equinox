//
//  download_service.cpp
//  Equinox
//

#include "download_service.hpp"

#include "emu/DownloadStore.hpp"
#include "emu/GameDataHttp.hpp"
#include "emu/GameDb.hpp"
#include "emu/NetGate.hpp"
#include "emu/PathUtf8.hpp"
#include "emu/SsCredentials.hpp"

#include <borealis.hpp>
#include <curl/curl.h>
#include <jansson.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace {

constexpr const char* kQueueFile = "sdmc:/equinox/downloads.json";
constexpr auto kSaveEvery = std::chrono::seconds(2);
constexpr auto kProgressEvery = std::chrono::milliseconds(100);
// Backoff and quota expire without anyone waking the worker.
constexpr auto kIdlePoll = std::chrono::seconds(1);
constexpr auto kIdleForever = std::chrono::hours(1);
constexpr auto kGateRetry = std::chrono::milliseconds(200);
// Between two games, so a launch or a cover polling the gate can get it.
constexpr auto kGateYield = std::chrono::milliseconds(200);

int64_t wallClock() { return static_cast<int64_t>(std::time(nullptr)); }

std::string stringField(const json_t* object, const char* key) {
    const json_t* value = json_object_get(object, key);
    return json_is_string(value) ? json_string_value(value) : std::string();
}

// Worker thread only. Loaded once per console and kept for the whole run.
std::shared_ptr<const emu::GameDb> workerGameDb(const emu::Console& console) {
    static std::map<std::string, std::shared_ptr<const emu::GameDb>> loaded;
    std::shared_ptr<const emu::GameDb>& db = loaded[console.slug];
    if (!db) {
        auto fresh = std::make_shared<emu::GameDb>();
        if (!fresh->loadFile(emu::gameDbPath(console)))
            brls::Logger::warning("DownloadService: no game database for {}", console.slug);
        emu::ensureFolders(emu::pathsFor(console));
        db = fresh;
    }
    return db;
}

}  // namespace

namespace emu {

std::string gameDataId(const std::string& slug, const std::string& fileName) {
    return "gamedata:" + slug + ":" + fileName;
}

DlTask gameDataTask(const Console& console, const RomEntry& rom, DlPriority priority) {
    DlTask task;
    task.id = gameDataId(console.slug, rom.fileName);
    task.kind = DlKind::GameData;
    task.priority = priority;
    task.skipWait = priority == DlPriority::Interactive;
    task.group = console.slug;
    task.hosts = {kHostLibretro};
    if (ssConfigured(appSsConfig())) task.hosts.push_back(kHostScreenScraper);

    json_t* params = json_object();
    json_object_set_new(params, "console", json_string(console.slug.c_str()));
    json_object_set_new(params, "path", json_string(rom.path.c_str()));
    json_object_set_new(params, "fileName", json_string(rom.fileName.c_str()));
    json_object_set_new(params, "title", json_string(rom.title.c_str()));
    if (char* text = json_dumps(params, JSON_COMPACT)) {
        task.params = text;
        free(text);
    }
    json_decref(params);
    return task;
}

}  // namespace emu

DownloadService& DownloadService::instance() {
    static DownloadService service;
    return service;
}

void DownloadService::start() {
    curl_global_init(CURL_GLOBAL_ALL);

    std::error_code ec;
    const fs::path file = emu::fromUtf8(kQueueFile);
    const bool exists =
        fs::exists(file, ec) || fs::exists(emu::fromUtf8(std::string(kQueueFile) + ".old"), ec);

    std::lock_guard<std::mutex> lock(mutex_);
    if (started_) return;
    if (!emu::loadDownloads(kQueueFile, queue_) && exists)
        brls::Logger::warning("DownloadService: {} is corrupt, starting empty", kQueueFile);
    ss_ = emu::appSsConfig();
    runners_[emu::DlKind::GameData] = [this](const emu::DlTask& task, DlContext& context) {
        return runGameData(task, context);
    };
    started_ = true;
    thread_ = std::thread([this] { worker(); });
}

void DownloadService::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_) return;
        started_ = false;
        quitting_ = true;
        requestStop(StopReason::Requeue);
    }
    wake_.notify_all();
    thread_.join();

    std::unique_lock<std::mutex> lock(mutex_);
    dirty_ = true;
    saveQueue(lock, true);
    listeners_.clear();
}

void DownloadService::registerKind(emu::DlKind kind, Runner runner) {
    std::lock_guard<std::mutex> lock(mutex_);
    runners_[kind] = std::move(runner);
}

bool DownloadService::submit(emu::DlTask task) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string id = task.id;
    const bool created = queue_.submit(std::move(task));
    dirty_ = true;
    if (created) post(DlEvent{id, emu::DlState::Queued, 0.0f, emu::DlResult::Done, std::nullopt});
    wake_.notify_all();
    return created;
}

void DownloadService::setPriority(const std::string& id, emu::DlPriority priority) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.setPriority(id, priority);
    dirty_ = true;
    wake_.notify_all();
}

void DownloadService::pause(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.pause(id);
    if (stateOf(id) != emu::DlState::Paused) return;
    dirty_ = true;
    if (runningId_ == id) requestStop(StopReason::Paused);
    post(DlEvent{id, emu::DlState::Paused, 0.0f, emu::DlResult::Done, std::nullopt});
    wake_.notify_all();
}

void DownloadService::resume(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stateOf(id) != emu::DlState::Paused) return;
    queue_.resume(id);
    dirty_ = true;
    post(DlEvent{id, emu::DlState::Queued, 0.0f, emu::DlResult::Done, std::nullopt});
    wake_.notify_all();
}

void DownloadService::cancel(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stateOf(id)) return;
    if (runningId_ == id) requestStop(StopReason::Abandoned);
    queue_.cancel(id);
    dirty_ = true;
    post(DlEvent{id, emu::DlState::Done, 0.0f, emu::DlResult::Cancelled, std::nullopt});
    wake_.notify_all();
}

void DownloadService::cancelGroup(const std::string& group, emu::DlKind kind) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DlEvent> events;
    for (const emu::DlTask& task : queue_.tasks()) {
        if (task.group != group || task.kind != kind) continue;
        if (runningId_ == task.id) requestStop(StopReason::Abandoned);
        events.push_back({task.id, emu::DlState::Done, 0.0f, emu::DlResult::Cancelled, std::nullopt});
    }
    if (events.empty()) return;
    queue_.cancelGroup(group, kind);
    dirty_ = true;
    post(std::move(events));
    wake_.notify_all();
}

void DownloadService::retryFailed() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.retryFailed();
    dirty_ = true;
    wake_.notify_all();
}

void DownloadService::credentialsChanged() {
    emu::SsConfig fresh = emu::appSsConfig();
    std::lock_guard<std::mutex> lock(mutex_);
    ss_ = std::move(fresh);
    queue_.clearLoginBlock();
    dirty_ = true;
    wake_.notify_all();
}

void DownloadService::suspend(std::function<void()> stopped) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool alreadyStopped = !started_ || (suspended_ > 0 && stoppedWaiters_.empty());
        suspended_++;
        if (!alreadyStopped) {
            stoppedWaiters_.push_back(std::move(stopped));
            if (runningId_) requestStop(StopReason::Requeue);
            wake_.notify_all();
            return;
        }
    }
    if (stopped) stopped();
}

void DownloadService::resumeAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (suspended_ > 0) suspended_--;
    wake_.notify_all();
}

std::vector<emu::DlTask> DownloadService::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.tasks();
}

bool DownloadService::blocked(const emu::DlTask& task) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = wallClock();
    for (const std::string& host : task.hosts) {
        const emu::DlHostBlocks blocks = queue_.blocks(host);
        if (blocks.badLogin || blocks.quotaUntil > now) return true;
    }
    return false;
}

int DownloadService::subscribe(std::function<void(const DlEvent&)> listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int token = nextToken_++;
    listeners_[token] = std::move(listener);
    return token;
}

void DownloadService::unsubscribe(int token) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.erase(token);
}

// Runs on the UI thread, one event after another. Each listener is looked up
// right before its call, so one that unsubscribed meanwhile is skipped.
void DownloadService::post(std::vector<DlEvent> events) {
    brls::sync([this, events = std::move(events)] {
        for (const DlEvent& event : events) {
            std::vector<int> tokens;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& entry : listeners_) tokens.push_back(entry.first);
            }
            for (int token : tokens) {
                std::function<void(const DlEvent&)> listener;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = listeners_.find(token);
                    if (it == listeners_.end()) continue;
                    listener = it->second;
                }
                listener(event);
            }
        }
    });
}

void DownloadService::post(DlEvent event) {
    std::vector<DlEvent> events;
    events.push_back(std::move(event));
    post(std::move(events));
}

std::optional<emu::DlState> DownloadService::stateOf(const std::string& id) const {
    for (const emu::DlTask& task : queue_.tasks())
        if (task.id == id) return task.state;
    return std::nullopt;
}

emu::SsConfig DownloadService::ssConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ss_;
}

void DownloadService::requestStop(StopReason reason) {
    if (!runningId_) return;
    // A cancelled task is gone: nothing may bring it back.
    if (stopReason_ != StopReason::Abandoned) stopReason_ = reason;
    if (runningStop_) runningStop_->store(true);
}

bool DownloadService::saveQueue(std::unique_lock<std::mutex>& lock, bool force) {
    const Clock::time_point now = Clock::now();
    if (!dirty_ || (!force && now < nextSave_)) return false;
    dirty_ = false;
    nextSave_ = now + kSaveEvery;
    const emu::DownloadQueue copy = queue_;
    lock.unlock();
    if (!emu::saveDownloads(kQueueFile, copy))
        brls::Logger::warning("DownloadService: could not save {}", kQueueFile);
    lock.lock();
    return true;
}

// A .part no task owns was left by a crash. Only the folders game data and
// covers write to: the ROM folders can hold thousands of files.
void DownloadService::removeOrphans() {
    std::vector<std::string> found;
    std::error_code ec;
    for (const emu::Console& console : emu::consoles()) {
        const emu::ConsolePaths paths = emu::pathsFor(console);
        for (const std::string& dir : {paths.snaps, paths.titles, paths.covers}) {
            fs::directory_iterator it(emu::fromUtf8(dir), ec);
            for (const fs::directory_iterator end; !ec && it != end; it.increment(ec)) {
                const std::string path = emu::toUtf8(it->path());
                if (path.size() > 5 && path.compare(path.size() - 5, 5, ".part") == 0)
                    found.push_back(path);
            }
            ec.clear();
        }
    }

    std::vector<std::string> orphans;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        orphans = emu::orphanPartials(found, queue_);
    }
    for (const std::string& path : orphans) fs::remove(emu::fromUtf8(path), ec);
    if (!orphans.empty())
        brls::Logger::info("DownloadService: removed {} unfinished files", orphans.size());
}

void DownloadService::worker() {
    std::error_code ec;
    fs::create_directories(emu::fromUtf8(kQueueFile).parent_path(), ec);
    removeOrphans();

    std::unique_lock<std::mutex> lock(mutex_);
    while (!quitting_) {
        if (!stoppedWaiters_.empty()) {
            saveQueue(lock, true);
            if (quitting_) break;
            brls::sync([waiters = std::move(stoppedWaiters_)] {
                for (const auto& stopped : waiters)
                    if (stopped) stopped();
            });
            stoppedWaiters_.clear();
            continue;
        }
        if (saveQueue(lock, false)) continue;

        emu::DlTask* picked = suspended_ == 0 ? queue_.next(wallClock()) : nullptr;
        if (picked) {
            const emu::DlTask task = *picked;
            runTask(task, lock);
            continue;
        }

        bool waiting = false;
        if (suspended_ == 0)
            for (const emu::DlTask& task : queue_.tasks())
                if (task.state == emu::DlState::Queued) waiting = true;
        Clock::time_point deadline =
            Clock::now() + (waiting ? Clock::duration(kIdlePoll) : Clock::duration(kIdleForever));
        if (dirty_ && nextSave_ < deadline) deadline = nextSave_;
        wake_.wait_until(lock, deadline);
    }
}

// Entered and left with the lock held; unlocks around the transfer.
void DownloadService::runTask(const emu::DlTask& task, std::unique_lock<std::mutex>& lock) {
    const std::string id = task.id;
    auto stop = std::make_shared<std::atomic<bool>>(false);
    runningId_ = id;
    runningStop_ = stop;
    stopReason_ = StopReason::None;
    Runner runner;
    auto found = runners_.find(task.kind);
    if (found != runners_.end()) runner = found->second;

    const bool usesGate = task.kind == emu::DlKind::GameData;
    lock.unlock();
    const bool gateHeld = usesGate && emu::netTryAcquire();
    lock.lock();

    if (usesGate && !gateHeld) {
        // Covers or a launch have the network. Whatever happened to the task
        // meanwhile (paused, cancelled) requeueRunning leaves it as it is.
        queue_.requeueRunning();
        runningId_.reset();
        runningStop_.reset();
        wake_.wait_for(lock, kGateRetry, [this] { return quitting_ || !stoppedWaiters_.empty(); });
        return;
    }

    post(DlEvent{id, emu::DlState::Running, 0.0f, emu::DlResult::Done, std::nullopt});
    lock.unlock();

    DlContext context;
    context.stop = stop;
    Clock::time_point nextProgress = Clock::now() + kProgressEvery;
    context.progress = [this, &id, &stop, &nextProgress](float progress) {
        const Clock::time_point now = Clock::now();
        if (now < nextProgress || stop->load()) return;
        nextProgress = now + kProgressEvery;
        std::unique_lock<std::mutex> progressLock(mutex_);
        post(DlEvent{id, emu::DlState::Running, progress, emu::DlResult::Done, std::nullopt});
        saveQueue(progressLock, false);
    };

    emu::DlResult result = emu::DlResult::Invalid;
    if (!stop->load()) {
        if (runner) {
            result = runner(task, context);
        } else {
            brls::Logger::error("DownloadService: no runner for {}", id);
        }
    } else {
        result = emu::DlResult::Cancelled;
    }
    if (gateHeld) emu::netRelease();

    lock.lock();
    const StopReason reason = stopReason_;
    runningId_.reset();
    runningStop_.reset();
    stopReason_ = StopReason::None;
    dirty_ = true;

    DlEvent event{id, emu::DlState::Done, 1.0f, result, std::move(context.data)};
    if (reason == StopReason::Abandoned) {
        // cancel() already told everyone, and the id may have been submitted again.
    } else if (result != emu::DlResult::Done && reason == StopReason::Requeue) {
        queue_.requeueRunning();
        event.state = emu::DlState::Queued;
        event.progress = 0.0f;
        event.result = emu::DlResult::Done;
        event.data.reset();
        post(std::move(event));
    } else if (result != emu::DlResult::Done && reason == StopReason::Paused) {
        // pause() already told everyone.
    } else {
        std::string host = context.failedHost;
        if (host.empty() && !task.hosts.empty()) host = task.hosts.front();
        queue_.finish(id, result, host, wallClock());
        const std::optional<emu::DlState> state = stateOf(id);
        event.state = state.value_or(emu::DlState::Done);
        event.progress = event.state == emu::DlState::Done ? 1.0f : 0.0f;
        post(std::move(event));
        if (state == emu::DlState::Done) queue_.cancel(id);
    }

    if (usesGate)
        wake_.wait_for(lock, kGateYield, [this] { return quitting_ || !stoppedWaiters_.empty(); });
}

emu::DlResult DownloadService::runGameData(const emu::DlTask& task, DlContext& context) {
    json_error_t err;
    json_t* params = json_loads(task.params.c_str(), 0, &err);
    if (!params) return emu::DlResult::Invalid;
    const std::string slug = stringField(params, "console");
    emu::RomEntry rom;
    rom.path = stringField(params, "path");
    rom.fileName = stringField(params, "fileName");
    rom.title = stringField(params, "title");
    json_decref(params);

    const emu::Console* console = emu::findConsole(slug);
    if (!console || !emu::romFileUsable(rom.path)) return emu::DlResult::Gone;

    const std::shared_ptr<const emu::GameDb> db = workerGameDb(*console);
    const emu::GameDataRequest request{console, emu::pathsFor(*console), rom, db.get(), ssConfig()};
    emu::HttpDataTransport transport(context.stop);
    emu::DataOutcome outcome =
        emu::fetchGameData(request, transport, [&context](emu::DataStep step) {
            if (context.stop->load()) return false;
            context.progress(emu::stepProgress(step));
            return true;
        });

    context.data = outcome;
    switch (outcome.stop) {
        case emu::DataStop::None: return emu::DlResult::Done;
        case emu::DataStop::Offline: return emu::DlResult::Transient;
        case emu::DataStop::BadLogin:
            context.failedHost = emu::kHostScreenScraper;
            return emu::DlResult::BadLogin;
        case emu::DataStop::Quota:
            context.failedHost = emu::kHostScreenScraper;
            return emu::DlResult::Quota;
        case emu::DataStop::Disk: return emu::DlResult::Disk;
        case emu::DataStop::Cancelled: return emu::DlResult::Cancelled;
    }
    return emu::DlResult::Invalid;
}
