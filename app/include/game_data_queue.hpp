//
//  game_data_queue.hpp
//  Equinox
//
//  Downloads the data of the games it is given (snap, title screen, synopsis),
//  one game at a time. UI thread only; each game runs as one brls::async task
//  that takes the shared network gate (emu::netTryAcquire) for as long as it
//  downloads. Callbacks run on the UI thread.
//

#pragma once

#include "emu/Console.hpp"
#include "emu/GameDataJob.hpp"
#include "emu/GameDb.hpp"
#include "emu/RomScan.hpp"

#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <optional>

class GameDataQueue {
  public:
    GameDataQueue(const emu::Console& console, emu::ConsolePaths paths, const emu::GameDb* db);
    ~GameDataQueue();
    GameDataQueue(const GameDataQueue&) = delete;
    GameDataQueue& operator=(const GameDataQueue&) = delete;

    // Ignored when the game is already waiting or being downloaded.
    void enqueue(size_t index, const emu::RomEntry& rom);
    // Stops the game in progress and drops the rest. No callback fires.
    void cancel();

    bool running() const;
    // Of the current batch; both go back to zero when the queue empties.
    size_t total() const { return total_; }
    size_t done() const { return done_; }
    std::optional<size_t> current() const { return current_; }
    bool queued(size_t index) const;

    std::function<void(size_t index, emu::DataStep)> onStep;
    std::function<void(size_t index, const emu::DataOutcome&)> onFinished;
    // The next game of the batch was just handed to the pool.
    std::function<void()> onAdvanced;
    // The queue stopped (offline, bad login, quota, disk) without finishing.
    std::function<void(emu::DataStop)> onStopped;

  private:
    struct Pending {
        size_t index;
        emu::RomEntry rom;
    };
    struct Job;

    void pump();
    void schedule(std::shared_ptr<Job> job);
    void finish(const std::shared_ptr<Job>& job, const emu::DataOutcome& outcome);
    void reset();
    static void work(std::shared_ptr<Job> job);

    const emu::Console* console_;
    emu::ConsolePaths paths_;
    const emu::GameDb* db_;

    std::deque<Pending> pending_;
    std::optional<size_t> current_;
    size_t total_ = 0;
    size_t done_ = 0;
    std::shared_ptr<std::atomic<bool>> stop_;
    std::shared_ptr<bool> alive_;
};
