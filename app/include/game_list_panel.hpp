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

#include <algorithm>
#include <cstddef>
#include <cstdint>
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

    // Only the rows near the screen exist; bindRow fills one for items[i]
    // whenever it comes into view (name, cover, data state, onPlay). The
    // query and the sort survive a new set, and the first result becomes the
    // current game (reported through onFocusGame). prepared saves sorting here
    // and is ignored if the query or the sort changed since orderRequest.
    void setItems(std::vector<emu::LibraryItem> items, emu::PreparedOrder prepared);
    // The query and sort to prepare an order for, off the UI thread.
    emu::PreparedOrder orderRequest() const;
    // One game's name, year or genre changed; the order follows, at once or,
    // while held, when the hold ends.
    void setItem(size_t index, emu::LibraryItem item);
    // A download of many games renames them one after another: sorting the
    // whole library again for each would be wasted, and the rows would keep
    // moving under the user. Held, the order waits for the end.
    void holdOrder(bool hold);
    // Fills that row again, if it exists now.
    void refreshRow(size_t index);
    void refreshRows();
    std::function<void(GameRowCell&, size_t)> bindRow;
    // 0 cartridges, three at a time: the picked game big, the one above and
    // the one below smaller beside their names. 1 list of names.
    void setLook(int view);
    // The count line says the folder is being read.
    void setScanning(bool on);

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
    // The items shown within `radius` places of that item, in display order:
    // `radius` before it and `radius` from it on. Empty when it is not shown.
    std::vector<size_t> itemsAround(size_t index, size_t radius) const {
        if (index >= shownAt.size() || shownAt[index] >= shown.size())
            return {};
        const size_t place = shownAt[index];
        const size_t from = place > radius ? place - radius : 0;
        const size_t to = std::min(place + radius, shown.size());
        return std::vector<size_t>(shown.begin() + from, shown.begin() + to);
    }
    // Focuses that game's row; when the filter hides it, the first result;
    // with no result, the search field.
    void focusGame(size_t index);

    // Keeps the shortcuts but takes them off the footer, for a screen that
    // lists them elsewhere.
    void hideShortcutHints();
    std::string batchActionLabel() const;
    bool batchActionAvailable() const;
    void openSearch();
    void openSort();
    void toggleMode();
    void downloadAllOrCancel();

    // The current game, which is the one the cartridge shows and the details
    // panel should show. It follows the focus on a row, and also changes
    // without focus on a row (a filter that hides it, a new set of items).
    // Fires only when it changes to a different game. nullopt: no game is
    // current (nothing matches the search, or no items).
    std::function<void(std::optional<size_t>)> onFocusGame;
    // Also "Try again" after a stop.
    std::function<void()> onDownloadAll, onRetry, onCancel;

    void willAppear(bool resetState = false) override;
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

  private:
    enum class Batch { Idle, Running, Done, Stopped };

    std::vector<emu::LibraryItem> items;
    std::vector<size_t> shown; // indexes into items, in display order
    std::vector<size_t> shownAt; // for each item, its place in shown, or NoGame
    std::vector<GameRowCell*> pool; // every row; owned by list
    std::vector<GameRowCell*> live; // the rows of shown[windowStart...], in order
    size_t windowStart = 0;
    bool windowDirty = true;
    bool orderHeld = false;
    bool orderStale = false; // setItem changed the order while it was held
    std::optional<size_t> current; // the cartridge, even while the search has focus
    std::string query;
    int sort = 0;
    int mode = 0;
    // Mode 0: the picked game's row and its neighbours' rows, so that the
    // three fill the column.
    float cartRow = GameRowCell::CartridgeHeight;
    float tapeRow = GameRowCell::TapeHeight;
    float step() const;  // from one row's top to the next one's, without the cartridge
    float extra() const; // what the cartridge's row adds
    void applySizes();
    unsigned recenterToken = 0;
    bool restoringFocus = false;
    bool scanning = false;
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
    brls::Button* retryButton = nullptr;
    brls::Button* cancelButton = nullptr;
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
    void registerShortcuts(bool hidden);
    // resetScroll: the order or the filter changed, not just a game's data.
    // ready: the new order when it is already known.
    void applyOrder(bool resetScroll, std::vector<size_t>* ready = nullptr);
    void applyLooks(bool animate);
    RowLook lookOf(size_t index) const;
    // The place in shown the rows should gather around.
    size_t anchorPlace();
    void fillWindow(size_t anchor);
    void placeRows();
    void measureFrame(int64_t start, int64_t windowUs, int64_t drawUs);
    void checkRows() const;
    void checkFocusAndView() const;
    GameRowCell* rowAt(size_t place) const;
    brls::View* rowNextTo(brls::View* from, brls::FocusDirection direction) const;
    brls::View* defaultRow() const;
    void setCurrent(std::optional<size_t> next, bool animate);
    void focusedGame(size_t index);
    void scheduleRecenter();
    bool focusInList() const;
    Batch batchState() const;

    void setQuery(const std::string& text);
    void chooseSort(int value);
};
