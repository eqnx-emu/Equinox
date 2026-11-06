//
//  host_card.hpp
//  Equinox
//
//  One paired PC on the home screen: a monitor that shows the host state at
//  a glance, the name, the status and the address (or the running game).
//  The whole card is the action; the footer hint names it.
//

#pragma once

#include "GameStreamClient.hpp"
#include "pressable_box.hpp"
#include <Settings.hpp>
#include <borealis.hpp>
#include <cstdint>
#include <map>
#include <string>

enum class HostCardState { FETCHING, ONLINE, RUNNING, ASLEEP, UNREACHABLE };

// A small monitor that shows the host state on its screen (sun = online,
// moon = off or asleep, dark = out of reach), with a status light. In "add"
// mode it draws a plus instead.
class MonitorView : public brls::View {
  public:
    void setState(HostCardState state) { this->state = state; }
    void setAddMode(bool primary) {
        this->add = true;
        this->primary = primary;
    }
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    static brls::View* create() { return new MonitorView(); }

  private:
    HostCardState state = HostCardState::FETCHING;
    bool add = false;
    bool primary = false;
};

class HostCard : public PressableBox {
  public:
    explicit HostCard(const Host& host);

    // Re-queries the host and updates the card.
    void refresh();

    BRLS_BIND(MonitorView, monitor, "monitor");
    BRLS_BIND(brls::Label, name, "name");
    BRLS_BIND(brls::Box, statusDot, "status_dot");
    BRLS_BIND(brls::Label, status, "status");
    BRLS_BIND(brls::Label, detail, "detail");
    BRLS_BIND(brls::Label, info, "info");

  private:
    Host host;
    // Last known library facts; kept while the PC is asleep or away.
    int gameCount = -1;
    int favoriteCount = 0;
    bool supports4K = false;
    void updateInfo();
    HostCardState state = HostCardState::FETCHING;
    AppInfo runningApp{"", 0};
    uint64_t refreshGeneration = 0;
    bool requestInFlight = false;
    uint64_t wakeRequestGeneration = 0;
    uint64_t canceledWakeRequestGeneration = 0;

    // The Hosts tab is rebuilt every time it is shown. Cards remember what
    // they last saw for a short while, so flipping tabs doesn't query every
    // PC again.
    struct Snapshot {
        HostCardState state;
        AppInfo runningApp;
        int gameCount;
        int favoriteCount;
        bool supports4K;
        int64_t takenAt;
    };
    static std::map<std::string, Snapshot>& snapshots();
    std::string snapshotKey() const;
    void saveSnapshot();
    bool restoreSnapshot();

    void applyState(HostCardState newState);
    void activate();
    void openLibrary();
    void resumeRunningApp();
    void wake();
    void openOptions();
};

// Last card of the grid: opens the screen that finds or adds a PC.
class AddHostCard : public PressableBox {
  public:
    // primary: no PC is paired yet, so this is the main action on screen.
    explicit AddHostCard(bool primary);

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    BRLS_BIND(MonitorView, monitor, "monitor");
    BRLS_BIND(brls::Label, name, "name");
    BRLS_BIND(brls::Box, statusDot, "status_dot");
    BRLS_BIND(brls::Label, status, "status");
    BRLS_BIND(brls::Label, detail, "detail");
    BRLS_BIND(brls::Label, info, "info");
};
