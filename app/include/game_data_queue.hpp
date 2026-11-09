//
//  game_data_queue.hpp
//  Equinox
//
//  One console's view of the game data downloads, which run in
//  DownloadService and go on after the view closes. Translates the service's
//  ids to the view's list indexes. UI thread only; callbacks run on the UI
//  thread.
//

#pragma once

#include "emu/Console.hpp"
#include "emu/DownloadQueue.hpp"
#include "emu/GameDataJob.hpp"
#include "emu/RomScan.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

struct DlEvent;

class GameDataQueue {
  public:
    // Picks up what is already downloading for this console.
    explicit GameDataQueue(const emu::Console& console);
    // Unsubscribes only: the downloads continue.
    ~GameDataQueue();
    GameDataQueue(const GameDataQueue&) = delete;
    GameDataQueue& operator=(const GameDataQueue&) = delete;

    // The list as the view numbers it: indexes in the callbacks refer to this.
    void setGames(std::vector<std::string> fileNames);

    // When the game is already waiting, only raises an Interactive request.
    void enqueue(size_t index, const emu::RomEntry& rom,
                 emu::DlPriority priority = emu::DlPriority::Interactive);
    // Stops the game in progress and drops the rest. No callback fires.
    void cancel();

    bool running() const { return !active_.empty(); }
    // Of the current batch; both go back to zero when the queue empties.
    size_t total() const { return total_; }
    size_t done() const { return done_; }
    std::optional<size_t> current() const;
    bool queued(size_t index) const;

    std::function<void(size_t index, emu::DataStep)> onStep;
    std::function<void(size_t index, const emu::DataOutcome&)> onFinished;
    // The next game of the batch just started.
    std::function<void()> onAdvanced;
    // A download stopped and needs the user (bad login, quota, disk, gave up).
    std::function<void(emu::DataStop)> onStopped;

  private:
    void adoptRunning();
    void handle(const DlEvent& event);
    void finishOne(const std::string& id);
    std::optional<size_t> indexOf(const std::string& id) const;

    const emu::Console* console_;
    std::string prefix_;  // of this console's task ids
    int token_ = 0;

    std::vector<std::string> names_;
    std::map<std::string, size_t> indexByName_;
    std::set<std::string> active_;  // ids waiting or downloading
    std::optional<std::string> currentId_;
    size_t total_ = 0;
    size_t done_ = 0;
};
