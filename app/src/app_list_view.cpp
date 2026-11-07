//
//  app_list_view.cpp
//  Moonlight
//
//  Created by XITRIX on 26.05.2021.
//

#include "app_list_view.hpp"
#include "BoxArtManager.hpp"
#include "helper.hpp"

AppListView::AppListView(const Host& host) : host(host) {
    this->inflateFromXMLRes("xml/views/app_list_view.xml");

    auto* label = new brls::Label();
    label->setText(brls::Hint::getKeyIcon(ControllerButton::BUTTON_BACK) +
                   "  " + "app_list/terminate_current_app"_i18n);
    label->setFontSize(24);
    label->setMargins(4, 16, 4, 16);

    Box* holder = new Box();
    holder->addView(label);
    holder->setFocusable(true);
    holder->setCornerRadius(6);
    holder->setMargins(18, 0, 8, 0);
    holder->addGestureRecognizer(new TapGestureRecognizer(holder));

    hintView = holder;
    getAppletFrameItem()->setHintView(hintView);

    container->setHideHighlight(true);
    carousel = new CoverCarousel();
    carousel->onSelect = [this](int index) { onAppSelected(index); };
    container->addView(carousel);

    titleLabel = new Label();
    titleLabel->setFontSize(26);
    titleLabel->setHorizontalAlign(HorizontalAlign::CENTER);
    titleLabel->setSingleLine(true);
    titleLabel->setMarginTop(18);
    container->addView(titleLabel);

    metaLabel = new Label();
    metaLabel->setFontSize(17);
    metaLabel->setHorizontalAlign(HorizontalAlign::CENTER);
    metaLabel->setTextColor(Application::getTheme()["brls/header/subtitle"]);
    metaLabel->setMarginTop(6);
    container->addView(metaLabel);

    dots = new CarouselDots();
    dots->setMarginTop(18);
    container->addView(dots);

    loader = new LoadingOverlay(this);

    auto closeCurrentAction = [this, host](View* view) {
        if (currentApp.has_value()) {
            this->terninateApp();
        }
        return true;
    };

    hintView->registerClickAction(closeCurrentAction);
    hintView->registerAction("", brls::ControllerButton::BUTTON_BACK,
                             closeCurrentAction, true);
    registerAction("", brls::ControllerButton::BUTTON_BACK, closeCurrentAction,
                   true);

    // #ifdef __SWITCH__
    //     registerAction("", brls::ControllerButton::BUTTON_LB, [](View* view)
    //     {
    //         appletPerformSystemButtonPressingIfInFocus(AppletSystemButtonType_HomeButtonLongPressing);
    //         return true;
    //     }, true);
    // #endif

    registerAction("app_list/reload_app_list"_i18n, BUTTON_X,
                   [this](View* view) {
                       this->updateAppList();
                       return true;
                   });
    blockInput(true);
}

void AppListView::blockInput(bool block) {
    if (block && !inputBlocked) {
        inputBlocked = block;
        Application::blockInputs();
    } else if (!block && inputBlocked) {
        inputBlocked = block;
        Application::unblockInputs();
    }
}

void AppListView::terninateApp() {
    if (loading)
        return;

    auto* dialog =
        new Dialog("app_list/terminate_prefix"_i18n + currentApp->name +
                   "app_list/terminate_postfix"_i18n);

    dialog->addButton("common/cancel"_i18n, [] {});

    dialog->addButton("app_list/terminate"_i18n, [this] {
        if (loading)
            return;

        loading = true;
        carousel->clearViews();
        apps.clear();
        Application::giveFocus(this);
        loader->setHidden(false);
        blockInput(true);

        ASYNC_RETAIN
        GameStreamClient::instance().quit(
            host, [ASYNC_TOKEN](const GSResult<bool>& result) {
                ASYNC_RELEASE

                loading = false;
                loader->setHidden(true);

                if (!result.isSuccess())
                    showError(result.error(), [this] {});

                updateAppList();
            });
    });

    dialog->open();
}

