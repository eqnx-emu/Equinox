#include "emu/DownloadQueue.hpp"

#include <algorithm>
#include <unordered_map>

namespace emu {

namespace {

constexpr int64_t kQuotaSeconds = 86400;
constexpr int64_t kBackoffSeconds[] = {5, 30, 120};
constexpr int kMaxFailures = 4;

}  // namespace

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

void DownloadQueue::finish(const std::string& id, DlResult result, const std::string& host,
                           int64_t now) {
    DlTask* task = find(id);
    if (!task) return;
    switch (result) {
        case DlResult::Done:
            task->state = DlState::Done;
            task->failures = 0;
            backoff_.erase(host);
            break;
        case DlResult::Gone:
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
            pauseAll();
            break;
        case DlResult::Invalid:
            task->state = DlState::Failed;
            break;
    }
}

void DownloadQueue::requeueRunning() {
    for (DlTask& task : tasks_)
        if (task.state == DlState::Running) task.state = DlState::Queued;
}

void DownloadQueue::pause(const std::string& id) {
    DlTask* task = find(id);
    if (task && (task->state == DlState::Queued || task->state == DlState::Running))
        task->state = DlState::Paused;
}

void DownloadQueue::pauseAll() {
    for (DlTask& task : tasks_)
        if (task.state == DlState::Queued || task.state == DlState::Running)
            task.state = DlState::Paused;
}

void DownloadQueue::resume(const std::string& id) {
    DlTask* task = find(id);
    if (task && task->state == DlState::Paused) task->state = DlState::Queued;
}

void DownloadQueue::cancel(const std::string& id) {
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(),
                                [&](const DlTask& t) { return t.id == id; }),
                 tasks_.end());
}

void DownloadQueue::cancelGroup(const std::string& group, DlKind kind) {
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(),
                                [&](const DlTask& t) { return t.group == group && t.kind == kind; }),
                 tasks_.end());
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

}  // namespace emu
