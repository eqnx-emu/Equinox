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

#include "cover_carousel.hpp"
#include "host_card.hpp"
#include "pressable_box.hpp"
#include <borealis.hpp>

#include <functional>
#include <string>
#include <vector>

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
class HomeCard : public PressableBox {
  public:
    explicit HomeCard(HomeCardKind kind);

    HomeCardKind getKind() const { return kind; }
    void setStatus(const std::string& text);

    std::function<void()> onEnter;

  private:
    HomeCardKind kind;
    brls::Label* status = nullptr;
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
    // Focus never leaves the screen's own shelves and panel by direction.
    brls::View* getNextFocus(brls::FocusDirection direction,
                             brls::View* currentView) override;
    brls::View* getDefaultFocus() override;

    static brls::View* create();

    // The home screen has no header; screens opened from it bring it back.
    static void presentWithHeader(brls::View* from, brls::View* view);

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
    std::vector<HostCard*> hostCards;

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

    void openPcs();
    void closePcs();
    void enter();
    void leave(bool focusCard = true);
    void fillPanel();
    void setPanelOptions(const std::vector<HomeOption>& options, int columns);
    HostCard* selectedHost();

    std::vector<HomeOption> favoritesOptions();
    std::vector<HomeOption> settingsOptions();
    std::vector<HomeOption> aboutOptions();
    std::vector<HomeOption> hostOptions(HostCard* card);
};
