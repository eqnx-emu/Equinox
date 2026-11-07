//
//  favorites_view.hpp
//  Equinox
//
//  Favorite games as a cover shelf, like a PC's library, from every PC or
//  from one. L and R (or the chips on top) change the PC.
//

#pragma once

#include "cover_carousel.hpp"
#include <Settings.hpp>
#include <borealis.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <vector>

class FavoritesView : public brls::Box {
  public:
    // hostIndex: index in Settings::hosts() to show, or -1 for all PCs.
    explicit FavoritesView(int hostIndex = -1);

  private:
    struct Entry {
        size_t host;
        App app;
    };

    std::vector<Host> hosts;
    // Hosts with favorites, in order: the filters after "all".
    std::vector<size_t> filters;
    int filter = -1; // index in hosts, or -1
    std::vector<Entry> entries;
    // Bumped on each rebuild, so pending requests know their cells are gone.
    uint64_t generation = 0;
    // Whether each PC (index in hosts) is on, once known. Asked once per
    // PC while the screen is open.
    std::map<size_t, bool> hostOnline;
    std::set<size_t> hostChecking;

    brls::Box* chips = nullptr;
    CoverCarousel* carousel = nullptr;
    brls::Label* title = nullptr;
    brls::Label* meta = nullptr;
    brls::Label* empty = nullptr;
    CarouselDots* dots = nullptr;

    void rebuild(int selectIndex);
    void stepFilter(int direction);
    void setFilter(int hostIndex);
    void updateChips();
    void onSelected(int index);
    void unstar(int index);
    void checkHost(size_t hostIndex);
    // Shows the PC's state on its covers and fetches the covers it lacks.
    void applyHostState(size_t hostIndex);
};
