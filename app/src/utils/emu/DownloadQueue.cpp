#include "emu/DownloadQueue.hpp"

#include <algorithm>
#include <unordered_map>

namespace emu {

namespace {

constexpr int64_t kQuotaSeconds = 86400;
constexpr int64_t kBackoffSeconds[] = {5, 30, 120};
constexpr int kMaxFailures = 4;

}  // namespace

bool retryable(long httpStatus) {
    return httpStatus < 400 || httpStatus == 429 || httpStatus >= 500;
}

DlTask* DownloadQueue::find(const std::string& id) {
    for (DlTask& task : tasks_)
        if (task.id == id) return &task;
    return nullptr;
}

bool DownloadQueue::hasRunning() const {
    return std::any_of(tasks_.begin(), tasks_.end(),
                       [](const DlTask& t) { return t.state == DlState::Running; });
}

bool DownloadQueue::submit(DlTask task) {
    if (DlTask* existing = find(task.id)) {
        if (task.priority < existing->priority) existing->priority = task.priority;
        if (task.skipWait && task.priority == DlPriority::Interactive) existing->skipWait = true;
        return false;
    }
    if (task.state != DlState::Paused && task.state != DlState::Failed) task.state = DlState::Queued;
    tasks_.push_back(std::move(task));
    return true;
}

size_t DownloadQueue::submitAll(std::vector<DlTask> tasks) {
    std::unordered_map<std::string, size_t> known;
    known.reserve(tasks_.size() + tasks.size());
    for (size_t i = 0; i < tasks_.size(); i++) known[tasks_[i].id] = i;

    size_t changed = 0;
    for (DlTask& task : tasks) {
        auto found = known.find(task.id);
        if (found == known.end()) {
            known[task.id] = tasks_.size();
            task.state = DlState::Queued;
            tasks_.push_back(std::move(task));
            changed++;
            continue;
        }
        DlTask& existing = tasks_[found->second];
        if (existing.state == DlState::Running) continue;
        existing.priority = task.priority;
        existing.params = std::move(task.params);
        if (existing.state == DlState::Failed) {
            existing.state = DlState::Queued;
            existing.failures = 0;
            changed++;
        } else if (existing.state == DlState::Paused && existing.diskPaused) {
            existing.state = DlState::Queued;
            existing.diskPaused = false;
            changed++;
        }
    }
    return changed;
}

void DownloadQueue::setPriority(const std::string& id, DlPriority priority) {
    if (DlTask* task = find(id)) task->priority = priority;
}

bool DownloadQueue::runnable(const DlTask& task, int64_t now, bool& usesSkipWait) const {
    usesSkipWait = false;
    if (task.state != DlState::Queued) return false;
    bool waiting = false;
    for (const std::string& host : task.hosts) {
        auto block = blocks_.find(host);
        if (block != blocks_.end()) {
            if (block->second.badLogin) return false;
            if (block->second.quotaUntil > now) return false;
        }
        auto wait = backoff_.find(host);
        if (wait != backoff_.end() && wait->second.until > now) waiting = true;
    }
    if (waiting) {
        if (!(task.skipWait && task.priority == DlPriority::Interactive)) return false;
        usesSkipWait = true;
    }
    return true;
}

DlTask* DownloadQueue::next(int64_t now) {
    if (hasRunning()) return nullptr;
    // A clock moved backwards must not lock a host for more than one quota window.
    for (auto& entry : blocks_)
        entry.second.quotaUntil = std::min(entry.second.quotaUntil, now + kQuotaSeconds);
    DlTask* best = nullptr;
    bool bestSkips = false;
    for (DlTask& task : tasks_) {
        bool skips = false;
        if (!runnable(task, now, skips)) continue;
        if (!best || task.priority < best->priority) {
            best = &task;
            bestSkips = skips;
        }
    }
    if (!best) return nullptr;
    best->state = DlState::Running;
    if (bestSkips) best->skipWait = false;
    return best;
}

bool DownloadQueue::shouldPreempt(int64_t now) const {
    const DlTask* running = nullptr;
    for (const DlTask& task : tasks_)
        if (task.state == DlState::Running) running = &task;
    if (!running || !running->resumable) return false;
    for (const DlTask& task : tasks_) {
        bool skips = false;
        if (task.priority < running->priority && runnable(task, now, skips)) return true;
    }
    return false;
}

std::vector<std::string> DownloadQueue::finish(const std::string& id, DlResult result,
                                               const std::string& host, int64_t now) {
    DlTask* task = find(id);
    if (!task) return {};
    switch (result) {
        case DlResult::Done:
            task->state = DlState::Done;
            task->failures = 0;
            backoff_.erase(host);
            finished_[{task->group, task->kind}]++;
            break;
        case DlResult::Gone:
            // Nothing to fetch: handled all the same.
            finished_[{task->group, task->kind}]++;
            cancel(id);
            break;
        case DlResult::Cancelled:
            cancel(id);
            break;
        case DlResult::Transient: {
            Backoff& wait = backoff_[host];
            wait.until = now + kBackoffSeconds[std::min(wait.failures, 2)];
            ++wait.failures;
            task->state = ++task->failures >= kMaxFailures ? DlState::Failed : DlState::Queued;
            break;
        }
        case DlResult::BadLogin:
            blocks_[host].badLogin = true;
            task->state = DlState::Queued;
            break;
        case DlResult::Quota:
            blocks_[host].quotaUntil = now + kQuotaSeconds;
            task->state = DlState::Queued;
            break;
        case DlResult::Disk:
            task->state = DlState::Paused;
            task->diskPaused = true;
            return pauseAll();
        case DlResult::Invalid:
            task->state = DlState::Failed;
            break;
    }
    return {};
}

void DownloadQueue::requeueRunning() {
    for (DlTask& task : tasks_)
        if (task.state == DlState::Running) task.state = DlState::Queued;
}

void DownloadQueue::pause(const std::string& id) {
    DlTask* task = find(id);
    if (task && (task->state == DlState::Queued || task->state == DlState::Running)) {
        task->state = DlState::Paused;
        task->diskPaused = false;
    }
}

std::vector<std::string> DownloadQueue::pauseAll() {
    std::vector<std::string> changed;
    for (DlTask& task : tasks_) {
        if (task.state != DlState::Queued && task.state != DlState::Running) continue;
        task.state = DlState::Paused;
        task.diskPaused = true;
        changed.push_back(task.id);
    }
    return changed;
}

void DownloadQueue::resume(const std::string& id) {
    DlTask* task = find(id);
    if (task && task->state == DlState::Paused) {
        task->state = DlState::Queued;
        task->diskPaused = false;
    }
}

void DownloadQueue::cancel(const std::string& id) {
    const DlTask* task = find(id);
    if (!task) return;
    const GroupKey key{task->group, task->kind};
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(),
                                [&](const DlTask& t) { return t.id == id; }),
                 tasks_.end());
    dropEmptyGroup(key);
}

