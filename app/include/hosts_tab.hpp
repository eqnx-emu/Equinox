//
//  hosts_tab.hpp
//  Equinox
//
//  Home screen: one card per paired PC.
//

#pragma once

#include "grid_view.hpp"
#include "host_card.hpp"
#include <borealis.hpp>
#include <vector>

class HostsTab : public brls::Box {
  public:
    HostsTab();

    // Re-queries every host when the screen comes back (for example after
    // leaving a stream), so the running game and state stay current.
    void willAppear(bool resetState) override;

    static brls::View* create();

    BRLS_BIND(brls::Box, container, "container");
    BRLS_BIND(brls::Label, title, "title");
    BRLS_BIND(brls::Label, subtitle, "subtitle");

  private:
    std::vector<HostCard*> cards;
    bool appeared = false;
};
