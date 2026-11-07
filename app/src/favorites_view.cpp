//
//  favorites_view.cpp
//  Equinox
//

#include "favorites_view.hpp"
#include "app_cell.hpp"
#include "bold_label.hpp"
#include "forwarder_maker.hpp"
#include "helper.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <string>

using namespace brls;
using namespace brls::literals;

FavoritesView::FavoritesView(int hostIndex) {
    setAxis(Axis::COLUMN);
    setJustifyContent(JustifyContent::CENTER);
    setAlignItems(AlignItems::STRETCH);
    setHideHighlight(true);
    getAppletFrameItem()->title = "home/favorites"_i18n;

    hosts = Settings::instance().hosts();
    for (size_t i = 0; i < hosts.size(); i++) {
        const bool any = std::any_of(
            hosts[i].favorites.begin(), hosts[i].favorites.end(),
            [](const App& app) { return !isHiddenApp(app.name); });
        if (any)
            filters.push_back(i);
    }
    if (hostIndex >= 0 &&
        std::find(filters.begin(), filters.end(), (size_t)hostIndex) !=
            filters.end())
        filter = hostIndex;

    chips = new Box(Axis::ROW);
    chips->setJustifyContent(JustifyContent::CENTER);
    chips->setAlignItems(AlignItems::CENTER);
    chips->setHeight(34);
    addView(chips);

    carousel = new CoverCarousel();
    carousel->setMarginTop(6);
    carousel->onSelect = [this](int index) { onSelected(index); };
    addView(carousel);

    title = new Label();
    title->setFontSize(26);
    title->setSingleLine(true);
    title->setHorizontalAlign(HorizontalAlign::CENTER);
    title->setMarginTop(12);
    addView(title);

    meta = new Label();
    meta->setFontSize(17);
    meta->setSingleLine(true);
    meta->setHorizontalAlign(HorizontalAlign::CENTER);
    meta->setTextColor(Application::getTheme()["brls/header/subtitle"]);
    meta->setMarginTop(4);
    addView(meta);

    dots = new CarouselDots();
    dots->setMarginTop(14);
    addView(dots);

    empty = new Label();
    empty->setFontSize(18);
    empty->setHorizontalAlign(HorizontalAlign::CENTER);
    empty->setTextColor(Application::getTheme()["brls/header/subtitle"]);
    empty->setText("favorites/empty"_i18n);
    addView(empty);

    // L and R change the PC; hidden from the footer with a single PC.
    registerAction("favorites/change_pc"_i18n, BUTTON_RB, [this](View*) {
        stepFilter(1);
        return true;
    }, filters.size() < 2);
    registerAction("", BUTTON_LB, [this](View*) {
        stepFilter(-1);
        return true;
    }, true);
    setActionAvailable(BUTTON_RB, filters.size() > 1);
    setActionAvailable(BUTTON_LB, filters.size() > 1);

    rebuild(0);
}

void FavoritesView::stepFilter(int direction) {
    if (filters.size() < 2)
        return;
    // Position -1 is "all", then each PC.
    int position = -1;
    for (size_t i = 0; i < filters.size(); i++)
        if ((int)filters[i] == filter)
            position = (int)i;
    const int count = (int)filters.size() + 1;
    position = ((position + 1 + direction) % count + count) % count - 1;
    setFilter(position < 0 ? -1 : (int)filters[position]);
}

void FavoritesView::setFilter(int hostIndex) {
    if (hostIndex == filter)
        return;
    filter = hostIndex;
    rebuild(0);
}

