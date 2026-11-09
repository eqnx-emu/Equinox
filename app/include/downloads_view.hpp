//
//  downloads_view.hpp
//  Equinox
//
//  The downloads screen: one row per console and kind (covers, game data,
//  the emulator) and one for the app update, each with its count, progress
//  and actions. A small badge elsewhere says something is downloading.
//  Switch only.
//

#pragma once

#include <borealis.hpp>

#include <chrono>
#include <functional>
#include <string>
#include <vector>

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

    DownloadsRefresh refresher;
    brls::Box* loginNotice = nullptr;
    brls::Box* quotaNotice = nullptr;
    brls::Label* quotaText = nullptr;
    brls::Box* resumeAll = nullptr;
    brls::Box* list = nullptr;
    brls::Box* empty = nullptr;
    std::vector<Row*> rows;  // reused in place, hidden when not needed

    void refresh();
    // Focus left on something that just went away moves to what is shown.
    void keepFocus(const std::string& focusedKey);
};

// The badge: an arrow and how many downloads are under way. Shown only while
// something downloads; a tap opens the screen.
class DownloadIndicator : public brls::Box {
  public:
    DownloadIndicator();

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    std::function<void()> onOpen;

  private:
    DownloadsRefresh refresher;
    brls::Label* count = nullptr;

    void refresh();
};
