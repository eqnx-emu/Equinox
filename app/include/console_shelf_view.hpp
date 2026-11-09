//
//  console_shelf_view.hpp
//  Equinox
//
//  A console's games as a library: the list on the left, the picked game's
//  details on the right. Covers come from libretro through the download
//  service; the rest of a game's data only when asked for. A plays,
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
#include "emu/LibraryIndex.hpp"
#include "emu/LibraryLog.hpp"
#include "emu/RomScan.hpp"
#include <borealis.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

struct DlEvent;
class GameDataQueue;
class GameDetailsPanel;
class GameListPanel;
class GameRowCell;
struct GameDetails;

// Opens a console game from outside its shelf (Favorites) once the downloads
// have stopped, like the shelf does, showing "Opening the game..." while it
// waits and giving up after a few seconds. onFail runs when the game was not
// opened (and `alive` is still true). Switch only.
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

    using Game = emu::LibraryGame;

  private:
    const emu::Console& consoleRef;
    emu::ConsolePaths paths;
    std::string restoreRom;

    std::vector<Game> games;
    std::optional<size_t> current;   // the game the details show
    brls::Time currentAt = 0;
    // Bumped on each rebuild, so work begun for the old list is dropped.
    uint64_t generation = 0;
    bool scanning = false;
    bool launching = false;
    // Cleared on destruction. Work finishing in the background checks it on
    // the UI thread before touching the view.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

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
    brls::Box* loading = nullptr;
    // The user asked for this look: say how it ended.
    bool announceScan = false;
    GameListPanel* listPanel = nullptr;
    GameDetailsPanel* detailsPanel = nullptr;

    CoverAmbient ambient;

    // The index on the card, when there is one, then a look at the folder.
    void openLibrary();
    // Every game on the card, read again.
    void rescan(bool clearMisses);
    void showLoading();
    // In the background: games put on or taken off the card since the index.
    void checkFolder();
    struct FolderChange {
        std::vector<Game> added;
        std::vector<std::string> removed; // file names
    };
    void applyFolderChange(const FolderChange& change, uint64_t forGeneration);
    bool indexDirty = false;
    bool indexSaving = false;
    // wait: before the app closes, when a background save would not finish.
    void saveIndex(bool wait = false);
    void openMore();
    // A finished scan, all of it worked out off the UI thread.
    struct Scan {
        std::vector<Game> games;
        std::vector<emu::LibraryItem> items; // items[i] names games[i]
        emu::PreparedOrder order;
    };
    void showGames(Scan& scan);
    void onCurrent(std::optional<size_t> index);
    void play(size_t index);
    // The game as it was when play() started: the list may be rebuilt while
    // the launch waits.
    struct LaunchTarget {
        size_t index;
        emu::RomEntry rom;
        uint64_t generation;
        std::shared_ptr<bool> stopped; // the downloads have stopped
    };
    // Polls (on the UI thread) until the downloads have stopped, then
    // launches; gives up after a few seconds.
    void waitForCovers(const LaunchTarget& target, int attempt);
    void launch(const LaunchTarget& target);
    // The launch did not happen: closes the "Opening" dialog and lets the
    // downloads go on.
    void endLaunch(bool notify);
    brls::Dialog* stopLoader = nullptr;

    const emu::GameInfo* infoFor(const Game& game) const;
    emu::LibraryItem itemFor(const Game& game) const;
    GameDetails detailsFor(size_t index) const;
    void bindRow(GameRowCell& cell, size_t index);
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

    // Queues the missing covers: those around `first` as Visible.
    void fetchCovers(size_t first);
    // The game's data brought a canonical name: tries its cover once more.
    void retryCover(size_t index);
    void scheduleCoverPriorities();
    void applyCoverPriorities();
    void coverEvent(const DlEvent& event);
    void coverArrived(size_t index);
    void coverMissedOn(size_t index);
    // The rows of each cover this list is waiting for. Cleared with the list,
    // so a cover asked for by an earlier one finds no row.
    std::unordered_map<std::string, std::vector<size_t>> coverRows;
    std::set<std::string> visibleCovers; // ids queued as Visible
    bool coverPrioritiesDue = false;
    // A cover hit a full card: said once, until something is written again.
    bool coverDiskNotified = false;
    int coverToken = 0;
};
