//
//  app_cell.hpp
//  Moonlight
//
//  Created by Даниил Виноградов on 03.06.2021.
//

#pragma once

#include <borealis.hpp>
#include "GameStreamClient.hpp"

#include <functional>

using namespace brls;

class AppCell : public Box {
  public:
    AppCell(const Host& host, const AppInfo& app, int currentApp);

    BRLS_BIND(Image, image, "image");
    BRLS_BIND(Label, title, "title");
    BRLS_BIND(Image, currentAppImage, "current_app_image");
    BRLS_BIND(Image, favoriteAppImage, "favorite_app_image");
    BRLS_BIND(Rectangle, unactiveLayer, "unactive_layer");
    BRLS_BIND(Box, caption, "caption");

    void setFavorite(bool favorite);
    // Hide the name bar on the cover (the library shows the name below).
    void setCaptionVisible(bool visible);

    // Asked before starting the stream; return false to only select.
    std::function<bool()> beforeLaunch;

  private:
    void updateFavoriteAction(Host host, AppInfo app);
};
