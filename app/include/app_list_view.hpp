//
//  app_list_view.hpp
//  Moonlight
//
//  Created by Даниил Виноградов on 26.05.2021.
//

#pragma once

#include "app_cell.hpp"
#include "cover_ambient.hpp"
#include "cover_carousel.hpp"
#include "loading_overlay.hpp"
#include <Settings.hpp>
#include <borealis.hpp>
#include "GameStreamClient.hpp"

#include <map>
#include <optional>
#include <vector>

using namespace brls;

class AppListView : public Box {
  public:
    AppListView(const Host& host);
    ~AppListView() override;

    void onLayout() override;
    void willAppear(bool resetState) override;
    void willDisappear(bool resetState) override;
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              Style style, FrameContext* ctx) override;

  private:
    Host host;
    View* hintView = nullptr;
    std::optional<AppInfo> currentApp;
    bool loading = false;
    bool inputBlocked = false;
    LoadingOverlay* loader = nullptr;
    void blockInput(bool block);

    CoverCarousel* carousel;
    Label* titleLabel;
    Label* metaLabel;
    CarouselDots* dots;
    std::vector<AppInfo> apps;
    int currentGameId = 0;
    // Cover to land on again after the list reloads (e.g. after a stream).
    int restoreAppId = -1;
    void showLoadError(const std::string& error);
    BRLS_BIND(Box, container, "container");

    // Colours taken from the focused game's cover, glowing behind the shelf.
    CoverAmbient ambient;
    Animatable detailsFade = 1.0f;
    void onAppSelected(int index);

    void setCurrentApp(const AppInfo& app);
    void terninateApp();
    void updateAppList();
    void updateFavoriteAction(AppCell* cell, Host host, const AppInfo& app);
};
