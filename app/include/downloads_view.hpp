//
//  downloads_view.hpp
//  Equinox
//
//  The downloads screen: on the left, one compact row per console and kind
//  (covers, game data, the emulator) and one for the app update, under a
//  heading per console; on the right, the focused row in detail with its
//  actions. A small ring on the home screen says something is downloading.
//  Switch only.
//

#pragma once

#include <borealis.hpp>

#include "emu/DownloadQueue.hpp"

#include <chrono>
#include <functional>
#include <string>
#include <vector>

class LibraryProgressBar;
class PressableBox;

// Service events only mark the view stale; it reads the queue again at most
// this often, while it is drawn.
class DownloadsRefresh {
  public:
    // Subscribes; `refresh` runs from tick(). The destructor unsubscribes,
    // so no event reaches a destroyed owner.
    explicit DownloadsRefresh(std::function<void()> refresh);
    ~DownloadsRefresh();
    DownloadsRefresh(const DownloadsRefresh&) = delete;
    DownloadsRefresh& operator=(const DownloadsRefresh&) = delete;

    // From the owner's draw: refreshes when stale and the interval has passed.
    void tick();
    void now();  // refreshes at once

    // Progress of the last task seen running.
    const std::string& runningId() const { return runningId_; }
    float progress() const { return progress_; }
    // The progress of the group's running task, or 0 when it is not the one
    // last seen running.
    float progressOf(const emu::DlGroup& group) const;

  private:
    using Clock = std::chrono::steady_clock;

    std::function<void()> refresh_;
    int token_ = 0;
    bool stale_ = true;
    Clock::time_point last_{};
    std::string runningId_;
    float progress_ = 0;
};

class DownloadsView : public brls::Box {
  public:
    DownloadsView();

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

  private:
    class Row;
    class Panes;

    DownloadsRefresh refresher;
    brls::Label* summary = nullptr;  // in the header, on the right
    brls::Box* notices = nullptr;
    brls::Box* loginNotice = nullptr;
    brls::Box* quotaNotice = nullptr;
    brls::Label* quotaText = nullptr;
    brls::Box* diskNotice = nullptr;
    Panes* panes = nullptr;
    brls::Box* list = nullptr;
    std::vector<Row*> rows;  // reused in place, hidden when not needed
    brls::Box* empty = nullptr;

    // The detail pane, showing the selected group.
    brls::Label* paneConsole = nullptr;
    brls::Label* paneTitle = nullptr;
    brls::Label* panePercent = nullptr;
    brls::Label* paneCount = nullptr;
    LibraryProgressBar* paneBar = nullptr;
    brls::Box* tiles = nullptr;
    brls::Box* nowTile = nullptr;
    brls::Label* nowValue = nullptr;
    brls::Label* leftValue = nullptr;
    brls::Label* failedValue = nullptr;
    brls::Box* actions = nullptr;
    PressableBox* pauseButton = nullptr;
    brls::Label* pauseIcon = nullptr;
    brls::Label* pauseLabel = nullptr;
    PressableBox* retryButton = nullptr;
    brls::Label* retryLabel = nullptr;
    PressableBox* cancelButton = nullptr;

    emu::DlOverview shown;    // the last overview read
    std::string selectedKey;  // the group the pane shows
    std::string yHint;        // what Y says now, to set it only on a change

    void refresh();
    void select(const std::string& key);
    void fillPane();
    const emu::DlGroup* selectedGroup() const;
    Row* selectedRow() const;
    View* firstAction() const;
    // Y: pauses everything, or resumes it when nothing runs and something
    // is paused.
    bool resumesAll() const;
    // Focus left on something that just went away moves to what is shown.
    void keepFocus();
};

// From start, for as long as the app runs: one notification each time a full
// card pauses the downloads, whatever screen is open.
void watchDiskStops();

// A pill at the top right of the home screen: a ring with how far the active
// downloads are, the download arrow inside, and how many there are. Shown
// and focusable only while something downloads; A or a tap opens the screen.
class DownloadIndicator : public brls::Box {
  public:
    DownloadIndicator();

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    std::function<void()> onOpen;
    // It hid while it had the focus: the owner takes the focus back.
    std::function<void()> onHidden;

    bool shown() const { return active > 0; }

  private:
    DownloadsRefresh refresher;
    brls::Box* ring = nullptr;  // the space the ring is drawn in
    brls::Label* count = nullptr;
    size_t active = 0;
    float fraction = 0;

    void refresh();
    void setActive(bool on);
};
