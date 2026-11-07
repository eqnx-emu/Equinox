//
//  game_list_panel.hpp
//  Equinox
//
//  The left column of the game library: console name and count, the cartridge
//  or list toggle, search, sort, the games, and the "download all" area.
//

#pragma once

#include "emu/Console.hpp"
#include "emu/GameDataJob.hpp"
#include "emu/Library.hpp"
#include "game_row_cell.hpp"
#include <borealis.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class LibraryScroll;

class GameListPanel : public brls::Box {
  public:
    explicit GameListPanel(const emu::Console& console);
    ~GameListPanel() override;

    // cells[i] is the row of items[i]. The panel takes them over: it frees the
    // previous set, owns onFocused of every cell (onPlay stays the caller's)
    // and sets their look from the mode. Covers and data state are the
    // caller's. The query and the sort survive a new set, and the first
    // result becomes the current game (reported through onFocusGame).
    void setItems(std::vector<emu::LibraryItem> items, std::vector<GameRowCell*> cells);
    // One game's name, year or genre changed; the order follows.
    void setItem(size_t index, emu::LibraryItem item);
    // 0 cartridge for the picked game and names for the rest, 1 list with thumbnails.
    void setLook(int view);

    // The batch of "download all". done counts finished games, so the line
    // reads "Downloading min(done + 1, total) of total". A running batch
    // clears the stop notice; missing is the number of games without data.
    void setBatch(bool running, size_t done, size_t total, int percent,
                  const std::string& currentName, size_t missing);
    // The notice stays until the user tries again, or leaves the screen.
    // None and Cancelled clear it, like no value.
    void setBatchStopped(std::optional<emu::DataStop> stop);

    // Index in the items given to setItems of the focused game, if any.
    std::optional<size_t> focusedIndex() const;
    // Focuses that game's row; when the filter hides it, the first result;
    // with no result, the search field.
    void focusGame(size_t index);

    // The current game, which is the one the cartridge shows and the details
    // panel should show. It follows the focus on a row, and also changes
    // without focus on a row (a filter that hides it, a new set of items).
    // Fires only when it changes to a different game. nullopt: no game is
    // current (nothing matches the search, or no items).
    std::function<void(std::optional<size_t>)> onFocusGame;
    // Also "Try again" after a stop.
    std::function<void()> onDownloadAll, onCancel;

    void willAppear(bool resetState = false) override;

  private:
    enum class Batch { Idle, Running, Done, Stopped };

    std::vector<emu::LibraryItem> items;
    std::vector<GameRowCell*> cells;
    std::vector<size_t> shown; // indexes into items, in display order
    std::optional<size_t> current; // the cartridge, even while the search has focus
    std::string query;
    int sort = 0;
    int mode = 0;
    unsigned recenterToken = 0;
    bool restoringFocus = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    bool running = false;
    size_t batchDone = 0, batchTotal = 0, missing = 0;
    int batchPercent = 0;
    std::string batchName;
    std::optional<emu::DataStop> stopped;

    std::string consoleName;
    brls::Label* countLabel = nullptr;
    brls::Box* cartButton = nullptr;
    brls::Box* listButton = nullptr;
    brls::Label* cartIcon = nullptr;
    brls::Label* listIcon = nullptr;
    brls::Box* searchBox = nullptr;
    brls::Label* searchIcon = nullptr;
    brls::Label* searchLabel = nullptr;
    brls::Box* sortBox = nullptr;
    brls::Label* sortLabel = nullptr;
    brls::Label* sortArrow = nullptr;
    LibraryScroll* scroll = nullptr;
    brls::Box* list = nullptr;
    brls::Label* noneLabel = nullptr;

    brls::Rectangle* separator = nullptr;
    brls::Box* footer = nullptr;
    brls::Box* idleRow = nullptr;
    brls::Label* idleIcon = nullptr;
    brls::Label* idleMissing = nullptr;
    brls::Label* doneLabel = nullptr;
    brls::Box* stoppedRow = nullptr;
    brls::Label* stoppedLabel = nullptr;
    brls::Box* runningRow = nullptr;
    brls::Label* runningLine = nullptr;
    brls::Label* runningName = nullptr;
    brls::View* bar = nullptr;

    void applyColors();
    void applyCount();
    void applyQuery();
    void applySort();
    void applyToggle();
    void applyBatch();
    void applyOrder();
    void applyLooks(bool animate);
    void setCurrent(std::optional<size_t> next, bool animate);
    void focusedGame(size_t index);
    void scheduleRecenter();
    bool focusInList() const;
    Batch batchState() const;

    void openSearch();
    void openSort();
    void toggleMode();
    void downloadAllOrCancel();
    void setQuery(const std::string& text);
    void chooseSort(int value);
};
