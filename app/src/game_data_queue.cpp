//
//  game_data_queue.cpp
//  Equinox
//

#include "game_data_queue.hpp"

#include "download_service.hpp"

#include <cmath>

namespace {

emu::DataStep stepFor(float progress) {
    switch (std::lround(progress * 4)) {
        case 0: return emu::DataStep::Identify;
        case 1: return emu::DataStep::Snap;
        case 2: return emu::DataStep::Title;
        case 3: return emu::DataStep::Synopsis;
        default: return emu::DataStep::Done;
    }
}

bool stoppedAll(emu::DlResult result) {
    return result == emu::DlResult::BadLogin || result == emu::DlResult::Quota ||
           result == emu::DlResult::Disk;
}

emu::DataStop stopFor(const DlEvent& event) {
    if (event.data && event.data->stop != emu::DataStop::None) return event.data->stop;
    switch (event.result) {
        case emu::DlResult::BadLogin: return emu::DataStop::BadLogin;
        case emu::DlResult::Quota: return emu::DataStop::Quota;
        case emu::DlResult::Disk: return emu::DataStop::Disk;
        default: return emu::DataStop::Offline;
    }
}

}  // namespace

GameDataQueue::GameDataQueue(const emu::Console& console)
    : console_(&console), prefix_(emu::gameDataId(console.slug, "")) {
    token_ = DownloadService::instance().subscribe([this](const DlEvent& event) { handle(event); });
    adoptRunning();
}

GameDataQueue::~GameDataQueue() { DownloadService::instance().unsubscribe(token_); }

// Tasks a host block holds back stay out, or the footer would spin forever.
void GameDataQueue::adoptRunning() {
    DownloadService& service = DownloadService::instance();
    for (const emu::DlTask& task : service.snapshot()) {
        if (task.kind != emu::DlKind::GameData || task.group != console_->slug) continue;
        if (task.state != emu::DlState::Queued && task.state != emu::DlState::Running) continue;
        if (service.blocked(task)) continue;
        active_.insert(task.id);
        if (task.state == emu::DlState::Running) currentId_ = task.id;
    }
    total_ = done_ + active_.size();
}

// After another console's stop: bad login, quota and a full card hold back
// this console's tasks too, and the service sends no event for those.
void GameDataQueue::dropHeld() {
    DownloadService& service = DownloadService::instance();
    std::set<std::string> live;
    for (const emu::DlTask& task : service.snapshot()) {
        if (task.kind != emu::DlKind::GameData || task.group != console_->slug) continue;
        if (task.state != emu::DlState::Queued && task.state != emu::DlState::Running) continue;
        if (!service.blocked(task)) live.insert(task.id);
    }
    bool dropped = false;
    for (auto it = active_.begin(); it != active_.end();) {
        if (live.count(*it)) {
            ++it;
            continue;
        }
        if (currentId_ == *it) currentId_.reset();
        it = active_.erase(it);
        dropped = true;
    }
    if (!dropped) return;
    total_ = active_.empty() ? 0 : done_ + active_.size();
    if (active_.empty()) done_ = 0;
    if (onAdvanced) onAdvanced();
}

void GameDataQueue::setGames(std::vector<std::string> fileNames) {
    names_ = std::move(fileNames);
    indexByName_.clear();
    for (size_t i = 0; i < names_.size(); i++) indexByName_[names_[i]] = i;
}

std::optional<size_t> GameDataQueue::indexOf(const std::string& id) const {
    auto it = indexByName_.find(id.substr(prefix_.size()));
    if (it == indexByName_.end()) return std::nullopt;
    return it->second;
}

std::optional<size_t> GameDataQueue::current() const {
    return currentId_ ? indexOf(*currentId_) : std::nullopt;
}

bool GameDataQueue::queued(size_t index) const {
    return index < names_.size() && active_.count(prefix_ + names_[index]) > 0;
}

void GameDataQueue::enqueue(size_t index, const emu::RomEntry& rom, emu::DlPriority priority) {
    DownloadService& service = DownloadService::instance();
    const std::string id = emu::gameDataId(console_->slug, rom.fileName);
    if (active_.count(id)) {
        if (priority == emu::DlPriority::Interactive) service.setPriority(id, priority);
        return;
    }
    if (index >= names_.size()) names_.resize(index + 1);
    names_[index] = rom.fileName;
    indexByName_[rom.fileName] = index;
    active_.insert(id);
    total_++;
    if (!service.submit(emu::gameDataTask(*console_, rom, priority))) {
        // Left behind by an earlier stop: this is the user asking again.
        service.resume(id);
        service.retryFailed();
    }
}

void GameDataQueue::cancel() {
    DownloadService::instance().cancelGroup(console_->slug, emu::DlKind::GameData);
    active_.clear();
    currentId_.reset();
    total_ = done_ = 0;
}

// `id` left the batch, counted as handled.
void GameDataQueue::finishOne(const std::string& id) {
    active_.erase(id);
    if (currentId_ == id) currentId_.reset();
    done_++;
    if (active_.empty()) total_ = done_ = 0;
}

void GameDataQueue::handle(const DlEvent& event) {
    if (event.id.rfind(prefix_, 0) != 0) {
        // A cover that hit a full card holds the game data back too.
        if (stoppedAll(event.result)) dropHeld();
        return;
    }
    if (!active_.count(event.id)) {
        // Started without this view asking, such as after new credentials.
        if (event.state != emu::DlState::Running) return;
        adoptRunning();
        if (!active_.count(event.id)) return;
    }
    const std::optional<size_t> index = indexOf(event.id);

    if (event.state == emu::DlState::Running) {
        const bool advanced = currentId_ != event.id;
        currentId_ = event.id;
        if (index && onStep) onStep(*index, stepFor(event.progress));
        if (advanced && onAdvanced) onAdvanced();
        return;
    }

    const bool needsUser = stoppedAll(event.result) || event.state == emu::DlState::Failed;
    if (event.state == emu::DlState::Queued && !needsUser) {
        // Will run again: a retry after a transient failure, or a suspend.
        if (currentId_ != event.id) return;
        currentId_.reset();
        if (onAdvanced) onAdvanced();
        return;
    }
    if (event.state == emu::DlState::Paused && !needsUser) {
        active_.erase(event.id);
        if (currentId_ == event.id) currentId_.reset();
        if (total_ > 0) total_--;
        if (active_.empty()) total_ = done_ = 0;
        if (onAdvanced) onAdvanced();
        return;
    }

    // The service holds every other task back after these.
    if (stoppedAll(event.result)) {
        active_.clear();
        currentId_.reset();
        total_ = done_ = 0;
    } else {
        finishOne(event.id);
    }

    if (event.result == emu::DlResult::Cancelled) return;
    if (index && onFinished) onFinished(*index, event.data.value_or(emu::DataOutcome{}));
    if (needsUser && onStopped) onStopped(stopFor(event));
}
