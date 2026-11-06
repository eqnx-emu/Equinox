//
//  favorite_tab.cpp
//  Moonlight
//
//  Created by Даниил Виноградов on 24.09.2021.
//

#include "favorite_tab.hpp"
#include "app_cell.hpp"
#include "main_tabs_view.hpp"
#include "forwarder_maker.hpp"
#include "helper.hpp"
#include <algorithm>
#include <cstdlib>
#include <string>

using namespace brls;
using namespace brls::literals;

FavoriteTab::FavoriteTab() {
    this->inflateFromXMLRes("xml/tabs/favorites.xml");

    container->setHideHighlight(true);

    updateAppList();
}

void FavoriteTab::refreshIfNeeded() {
    if (isDirty) {
        isDirty = false;
        updateAppList();
    }
}

void FavoriteTab::updateAppList() {
    container->clearViews();
    std::vector<Host> hosts = Settings::instance().hosts();
    for (const Host& host : hosts) {
        if (host.favorites.empty())
            continue;

        auto* header = new Header();
        header->setTitle(host.hostname);
        header->setLineBottom(0);

        if (!container->getChildren().empty())
            header->setMarginTop(40);

        container->addView(header);

        auto* gridView = new GridView(5);

        for (const App& app : host.favorites) {
            AppInfo info{app.name, app.app_id};
            auto* cell = new AppCell(host, info, 0);
            gridView->addView(cell);

#if defined(PLATFORM_SWITCH)
            cell->registerAction(
                "forwarder/make"_i18n, BUTTON_X,
                [host, app](View* view) {
                    // Building and installing takes a few seconds: do it off
                    // the UI thread behind a spinner.
                    Dialog* loader =
                        createLoadingDialog("forwarder/making"_i18n);
                    loader->open();
                    brls::async([host, app, loader]() {
                        const int rc = makeForwarder(host, app, false);
                        brls::sync([rc, loader]() {
                            loader->close([rc]() {
                                const std::string message =
                                    rc == EXIT_SUCCESS
                                        ? "forwarder/installed"_i18n
                                        : brls::getStr(
                                              "forwarder/install_failed", rc);
                                auto dialog = new Dialog(message);
                                dialog->addButton("common/close"_i18n, []() {});
                                dialog->open();
                            });
                        });
                    });
                    return true;
            });
#endif

            cell->registerAction(
                "app_list/unstar"_i18n, BUTTON_Y,
                [this, gridView, cell, host, app](View* view) {
                    const int index = gridView->getItemIndex(cell);
                    const int section =
                        *((int*)gridView->getParentUserData());

                    Settings::instance().remove_favorite(host, app.app_id);

                    // Rebuilding deletes this cell and this action, so it
                    // runs after the action has returned.
                    ASYNC_RETAIN
                    brls::sync([ASYNC_TOKEN, section, index]() {
                        ASYNC_RELEASE
                        rebuildAfterUnstar(section, index);
                    });
                    return true;
                });
        }

        container->addView(gridView);

        // Covers never downloaded need the PC's connection first; connect
        // once for the whole group instead of once per cover.
        const auto& cells = gridView->getChildren();
        const bool pending = std::any_of(cells.begin(), cells.end(), [](View* v) {
            return ((AppCell*)v)->needsCover();
        });
        if (pending) {
            ASYNC_RETAIN
            GameStreamClient::instance().connect(
                host, [ASYNC_TOKEN, gridView](GSResult<SERVER_DATA> result) {
                    ASYNC_RELEASE
                    // The list may have been rebuilt since.
                    const auto& children = container->getChildren();
                    if (!result.isSuccess() ||
                        std::find(children.begin(), children.end(), gridView) ==
                            children.end())
                        return;
                    for (View* v : gridView->getChildren())
                        if (((AppCell*)v)->needsCover())
                            ((AppCell*)v)->fetchCover();
                });
        }
    }
}

void FavoriteTab::rebuildAfterUnstar(int section, int index) {
    updateAppList();

    if (section >= (int)container->getChildren().size())
        section = (int)container->getChildren().size() - 2;

    if (section <= 0) {
        MainTabs::getInstanse()->updateFavoritesIfNeeded();
        return;
    }

    auto& items =
        ((GridView*)container->getChildren()[section])->getChildren();
    const int newIndex = std::min(index, (int)items.size() - 1);
    if (newIndex < 0)
        return;

    Application::giveFocus(items[newIndex]);
}

brls::View* FavoriteTab::create() { return new FavoriteTab(); }
