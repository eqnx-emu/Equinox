//
//  console_shelf_view.hpp
//  Equinox
//
//  A console's games as a library: the list on the left, the picked game's
//  details on the right. Covers come from libretro, one at a time, while the
//  library is open; the rest of a game's data only when asked for. A plays,
//  Y stars, X opens the other actions in a side sheet, B goes
//  back.
//

#pragma once

#include "cover_ambient.hpp"
#include "emu/Console.hpp"
#include "emu/GameDataJob.hpp"
#include "emu/GameDb.hpp"
#include "emu/GameMeta.hpp"
#include "emu/LaunchLine.hpp"
#include "emu/Library.hpp"
#include "emu/RomScan.hpp"
#include <borealis.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class GameDataQueue;
class GameDetailsPanel;
class GameListPanel;
class GameRowCell;
struct GameDetails;

// Opens a console game from outside its shelf (Favorites) once no cover is
// downloading, like the shelf does: polls the shared cover-busy flag on the UI
// thread, showing "Opening the game..." while it waits, and gives up after
// a few seconds. onFail runs when the game was not opened (and `alive` is
// still true). Switch only.
void launchWhenCoversIdle(const emu::Console& console, const emu::RomEntry& rom,
                          emu::Origin origin, int filter,
                          std::shared_ptr<bool> alive,
                          std::function<void()> onFail);

class ConsoleShelfView : public brls::Box {
  public:
    // selectRom: file name of the game to land on (back from playing it).
    ConsoleShelfView(const emu::Console& console,
                     const std::string& selectRom = "");
    ~ConsoleShelfView() override;

    void willAppear(bool resetState) override;
    void willDisappear(bool resetState) override;
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    struct Game {
        emu::RomEntry rom;
        bool hasCover = false;
        bool missed = false; // libretro had no cover, as of this version
        // A meta that still describes the ROM on the card.
        bool hasData = false;
        emu::GameMeta meta;
        std::string snapPath, titlePath; // empty when the file is not there
    };
    // Shared with the cover tasks on the async pool.
    struct CoverQueue;

  private:
    const emu::Console& consoleRef;
    emu::ConsolePaths paths;
    std::string restoreRom;

    std::vector<Game> games;
    std::vector<GameRowCell*> cells; // owned by listPanel
    std::optional<size_t> current;   // the game the details show
    brls::Time currentAt = 0;
    // Bumped on each rebuild, so covers for the old cells are dropped.
    uint64_t generation = 0;
    bool scanning = false;
    bool launching = false;
    // Cleared on destruction. Work finishing in the background checks it on
    // the UI thread before touching the view.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::shared_ptr<CoverQueue> covers;

    // Set by the first scan, together with dataQueue.
    std::shared_ptr<const emu::GameDb> db;
    std::unique_ptr<GameDataQueue> dataQueue;
    std::optional<emu::DataStep> dataStep; // of dataQueue->current()
    struct Failure {
        size_t index;
        emu::DataStop stop;
    };
    // Shown on that game's details until a download starts again.
    std::optional<Failure> failure;

    brls::Box* library = nullptr;
    brls::Box* empty = nullptr;
    GameListPanel* listPanel = nullptr;
    GameDetailsPanel* detailsPanel = nullptr;

    CoverAmbient ambient;

    void rescan(bool clearMisses);
    void openMore();
    void showGames(std::vector<Game> found);
    void onCurrent(std::optional<size_t> index);
    void play(size_t index);
    // The game as it was when play() started: the list may be rebuilt while
    // the launch waits.
    struct LaunchTarget {
        size_t index;
        emu::RomEntry rom;
        uint64_t generation;
    };
    // Polls (on the UI thread) until the network is free, then launches;
    // gives up after a few seconds.
    void waitForCovers(const LaunchTarget& target, int attempt);
    void launch(const LaunchTarget& target);
    // The launch did not happen: closes the "Opening" dialog, lets the covers
    // resume and, with releaseGate, frees the network gate it held.
    void endLaunch(bool releaseGate, bool notify);
    brls::Dialog* stopLoader = nullptr;

    const emu::GameInfo* infoFor(const Game& game) const;
    emu::LibraryItem itemFor(const Game& game) const;
    GameDetails detailsFor(size_t index) const;
    void applyRow(size_t index);
    void applyAmbient();
    void applyActions();
    // Rows, the details' action and the download-all area, from the queue.
    void applyProgress();
    // The details' button that had the focus may have just gone.
    void keepFocus();

    void createDataQueue();
    void download(size_t index);
    void downloadAll();
    // The bottom notice's retry: the game that stopped, then the rest of the
    // batch when it stopped inside one.
    void retryStopped();
    void cancelData();
    void dataFinished(size_t index, const emu::DataOutcome& outcome);
    void dataStopped(emu::DataStop stop);
    std::optional<size_t> stoppedIndex;
    // Set by downloadAll, cleared when the queue empties or stops.
    bool batchActive = false;
    bool stoppedInBatch = false;
    struct Retry {
        size_t index;
        bool batch;
    };
    // What the bottom notice's retry re-queues.
    std::optional<Retry> retry;

    // Downloads the missing covers, nearest to `first` first.
    void fetchCovers(size_t first);
    void stopCovers();
    std::shared_ptr<CoverQueue> newCoverQueue();
    // The game's data brought a canonical name: tries its cover once more.
    void retryCover(size_t index);
    static void runNextCover(std::shared_ptr<CoverQueue> queue);
    static void fetchOneCover(std::shared_ptr<CoverQueue> queue);
    void coverArrived(uint64_t forGeneration, size_t index,
                      const std::string& path);
};