void AppListView::updateAppList() {
    if (loading)
        return;

    loading = true;

    const int sel = carousel->selectedIndex();
    if (sel >= 0 && sel < (int)apps.size())
        restoreAppId = apps[sel].app_id;

    carousel->clearViews();
    apps.clear();
    titleLabel->setText("");
    metaLabel->setText("");
    dots->setState(0, 0);
    Application::giveFocus(this);
    loader->setHidden(false);
    currentApp = std::nullopt;
    hintView->setVisibility(Visibility::GONE);
    blockInput(true);

    getAppletFrameItem()->title = host.hostname;
    updateAppletFrameItem();

    ASYNC_RETAIN
    GameStreamClient::instance().connect(
        host, [ASYNC_TOKEN](const GSResult<SERVER_DATA>& result) {
            ASYNC_RELEASE

            if (result.isSuccess()) {
                int currentGame = result.value().currentGame;

                ASYNC_RETAIN
                GameStreamClient::instance().applist(
                    host,
                    [ASYNC_TOKEN, currentGame](const GSResult<AppInfoList>& result) {
                        ASYNC_RELEASE

                        loading = false;
                        loader->setHidden(true);
                        blockInput(false);

                        if (result.isSuccess()) {
                            AppInfoList sortedApps;
                            for (const AppInfo& app : result.value())
                                if (!isHiddenApp(app.name))
                                    sortedApps.push_back(app);
                            std::sort(
                                sortedApps.begin(), sortedApps.end(),
                                [this, currentGame](const AppInfo& l, const AppInfo& r) {
                                    int lScore = 0;
                                    int rScore = 0;

                                    if (l.app_id == currentGame) lScore+=2;
                                    if (Settings::instance().is_favorite(this->host, l.app_id)) lScore+=1;

                                    if (r.app_id == currentGame) rScore+=2;
                                    if (Settings::instance().is_favorite(this->host, r.app_id)) rScore+=1;

                                    return lScore > rScore;
                                });

                            currentGameId = currentGame;
                            for (const AppInfo& app : sortedApps) {
                                if (app.app_id == currentGame)
                                    setCurrentApp(app);

                                auto* cell =
                                    new AppCell(host, app, currentGame);
                                cell->setFavorite(
                                    Settings::instance().is_favorite(
                                        host, app.app_id));
                                cell->setCaptionVisible(false);
                                const int index = (int)apps.size();
                                // A tap on a side cover brings it to the
                                // centre; a tap on the centre one plays.
                                cell->beforeLaunch = [this, index]() {
                                    if (carousel->selectedIndex() != index)
                                        return false;
                                    return Application::getInputType() !=
                                               InputType::TOUCH ||
                                           getCPUTimeUsec() -
                                                   carousel->selectedAt() >
                                               350000;
                                };
                                apps.push_back(app);
                                carousel->addView(cell);
                                this->updateFavoriteAction(cell, host, app);
                            }

                            // Back on the cover that was selected before.
                            const auto kept = std::find_if(
                                apps.begin(), apps.end(),
                                [this](const AppInfo& a) {
                                    return a.app_id == restoreAppId;
                                });
                            if (kept != apps.end()) {
                                const int index = (int)(kept - apps.begin());
                                carousel->jumpTo(index);
                                Application::giveFocus(
                                    carousel->getChildren()[index]);
                            } else {
                                Application::giveFocus(this);
                            }
                        } else {
                            showLoadError(result.error());
                        }
                    });
            } else {
                loading = false;
                loader->setHidden(true);
                blockInput(false);
                showLoadError(result.error());
            }
        });
}

void AppListView::showLoadError(const std::string& error) {
    // Offer another try: a PC that is still waking up often answers on the
    // second attempt.
    auto* dialog = new Dialog(error);
    dialog->addButton("hints/back"_i18n, [this] { this->dismiss(); });
    dialog->addButton("app_list/retry"_i18n, [this] { updateAppList(); });
    dialog->setCancelable(false);
    dialog->open();
}

void AppListView::setCurrentApp(const AppInfo& app) {
    this->currentApp = app;
    hintView->setVisibility(Visibility::VISIBLE);
    getAppletFrameItem()->title =
        host.hostname + " - " + "app_list/running"_i18n + " " + app.name;
    updateAppletFrameItem();
}

AppListView::~AppListView() = default;

void AppListView::willDisappear(bool resetState) {
    Box::willDisappear(resetState);
    ambient.detach();
}

void AppListView::willAppear(bool resetState) {
    Box::willAppear(resetState);
    ambient.attach(this);
    updateAppList();
}

void AppListView::onLayout() {
    Box::onLayout();

    if (loader)
        loader->layout();
}

void AppListView::onAppSelected(int index) {
    if (index < 0 || index >= (int)apps.size())
        return;
    const AppInfo& app = apps[index];

    titleLabel->setText(app.name);
    std::string meta;
    if (app.app_id == currentGameId)
        meta = "app_list/running_now"_i18n;
    if (Settings::instance().is_favorite(host, app.app_id))
        meta += (meta.empty() ? "" : "  ·  ") + "app_list/favorite"_i18n;
    if (meta.empty())
        meta = currentGameId != 0 ? "app_list/another_running"_i18n
                                  : "app_list/ready"_i18n;
    metaLabel->setText(meta);
    detailsFade.reset(0.0f);
    detailsFade.addStep(1.0f, 260, EasingFunction::quadraticOut);
    detailsFade.start();
    dots->setState(apps.size(), index);

    ambient.select(host, app.app_id);
}

void AppListView::draw(NVGcontext* vg, float x, float y, float width,
                       float height, Style style, FrameContext* ctx) {
    // Without a BackdropFrame the glow stays inside the content area.
    ambient.drawFallback(vg, x, y, width, height);

    const float details = detailsFade.getValue();
    titleLabel->setAlpha(details);
    metaLabel->setAlpha(details);
    titleLabel->setTranslationY(6 * (1 - details));
    metaLabel->setTranslationY(6 * (1 - details));

    Box::draw(vg, x, y, width, height, style, ctx);
}

void AppListView::updateFavoriteAction(AppCell* cell, Host host, const AppInfo& app) {
    bool isFavorite = Settings::instance().is_favorite(host, app.app_id);
    cell->registerAction(
        isFavorite ? "app_list/unstar"_i18n : "app_list/star"_i18n, BUTTON_Y,
        [this, cell, host, app](View* view) {
            bool isFavorite =
                Settings::instance().is_favorite(host, app.app_id);
            cell->setFavorite(!isFavorite);
            if (isFavorite) {
                Settings::instance().remove_favorite(host, app.app_id);
            } else {
                App thisApp{app.name, app.app_id};
                Settings::instance().add_favorite(host, thisApp);
            }
            this->updateFavoriteAction(cell, host, app);
            return true;
        });
    Application::getGlobalHintsUpdateEvent()->fire();
}
