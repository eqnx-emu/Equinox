//
//  main_tabs_view.cpp
//  Moonlight
//
//  Created by XITRIX on 26.05.2021.
//

#include "main_tabs_view.hpp"
#include "Settings.hpp"
#include "about_tab.hpp"
#include "add_host_tab.hpp"
#include "hosts_tab.hpp"
#include "settings_tab.hpp"

MainTabs::MainTabs() {
    favoriteTab = new FavoriteTab();
    favoriteTab->ptrLock();

    MainTabs::instanse = this;
    refillTabs();
    lastHasAnyFavorites = Settings::instance().has_any_favorite();
}

void MainTabs::willAppear(bool resetState) {
    Box::willAppear(resetState);
    presenting = false;
    if (auto* applet = getAppletFrame()) {
        frame = applet;
        frame->setHeaderVisibility(brls::Visibility::GONE);
    }
    updateFavoritesIfNeeded();
    favoriteTab->refreshIfNeeded();
}

void MainTabs::updateFavoritesIfNeeded() {
    if (lastHasAnyFavorites != Settings::instance().has_any_favorite()) {
        refillTabs(false);
    }
}

void MainTabs::refillTabs(bool keepFocus) {
    int newFocus = 0;

    if (keepFocus) {
        for (int i = 0; i < this->sidebar->getItemsSize(); i++) {
            auto item = dynamic_cast<SidebarItem*>(this->sidebar->getItem(i));
            if (item && item->isActive())
                newFocus = i;
        }
    }

    clearTabs();

    addTab("tabs/hosts"_i18n, HostsTab::create);

    bool hasAnyFavorite = Settings::instance().has_any_favorite();
    if (hasAnyFavorite)
        addTab("tabs/favorites"_i18n, [this] { return this->favoriteTab; });
    lastHasAnyFavorites = hasAnyFavorite;
    addSeparator();

    addTab("tabs/settings"_i18n, SettingsTab::create);
    addSeparator();
    addTab("tabs/about"_i18n, AboutTab::create);
    sidebar->setContentOffsetY(-40, false);
    focusTab(newFocus);
}

void MainTabs::presentWithHeader(brls::View* from, brls::View* view) {
    brls::AppletFrame* applet = from->getAppletFrame();
    if (!applet)
        return;
    if (instanse && instanse->frame == applet)
        instanse->presenting = true;
    applet->setHeaderVisibility(brls::Visibility::VISIBLE);
    applet->pushContentView(view);
}

void MainTabs::onHostAdded() {
    if (presenting && frame) {
        frame->popContentView([this] { refillTabs(false); });
        return;
    }
    refillTabs();
}

View* MainTabs::create() { return new MainTabs(); }
