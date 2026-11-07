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
#include <memory>

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
    // Dim the cover and say the PC is off.
    void setOffline(bool offline);
    // Hide the name bar on the cover (the library shows the name below).
    void setCaptionVisible(bool visible);

    // Asked before starting the stream; return false to only select.
    std::function<bool()> beforeLaunch;

    // True when the cover still has to be downloaded but the PC wasn't
    // connected yet; call fetchCover() once it is.
    bool needsCover() const { return coverPending; }
    void fetchCover();

  private:
    Host host;
    int appId;
    bool coverPending = false;
    Box* offlineLayer = nullptr;

    // Covers are decoded off the UI thread; only the upload runs on it.
    void loadCover();
    void applyCover(const std::string& path,
                    std::shared_ptr<unsigned char> pixels, int width,
                    int height);
    bool useCachedCover(const std::string& path);
    void setCoverTexture(int texture);

    void updateFavoriteAction(Host host, AppInfo app);
};
