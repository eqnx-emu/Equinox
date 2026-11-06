//
//  host_card.hpp
//  Equinox
//
//  One paired PC on the home screen: a horizon illustration that shows the
//  host state at a glance (sun = online, moon = off or asleep, empty outline
//  = out of reach), the running game, and the main action for that state.
//

#pragma once

#include "GameStreamClient.hpp"
#include <Settings.hpp>
#include <borealis.hpp>
#include <cstdint>

enum class HostCardState { FETCHING, ONLINE, RUNNING, ASLEEP, UNREACHABLE };

class HorizonView : public brls::View {
  public:
    void setState(HostCardState state) { this->state = state; }
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    static brls::View* create() { return new HorizonView(); }

  private:
    HostCardState state = HostCardState::FETCHING;
};

class HostCard : public brls::Box {
  public:
    explicit HostCard(const Host& host);

    // Re-queries the host and updates the card.
    void refresh();

    BRLS_BIND(HorizonView, horizon, "horizon");
    BRLS_BIND(brls::Label, name, "name");
    BRLS_BIND(brls::Label, status, "status");
    BRLS_BIND(brls::Box, infoBox, "info_box");
    BRLS_BIND(brls::Label, infoCaption, "info_caption");
    BRLS_BIND(brls::Label, infoValue, "info_value");
    BRLS_BIND(brls::Box, actionPill, "action_pill");
    BRLS_BIND(brls::Label, actionLabel, "action_label");

  private:
    Host host;
    HostCardState state = HostCardState::FETCHING;
    AppInfo runningApp{"", 0};
    uint64_t refreshGeneration = 0;
    bool requestInFlight = false;
    uint64_t wakeRequestGeneration = 0;
    uint64_t canceledWakeRequestGeneration = 0;

    void applyState(HostCardState newState);
    void activate();
    void openLibrary();
    void resumeRunningApp();
    void wake();
    void openOptions();
};

// Last card of the grid: opens the screen that finds or adds a PC.
class AddHostCard : public brls::Box {
  public:
    // primary: no PC is paired yet, so this is the main action on screen.
    explicit AddHostCard(bool primary);

    BRLS_BIND(HorizonView, horizon, "horizon");
    BRLS_BIND(brls::Label, name, "name");
    BRLS_BIND(brls::Label, status, "status");
    BRLS_BIND(brls::Box, infoBox, "info_box");
    BRLS_BIND(brls::Box, actionPill, "action_pill");
    BRLS_BIND(brls::Label, actionLabel, "action_label");
};
