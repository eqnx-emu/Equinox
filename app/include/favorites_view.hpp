//
//  favorites_view.hpp
//  Equinox
//
//  Favorite games as a cover shelf, like a PC's library, from every PC or
//  from one. L and R (or the chips on top) change the PC. On the Switch,
//  console games marked as favorite come after the PC ones, with a chip per
//  console after the PC chips.
//

#pragma once

#include "cover_ambient.hpp"
#include "cover_carousel.hpp"
#include "emu/Console.hpp"
#include "emu/LaunchLine.hpp"
#include "emu/RomScan.hpp"
#include <Settings.hpp>
#include <borealis.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <variant>
#include <vector>

class FavoritesView : public brls::Box {
  public:
    // hostIndex: index in Settings::hosts() to show, or -1 for all PCs.
    // console: a console slug to show only that console's games instead.
    explicit FavoritesView(int hostIndex = -1, const std::string& console = "");
    // Back from a game opened here: the same chip, when it still is the
    // game's console, and the same game.
    explicit FavoritesView(const emu::ResumeToken& token);
    ~FavoritesView() override;

    void willAppear(bool resetState) override;
    void willDisappear(bool resetState) override;
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

  private:
    struct PcEntry {
        size_t host;
        App app;
    };
    struct LocalEntry {
        LocalFavorite favorite;
        const emu::Console* console = nullptr;
        emu::RomEntry rom;
        bool missing = false; // the ROM is no longer on the SD card
        bool hasCover = false;
    };
    using Entry = std::variant<PcEntry, LocalEntry>;

    std::vector<Host> hosts;
    // Hosts with favorites, in order: the PC chips after "all".
    std::vector<size_t> filters;
    // Consoles with favorite games, in emu::consoles() order: their chips
    // come after the PC chips. Always empty off the Switch.
    std::vector<std::string> consoleFilters;
    int filter = -1;           // index in hosts, or -1
    std::string consoleFilter; // a console slug, or empty
    std::vector<Entry> entries;
    // Bumped on each rebuild, so pending requests know their cells are gone.
    uint64_t generation = 0;
    // Whether each PC (index in hosts) is on, once known. Asked once per
    // PC while the screen is open.
    std::map<size_t, bool> hostOnline;
    std::set<size_t> hostChecking;
    bool launching = false;
    // Game to land on in the next rebuild (back from playing it).
    std::string selectConsole, selectRom;
    // Cleared on destruction; checked before a launch that waited.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    brls::Box* chips = nullptr;
    CoverCarousel* carousel = nullptr;
    brls::Label* title = nullptr;
    brls::Label* meta = nullptr;
    brls::Label* empty = nullptr;
    CarouselDots* dots = nullptr;
    // Glow in the colours of the focused cover, as in a PC's library.
    CoverAmbient ambient;

    void setup(int hostIndex, const std::string& console);
    void rebuild(int selectIndex);
    // PCs plus consoles with favorites: chips and L/R need two or more.
    size_t sources() const { return filters.size() + consoleFilters.size(); }
    // Chip positions: 0 is "all", then filters, then consoleFilters.
    int chipPosition() const;
    void setChip(int position);
    void stepFilter(int direction);
    void setFilter(int hostIndex, const std::string& console);
    void updateChips();
    void updateSourceActions();
    void onSelected(int index);
    void unstar(int index);
    void play(int index);
    void checkHost(size_t hostIndex);
    // Shows the PC's state on its covers and fetches the covers it lacks.
    void applyHostState(size_t hostIndex);
};
