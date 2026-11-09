//
//  host_card.hpp
//  Equinox
//
//  One paired PC on the home shelf: name, status, the address (or the
//  running game) and what the library holds. A opens its options on the
//  home screen; the card also runs them.
//

#pragma once

#include "GameStreamClient.hpp"
#include "cover_carousel.hpp"
#include "pressable_box.hpp"
#include "turntable.hpp"
#include <Settings.hpp>
#include <borealis.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

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

// With img/3d/host-pc.mesh in the resources the PC has no card: a 3D tower
// floats over the name, its LED in the state's colour.
class HostCard : public PressableBox, public ShelfItem {
  public:
    explicit HostCard(const Host& host);

    bool frameless() const override { return turntable != nullptr; }
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;
    void onFocusGained() override;
    void onFocusLost() override;

    // Re-queries the host and updates the card.
    void refresh();

    BRLS_BIND(MonitorView, monitor, "monitor");
    BRLS_BIND(brls::Label, name, "name");
    BRLS_BIND(brls::Box, statusDot, "status_dot");
    BRLS_BIND(brls::Label, status, "status");
    BRLS_BIND(brls::Label, detail, "detail");
    BRLS_BIND(brls::Label, info, "info");

    // A on the card (after the press animation).
    std::function<void()> onEnter;
    // State, running game or library facts changed.
    std::function<void()> onChanged;

    const Host& getHost() const { return host; }
    HostCardState getState() const { return state; }
    const AppInfo& getRunningApp() const { return runningApp; }
    std::string statusText() { return status->getFullText(); }
    std::string detailText() { return detail->getFullText(); }
    std::string infoText() { return info->getFullText(); }
    NVGcolor statusColor() const;

    void openLibrary();
    void resumeRunningApp();
    void quitRunningApp();
    void wake();
    void openCapacityTest();
    // Asks Solstice to add the PC's installed Steam games to its library.
    void importSteamGames();
    void rename();
    void remove();

  private:
    Host host;
    std::unique_ptr<Turntable> turntable;
    // Last known library facts; kept while the PC is asleep or away.
    int gameCount = -1;
    int favoriteCount = 0;
    // Games in the last app list, to count favorites still installed.
    std::vector<int> appIds;
    bool supports4K = false;
    void updateInfo();
    HostCardState state = HostCardState::FETCHING;
    AppInfo runningApp{"", 0};
    uint64_t refreshGeneration = 0;
    bool requestInFlight = false;
    uint64_t wakeRequestGeneration = 0;
    uint64_t canceledWakeRequestGeneration = 0;

    // The home shelf is rebuilt when PCs change. Cards remember what they
    // last saw for a short while, so a rebuild doesn't query every PC again.
    struct Snapshot {
        HostCardState state;
        AppInfo runningApp;
        int gameCount;
        std::vector<int> appIds;
        bool supports4K;
        int64_t takenAt;
    };
    static std::map<std::string, Snapshot>& snapshots();
    std::string snapshotKey() const;
    void saveSnapshot();
    bool restoreSnapshot();

    void applyState(HostCardState newState);
    void changed() {
        if (onChanged)
            onChanged();
    }
};

// Last card of the PC shelf: opens the screen that finds or adds a PC. With
// img/3d/host-add.mesh it is an open box with a plus instead of a card.
class AddHostCard : public PressableBox, public ShelfItem {
  public:
    // primary: no PC is paired yet, so this is the main action on screen.
    explicit AddHostCard(bool primary);

    bool frameless() const override { return turntable != nullptr; }
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;
    void onFocusGained() override;
    void onFocusLost() override;

    BRLS_BIND(MonitorView, monitor, "monitor");
    BRLS_BIND(brls::Label, name, "name");
    BRLS_BIND(brls::Box, statusDot, "status_dot");
    BRLS_BIND(brls::Label, status, "status");
    BRLS_BIND(brls::Label, detail, "detail");
    BRLS_BIND(brls::Label, info, "info");

  private:
    std::unique_ptr<Turntable> turntable;
};
