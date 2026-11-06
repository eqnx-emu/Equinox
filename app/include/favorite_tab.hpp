//
//  favorite_tab.hpp
//  Moonlight
//
//  Created by Даниил Виноградов on 24.09.2021.
//

#pragma once

#include "grid_view.hpp"
#include <Settings.hpp>
#include <borealis.hpp>

class FavoriteTab : public brls::Box {
  public:
    FavoriteTab();

    void refreshIfNeeded();
    void setRefreshNeeded() { isDirty = true; }
    static brls::View* create();

  private:
    BRLS_BIND(brls::Box, container, "container");

    bool isDirty = false;
    // Bumped on each rebuild, so pending requests know their views are gone.
    uint64_t buildGeneration = 0;

    void updateAppList();
    void rebuildAfterUnstar(int section, int index);
};
