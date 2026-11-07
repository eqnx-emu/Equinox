//
//  console_shelf_view.hpp
//  Equinox
//
//  A console's games as a cover shelf, like a PC's library. Covers come
//  from libretro, one at a time, while the shelf is open. A plays, X looks
//  for games again, B goes back.
//

#pragma once

#include "cover_ambient.hpp"
#include "cover_carousel.hpp"
#include "emu/Console.hpp"
#include "emu/LaunchLine.hpp"
#include "emu/RomScan.hpp"
#include "rom_cell.hpp"
#include <borealis.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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
    };
    // Shared with the cover tasks on the async pool.
    struct CoverQueue;

  private:
    const emu::Console& consoleRef;
    emu::ConsolePaths paths;
    std::string restoreRom;

    std::vector<Game> games;
    std::vector<RomCell*> cells;
    // Bumped on each rebuild, so covers for the old cells are dropped.
    uint64_t generation = 0;
    bool scanning = false;
    bool launching = false;
    // Cleared on destruction. Work finishing in the background checks it on
    // the UI thread before touching the view.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::shared_ptr<CoverQueue> covers;
    // The cell showing "Fetching cover...", or -1. Only the cover being
    // downloaded right now; cleared whenever the queue stops.
    int fetchingIndex = -1;
    void showFetching(int index);

    brls::Box* shelf = nullptr;
    brls::Box* empty = nullptr;
    CoverCarousel* carousel = nullptr;
    brls::Label* titleLabel = nullptr;
    brls::Label* metaLabel = nullptr;
    CarouselDots* dots = nullptr;

    CoverAmbient ambient;
    brls::Animatable detailsFade = 1.0f;

    void rescan(bool clearMisses);
    void showGames(std::vector<Game> found);
    void onSelected(int index);
    void play(int index);
    // Polls (on the UI thread) until the cover task has finished, then
    // launches; gives up after a few seconds.
    void waitForCovers(int index, int attempt);
    void launch(int index);
    brls::Dialog* stopLoader = nullptr;
    std::string metaText(const Game& game) const;
    // Y stars or unstars the game; the label follows.
    void updateFavoriteAction(RomCell* cell);

    // Downloads the missing covers, nearest to `first` first.
    void fetchCovers(int first);
    void stopCovers();
    static void runNextCover(std::shared_ptr<CoverQueue> queue);
    static void fetchOneCover(std::shared_ptr<CoverQueue> queue);
    void coverArrived(uint64_t forGeneration, size_t index,
                      const std::string& path);
};