void DownloadQueue::cancelGroup(const std::string& group, DlKind kind) {
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(),
                                [&](const DlTask& t) { return t.group == group && t.kind == kind; }),
                 tasks_.end());
    finished_.erase({group, kind});
}

void DownloadQueue::dropEmptyGroup(const GroupKey& key) {
    const bool left = std::any_of(tasks_.begin(), tasks_.end(), [&](const DlTask& t) {
        return t.group == key.first && t.kind == key.second;
    });
    if (!left) finished_.erase(key);
}

std::vector<std::string> DownloadQueue::pauseGroup(const std::string& group, DlKind kind) {
    std::vector<std::string> changed;
    for (DlTask& task : tasks_) {
        if (task.group != group || task.kind != kind) continue;
        if (task.state != DlState::Queued && task.state != DlState::Running) continue;
        task.state = DlState::Paused;
        task.diskPaused = false;
        changed.push_back(task.id);
    }
    return changed;
}

std::vector<std::string> DownloadQueue::resumeGroup(const std::string& group, DlKind kind) {
    std::vector<std::string> changed;
    for (DlTask& task : tasks_) {
        if (task.group != group || task.kind != kind || task.state != DlState::Paused) continue;
        task.state = DlState::Queued;
        task.diskPaused = false;
        changed.push_back(task.id);
    }
    return changed;
}

std::vector<std::string> DownloadQueue::retryGroup(const std::string& group, DlKind kind) {
    std::vector<std::string> changed;
    for (DlTask& task : tasks_) {
        if (task.group != group || task.kind != kind || task.state != DlState::Failed) continue;
        task.state = DlState::Queued;
        task.failures = 0;
        changed.push_back(task.id);
    }
    return changed;
}

std::vector<std::string> DownloadQueue::resumePaused() {
    std::vector<std::string> changed;
    for (DlTask& task : tasks_) {
        if (task.state != DlState::Paused) continue;
        task.state = DlState::Queued;
        task.diskPaused = false;
        changed.push_back(task.id);
    }
    return changed;
}

