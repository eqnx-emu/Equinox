//
//  game_data_queue.cpp
//  Equinox
//

#include "game_data_queue.hpp"

#include "emu/GameDataHttp.hpp"
#include "emu/NetGate.hpp"
#include "emu/SsCredentials.hpp"

#include <borealis.hpp>

namespace {
// Retry period while another download or a launch holds the network gate.
constexpr int kBusyRetryMs = 200;
}  // namespace

struct GameDataQueue::Job {
    size_t index;
    emu::GameDataRequest request;
    std::shared_ptr<std::atomic<bool>> stop;
    std::shared_ptr<bool> alive;
    GameDataQueue* queue = nullptr;  // only used when *alive
};

GameDataQueue::GameDataQueue(const emu::Console& console, emu::ConsolePaths paths,
                             const emu::GameDb* db)
    : console_(&console), paths_(std::move(paths)), db_(db),
      stop_(std::make_shared<std::atomic<bool>>(false)), alive_(std::make_shared<bool>(true)) {}

GameDataQueue::~GameDataQueue() {
    stop_->store(true);
    *alive_ = false;
}

bool GameDataQueue::running() const { return current_.has_value() || !pending_.empty(); }

bool GameDataQueue::queued(size_t index) const {
    if (current_ == index) return true;
    for (const Pending& p : pending_)
        if (p.index == index) return true;
    return false;
}

void GameDataQueue::enqueue(size_t index, const emu::RomEntry& rom) {
    if (queued(index)) return;
    pending_.push_back({index, rom});
    total_++;
    if (!current_) pump();
}

void GameDataQueue::cancel() {
    stop_->store(true);
    stop_ = std::make_shared<std::atomic<bool>>(false);
    pending_.clear();
    current_.reset();
    total_ = done_ = 0;
}

void GameDataQueue::reset() {
    pending_.clear();
    current_.reset();
    total_ = done_ = 0;
}

// Hands the next game to the pool.
void GameDataQueue::pump() {
    if (current_ || pending_.empty()) return;
    Pending next = std::move(pending_.front());
    pending_.pop_front();
    current_ = next.index;

    auto job = std::make_shared<Job>();
    job->index = next.index;
    job->request = {console_, paths_, std::move(next.rom), db_, emu::appSsConfig()};
    job->stop = stop_;
    job->alive = alive_;
    job->queue = this;
    schedule(job);
}

void GameDataQueue::schedule(std::shared_ptr<Job> job) {
    brls::async([job] { work(job); });
}

// Pool thread: one game.
void GameDataQueue::work(std::shared_ptr<Job> job) {
    if (job->stop->load()) return;
    // Covers or a launch have the network: try again soon, without holding
    // this worker.
    if (!emu::netTryAcquire()) {
        brls::delay(kBusyRetryMs, [job] {
            if (*job->alive && !job->stop->load()) job->queue->schedule(job);
        });
        return;
    }
    if (job->stop->load()) {
        emu::netRelease();
        return;
    }

    emu::HttpDataTransport transport(job->stop);
    const emu::DataOutcome outcome =
        emu::fetchGameData(job->request, transport, [job](emu::DataStep step) {
            if (job->stop->load()) return false;
            brls::sync([job, step] {
                if (*job->alive && !job->stop->load() && job->queue->onStep)
                    job->queue->onStep(job->index, step);
            });
            return true;
        });
    emu::netRelease();

    brls::sync([job, outcome] {
        if (*job->alive && !job->stop->load()) job->queue->finish(job, outcome);
    });
}

// UI thread.
void GameDataQueue::finish(const std::shared_ptr<Job>& job, const emu::DataOutcome& outcome) {
    done_++;
    current_.reset();
    const bool stopped = outcome.stop != emu::DataStop::None;
    if (stopped) pending_.clear();
    if (onFinished) onFinished(job->index, outcome);
    // A callback may have cancelled, or added games.
    if (job->stop->load()) return;

    if (stopped) {
        if (!current_) reset();
        if (onStopped) onStopped(outcome.stop);
        return;
    }
    if (pending_.empty() && !current_) reset();
    else pump();
}
