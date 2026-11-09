//
//  home_view.hpp
//  Equinox
//
//  Home screen: a shelf of floating cards (Seus PCs, Favoritos,
//  Configurações, Sobre). A on Seus PCs zooms into a second shelf with one
//  card per PC. A on any other card "enters" it: the card slides left, the
//  others fly away and its options show on the right. B goes back a level.
//

#pragma once

#include "console_card.hpp"
#include "cover_carousel.hpp"
#include "host_card.hpp"
#include "pressable_box.hpp"
#include "turntable.hpp"
#include <borealis.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class DownloadIndicator;

// A box drawn scaled, shifted and faded as a whole (zoom between shelves,
// the options panel sliding in). Touch areas do not follow the transform,
// so it is only used for short transitions and fully shown/hidden states.
class MotionBox : public brls::Box {
  public:
    using brls::Box::Box;

    void animate(float scale, float alpha, float shiftX, int ms,
                 std::function<void()> done = nullptr);
    void set(float scale, float alpha, float shiftX);
    float getMotionAlpha() { return alpha.getValue(); }

    void frame(brls::FrameContext* ctx) override;

    // Vertical point the scaling grows from, as a fraction of the height.
    float originY = 0.5f;

  private:
    brls::Animatable scale = 1.0f;
    brls::Animatable alpha = 1.0f;
    brls::Animatable shiftX = 0.0f;
};

enum class HomeCardKind { PCS, FAVORITES, SETTINGS, ABOUT };

// One card of the home shelf: an illustration, the name and a status line.
// A card with a 3D object (img/3d/home-<kind>.png) has no card around it:
// the object floats over the name and spins while it is selected.
class HomeCard : public PressableBox, public ShelfItem {
  public:
    explicit HomeCard(HomeCardKind kind);

    HomeCardKind getKind() const { return kind; }
    void setStatus(const std::string& text);
    bool frameless() const override { return turntable != nullptr; }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;
    void onFocusGained() override;
    void onFocusLost() override;

    std::function<void()> onEnter;

  private:
    HomeCardKind kind;
    brls::Label* status = nullptr;
    std::unique_ptr<Turntable> turntable;
};

// One entry of the options panel.
struct HomeOption {
    std::string icon; // Material Icons code point
    std::string label;
    std::string hint; // shown on the right, e.g. a count or a button
    std::function<void()> action;
    bool danger = false;
};

class HomeView : public brls::Box {
  public:
    HomeView();
    ~HomeView() override;

    void willAppear(bool resetState) override;
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;
    // Focus never leaves the screen's own shelves and panel by direction,
    // but for up from a shelf to the downloads indicator while it shows.
    brls::View* getNextFocus(brls::FocusDirection direction,
                             brls::View* currentView) override;
    brls::View* getDefaultFocus() override;

    static brls::View* create();

    // The home screen has no header; screens opened from it bring it back.
    static void presentWithHeader(brls::View* from, brls::View* view);

    // The shelf of a console's games, landing on selectRom (a file name)
    // when given.
    void openConsole(const emu::Console& console, const std::string& selectRom = "");
    // A on the "Add" card of the devices shelf: a PC, or a console on the
    // Switch.
    static void showAddChoices();

    // Opens a settings group ("video", "system", ...) over the home screen.
    static void openSettings(const std::string& group);
    // The downloads screen, over the home screen. Switch only.
    static void openDownloads();

    // PCs were renamed or removed: rebuild the PC shelf.
    static void hostsChanged();
    // A PC was paired from the add PC screen: back to the PC shelf, which
    // shows the new card.
    static void hostAdded();

  private:
    inline static HomeView* instance = nullptr;
    brls::AppletFrame* frame = nullptr;
    bool presenting = false;
    bool appeared = false;

    enum class Level { HOME, PCS };
    Level level = Level::HOME;
    bool entered = false;

    brls::Box* header = nullptr;
    // Says something downloads; empty while nothing does. Switch only.
    DownloadIndicator* downloadBadge = nullptr;
    MotionBox* homeLayer = nullptr;
    MotionBox* pcLayer = nullptr;
    MotionBox* panel = nullptr;

    struct Shelf {
        brls::Label* crumb = nullptr;
        CoverCarousel* carousel = nullptr;
        brls::Label* info = nullptr;
        brls::Label* detail = nullptr;
        CarouselDots* dots = nullptr;
    };
    Shelf home, pcs;
    std::vector<HomeCard*> homeCards;
    // The devices shelf holds the PCs, then the consoles, then "Add".
    std::vector<HostCard*> hostCards;
    std::vector<ConsoleCard*> consoleCards;

    // Panel contents.
    brls::Label* panelName = nullptr;
    brls::Box* panelDot = nullptr;
    brls::Label* panelStatus = nullptr;
    brls::Label* panelDetail = nullptr;
    brls::Box* panelOptions = nullptr;
    std::vector<std::vector<PressableBox*>> optionSlots; // rows of two
    brls::Animatable chromeFade = 1.0f; // header, crumbs and info lines

    MotionBox* makeLayer(Shelf& shelf, bool forPcs);
    void buildHomeCards();
    void buildHostCards();
    // selectIndex: card to land on, or -1 to stay on the current one.
    void rebuildHosts(int selectIndex = -1);
    void updateHomeCards();
    void updateShelfInfo();
    void updateBackAction();
    // B on the home shelf: asks before closing the app.
    void confirmExit();

    // checkUpdates: false on the way back from a game, so no manifest
    // request is in flight if the player starts another one right away;
    // the check runs on the next visit to Dispositivos.
    void openPcs(bool checkUpdates = true);
    void closePcs();
    void enter();
    void leave(bool focusCard = true);
    void fillPanel();
    void setPanelOptions(const std::vector<HomeOption>& options, int columns);
    HostCard* selectedHost();
    ConsoleCard* selectedConsole();

    std::vector<HomeOption> favoritesOptions();
    std::vector<HomeOption> settingsOptions();
    std::vector<HomeOption> aboutOptions();
    std::vector<HomeOption> hostOptions(HostCard* card);
    std::vector<HomeOption> consoleOptions(ConsoleCard* card);

    // Downloads and installs (or updates) the console's emulator, as a
    // DownloadService task the dialog shows; it goes on in the background
    // once the user leaves the dialog.
    void installConsole(const emu::Console& console);
    // background: the user left the dialog, so the shelf is not opened.
    void consoleInstalled(const emu::Console& console, bool first,
                          bool background);
    // Handles the emulator tasks' events for as long as the app runs.
    static void watchRunnerTasks();
    // Re-reads the installed version and the games, with the latest
    // manifest seen.
    void refreshConsole(const emu::Console& console);
    void removeConsole(const emu::Console& console);
    // At most once a day, asks whether the installed emulators have a newer
    // version. Silent when offline.
    void checkConsoleUpdates();
    // Started by the runner on its way back: opens the console's shelf on
    // the game that was played (see emu::setPendingResume).
    void resumeConsole();
};