void DownloadQueue::retryFailed() {
    for (DlTask& task : tasks_) {
        if (task.state != DlState::Failed) continue;
        task.state = DlState::Queued;
        task.failures = 0;
    }
    blocks_.clear();
    backoff_.clear();
}

std::vector<std::string> DownloadQueue::retryHost(const std::string& host, int64_t now) {
    std::vector<std::string> changed;
    for (DlTask& task : tasks_) {
        if (std::find(task.hosts.begin(), task.hosts.end(), host) == task.hosts.end()) continue;
        if (task.state == DlState::Failed) {
            task.state = DlState::Queued;
            task.failures = 0;
            changed.push_back(task.id);
        } else if (task.state == DlState::Queued && held(task, now)) {
            changed.push_back(task.id);
        }
    }
    blocks_.erase(host);
    backoff_.erase(host);
    return changed;
}

bool DownloadQueue::retry(const std::string& id) {
    DlTask* task = find(id);
    if (!task || task->state != DlState::Failed) return false;
    task->state = DlState::Queued;
    task->failures = 0;
    return true;
}

void DownloadQueue::clearLoginBlock() {
    for (auto& entry : blocks_) entry.second.badLogin = false;
}

DlHostBlocks DownloadQueue::blocks(const std::string& host) const {
    auto it = blocks_.find(host);
    return it == blocks_.end() ? DlHostBlocks{} : it->second;
}

void DownloadQueue::setBlocks(const std::string& host, DlHostBlocks blocks) {
    blocks_[host] = blocks;
}

std::vector<std::pair<std::string, DlHostBlocks>> DownloadQueue::allBlocks() const {
    return {blocks_.begin(), blocks_.end()};
}

bool DownloadQueue::held(const DlTask& task, int64_t now) const {
    for (const std::string& host : task.hosts) {
        auto block = blocks_.find(host);
        if (block != blocks_.end() && (block->second.badLogin || block->second.quotaUntil > now))
            return true;
    }
    return false;
}

DlOverview DownloadQueue::overview(int64_t now) const {
    DlOverview out;
    const DlHostBlocks ss = blocks(kHostScreenScraper);
    std::map<GroupKey, size_t> index;
    for (const DlTask& task : tasks_) {
        if (task.state == DlState::Done) continue;
        const GroupKey key{task.group, task.kind};
        auto found = index.find(key);
        if (found == index.end()) {
            found = index.emplace(key, out.groups.size()).first;
            DlGroup fresh;
            fresh.group = task.group;
            fresh.kind = task.kind;
            auto done = finished_.find(key);
            if (done != finished_.end()) fresh.finished = done->second;
            out.groups.push_back(std::move(fresh));
        }
        DlGroup& group = out.groups[found->second];
        switch (task.state) {
            case DlState::Running:
                group.running++;
                group.runningId = task.id;
                group.runningName = taskName(task);
                break;
            case DlState::Queued:
                if (!held(task, now)) {
                    group.queued++;
                    break;
                }
                group.held++;
                if (std::find(task.hosts.begin(), task.hosts.end(), kHostScreenScraper) !=
                    task.hosts.end()) {
                    if (ss.badLogin) out.badLogin = true;
                    if (ss.quotaUntil > now) out.quotaUntil = ss.quotaUntil;
                }
                break;
            case DlState::Paused:
                group.paused++;
                if (task.diskPaused) out.diskFull = true;
                break;
            case DlState::Failed: group.failed++; break;
            case DlState::Done: break;
        }
    }
    return out;
}

size_t DlOverview::active() const {
    size_t count = 0;
    for (const DlGroup& group : groups) count += group.running + group.queued;
    return count;
}

float DlOverview::progress(float runningProgress) const {
    const float running = std::clamp(runningProgress, 0.0f, 1.0f);
    float done = 0;
    size_t total = 0;
    for (const DlGroup& group : groups) {
        if (group.running + group.queued == 0) continue;
        done += (float)group.finished + (group.running > 0 ? running : 0.0f);
        total += group.total();
    }
    return total > 0 ? std::min(done / (float)total, 1.0f) : 0.0f;
}

std::string taskName(const DlTask& task) {
    if (task.kind != DlKind::Cover && task.kind != DlKind::GameData) return {};
    const std::string mark = ":" + task.group + ":";
    const size_t at = task.id.find(mark);
    return at == std::string::npos ? std::string() : task.id.substr(at + mark.size());
}

}  // namespace emu