void FavoritesView::updateChips() {
    chips->clearViews();
    if (filters.size() < 2 && filter < 0) {
        chips->setVisibility(Visibility::INVISIBLE);
        return;
    }
    chips->setVisibility(Visibility::VISIBLE);

    Theme theme = Application::getTheme();
    auto addChip = [&](const std::string& text, int hostIndex) {
        const bool active = hostIndex == filter;
        auto* chip = new Box(Axis::ROW);
        chip->setPadding(6, 16, 6, 16);
        chip->setMarginLeft(5);
        chip->setMarginRight(5);
        chip->setCornerRadius(15);
        chip->setBackgroundColor(active ? theme.getColor("equinox/sun")
                                        : theme.getColor("equinox/card"));
        auto* label = new BoldLabel();
        label->setFontSize(15);
        label->setSingleLine(true);
        label->setTextColor(active ? theme.getColor("equinox/pill_text")
                                   : theme.getColor("equinox/muted"));
        label->setText(text);
        chip->addView(label);
        chip->addGestureRecognizer(new TapGestureRecognizer(
            [this, hostIndex](TapGestureStatus status, Sound* sound) {
                if (status.state != GestureState::END)
                    return;
                // Rebuilding deletes this chip: do it after the gesture.
                brls::sync([this, hostIndex] { setFilter(hostIndex); });
            }));
        chips->addView(chip);
    };

    auto* hint = new Label();
    hint->setFontSize(18);
    hint->setMarginRight(8);
    hint->setText(Hint::getKeyIcon(BUTTON_LB));
    chips->addView(hint);
    addChip("favorites/all"_i18n, -1);
    for (size_t index : filters)
        addChip(hosts[index].hostname, (int)index);
    hint = new Label();
    hint->setFontSize(18);
    hint->setMarginLeft(8);
    hint->setText(Hint::getKeyIcon(BUTTON_RB));
    chips->addView(hint);
}

void FavoritesView::rebuild(int selectIndex) {
    generation++;
    hosts = Settings::instance().hosts();

    // The cells are about to go: keep the focus on this view meanwhile.
    View* focus = Application::getCurrentFocus();
    bool focusHere = false;
    for (View* v = focus; v; v = v->hasParent() ? v->getParent() : nullptr)
        if (v == carousel)
            focusHere = true;
    setFocusable(true);
    if (focusHere)
        Application::giveFocus(this);

    carousel->clearViews();
    entries.clear();
    updateChips();

    for (size_t h = 0; h < hosts.size(); h++) {
        if (filter >= 0 && (int)h != filter)
            continue;
        const Host& host = hosts[h];
        for (const App& app : host.favorites) {
            if (isHiddenApp(app.name))
                continue;
            auto* cell = new AppCell(host, AppInfo{app.name, app.app_id}, 0);
            cell->setCaptionVisible(false);
            const int index = (int)entries.size();
            // A tap on a side cover brings it to the centre; a tap on the
            // centre one plays.
            cell->beforeLaunch = [this, index]() {
                if (carousel->selectedIndex() != index)
                    return false;
                return Application::getInputType() != InputType::TOUCH ||
                       getCPUTimeUsec() - carousel->selectedAt() > 350000;
            };
            cell->registerAction("app_list/unstar"_i18n, BUTTON_Y,
                                 [this, index](View*) {
                                     // Rebuilding deletes this cell: do it
                                     // after the action returns.
                                     brls::sync([this, index] { unstar(index); });
                                     return true;
                                 });
#if defined(PLATFORM_SWITCH)
            cell->registerAction(
                "forwarder/make"_i18n, BUTTON_X, [host, app](View* view) {
                    // Building and installing takes a few seconds: do it off
                    // the UI thread behind a spinner.
                    Dialog* loader = createLoadingDialog("forwarder/making"_i18n);
                    loader->open();
                    brls::async([host, app, loader]() {
                        const int rc = makeForwarder(host, app, false);
                        brls::sync([rc, loader]() {
                            loader->close([rc]() {
                                const std::string message =
                                    rc == EXIT_SUCCESS
                                        ? "forwarder/installed"_i18n
                                        : brls::getStr("forwarder/install_failed", rc);
                                auto dialog = new Dialog(message);
                                dialog->addButton("common/close"_i18n, []() {});
                                dialog->open();
                            });
                        });
                    });
                    return true;
                });
#endif
            entries.push_back({h, app});
            carousel->addView(cell);
        }
    }

    const bool none = entries.empty();
    setFocusable(none); // something must hold the focus on an empty screen
    carousel->setVisibility(none ? Visibility::GONE : Visibility::VISIBLE);
    title->setVisibility(none ? Visibility::GONE : Visibility::VISIBLE);
    meta->setVisibility(none ? Visibility::GONE : Visibility::VISIBLE);
    dots->setVisibility(none ? Visibility::GONE : Visibility::VISIBLE);
    empty->setVisibility(none ? Visibility::VISIBLE : Visibility::GONE);

    if (!none) {
        const int index = std::clamp(selectIndex, 0, (int)entries.size() - 1);
        carousel->jumpTo(index);
        onSelected(index);
        if (focusHere || focus == this)
            Application::giveFocus(carousel->getChildren()[index]);
    }

    // Each PC is asked once whether it is on; that connection also lets
    // the covers never downloaded be fetched.
    std::set<size_t> shown;
    for (const Entry& entry : entries)
        shown.insert(entry.host);
    for (size_t h : shown) {
        if (hostOnline.count(h))
            applyHostState(h);
        else
            checkHost(h);
    }
}

void FavoritesView::checkHost(size_t hostIndex) {
    if (hostChecking.count(hostIndex) || hostIndex >= hosts.size())
        return;
    hostChecking.insert(hostIndex);
    ASYNC_RETAIN
    GameStreamClient::instance().connect(
        hosts[hostIndex],
        [ASYNC_TOKEN, hostIndex](GSResult<SERVER_DATA> result) {
            ASYNC_RELEASE
            hostChecking.erase(hostIndex);
            hostOnline[hostIndex] = result.isSuccess();
            applyHostState(hostIndex);
        });
}

void FavoritesView::applyHostState(size_t hostIndex) {
    const auto it = hostOnline.find(hostIndex);
    if (it == hostOnline.end())
        return;
    const bool online = it->second;
    auto& cells = carousel->getChildren();
    for (size_t i = 0; i < entries.size() && i < cells.size(); i++) {
        if (entries[i].host != hostIndex)
            continue;
        auto* cell = static_cast<AppCell*>(cells[i]);
        cell->setOffline(!online);
        if (online && cell->needsCover())
            cell->fetchCover();
    }
    onSelected(carousel->selectedIndex());
}

void FavoritesView::onSelected(int index) {
    if (index < 0 || index >= (int)entries.size())
        return;
    title->setText(entries[index].app.name);
    const size_t host = entries[index].host;
    const auto state = hostOnline.find(host);
    const bool offline = state != hostOnline.end() && !state->second;
    meta->setText(offline ? hosts[host].hostname + "  ·  " +
                                "hosts/status_asleep"_i18n
                          : hosts[host].hostname);
    dots->setState(entries.size(), index);
}

void FavoritesView::unstar(int index) {
    if (index < 0 || index >= (int)entries.size())
        return;
    const Entry entry = entries[index];
    Settings::instance().remove_favorite(hosts[entry.host], entry.app.app_id);

    // The PC may have no favorites left: drop its chip.
    const auto host = Settings::instance().hosts();
    const bool any =
        entry.host < host.size() &&
        std::any_of(host[entry.host].favorites.begin(),
                    host[entry.host].favorites.end(),
                    [](const App& app) { return !isHiddenApp(app.name); });
    if (!any) {
        filters.erase(std::remove(filters.begin(), filters.end(), entry.host),
                      filters.end());
        if (filter == (int)entry.host)
            filter = -1;
        setActionAvailable(BUTTON_RB, filters.size() > 1);
        setActionAvailable(BUTTON_LB, filters.size() > 1);
    }
    rebuild(index);
}
