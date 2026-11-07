//
//  favorites_view.cpp
//  Equinox
//

#include "favorites_view.hpp"
#include "app_cell.hpp"
#include "bold_label.hpp"
#include "forwarder_maker.hpp"
#include "helper.hpp"

#if defined(PLATFORM_SWITCH)
#include "console_shelf_view.hpp"
#include "emu/CoverName.hpp"
#include "emu/PathUtf8.hpp"
#include "rom_cell.hpp"

#include <filesystem>
#include <system_error>
#endif

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <string>

using namespace brls;
using namespace brls::literals;

namespace {
#if defined(PLATFORM_SWITCH)
bool fileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(emu::fromUtf8(path), ec);
}

bool hasLocalFavorites(const std::string& console) {
    const auto& favorites = Settings::instance().local_favorites();
    return std::any_of(favorites.begin(), favorites.end(),
                       [&](const LocalFavorite& f) { return f.console == console; });
}
#endif
} // namespace

FavoritesView::FavoritesView(int hostIndex, const std::string& console) {
    setup(hostIndex, console);
    rebuild(0);
}

FavoritesView::FavoritesView(const emu::ResumeToken& token) {
    setup(-1, "");
    // token.filter is the chip position when the game was opened. Only
    // "all" or the game's console chip can have shown it, so that position
    // is kept only when it is still this console's chip; anything else
    // (favorites changed meanwhile, a bad token) shows all.
    const auto it = std::find(consoleFilters.begin(), consoleFilters.end(),
                              token.console);
    if (it != consoleFilters.end() &&
        token.filter ==
            1 + (int)filters.size() + (int)(it - consoleFilters.begin()))
        consoleFilter = token.console;
    updateSourceActions();

    // The game itself, or the first one when it is no longer a favorite.
    selectConsole = token.console;
    selectRom = token.romFile;
    rebuild(0);
}

FavoritesView::~FavoritesView() { *alive = false; }

void FavoritesView::setup(int hostIndex, const std::string& console) {
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

#if defined(PLATFORM_SWITCH)
    for (const emu::Console& c : emu::consoles())
        if (hasLocalFavorites(c.slug))
            consoleFilters.push_back(c.slug);
    if (filter < 0 && !console.empty() &&
        std::find(consoleFilters.begin(), consoleFilters.end(), console) !=
            consoleFilters.end())
        consoleFilter = console;
#else
    (void)console;
#endif

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

    // L and R change the PC (or console); hidden from the footer with a
    // single source. The PC-only wording stays when there is no console.
    registerAction(consoleFilters.empty() ? "favorites/change_pc"_i18n
                                          : "favorites/change_filter"_i18n,
                   BUTTON_RB, [this](View*) {
        stepFilter(1);
        return true;
    }, sources() < 2);
    registerAction("", BUTTON_LB, [this](View*) {
        stepFilter(-1);
        return true;
    }, true);
    updateSourceActions();
}

void FavoritesView::updateSourceActions() {
    setActionAvailable(BUTTON_RB, sources() > 1);
    setActionAvailable(BUTTON_LB, sources() > 1);
}

int FavoritesView::chipPosition() const {
    if (!consoleFilter.empty()) {
        const auto it = std::find(consoleFilters.begin(), consoleFilters.end(),
                                  consoleFilter);
        if (it != consoleFilters.end())
            return 1 + (int)filters.size() + (int)(it - consoleFilters.begin());
        return 0;
    }
    for (size_t i = 0; i < filters.size(); i++)
        if ((int)filters[i] == filter)
            return 1 + (int)i;
    return 0;
}

void FavoritesView::setChip(int position) {
    const int pcs = (int)filters.size();
    if (position >= 1 && position <= pcs)
        setFilter((int)filters[position - 1], "");
    else if (position > pcs && position - pcs - 1 < (int)consoleFilters.size())
        setFilter(-1, consoleFilters[position - pcs - 1]);
    else
        setFilter(-1, "");
}

void FavoritesView::stepFilter(int direction) {
    if (sources() < 2)
        return;
    const int count = (int)sources() + 1;
    setChip(((chipPosition() + direction) % count + count) % count);
}

void FavoritesView::setFilter(int hostIndex, const std::string& console) {
    if (hostIndex == filter && console == consoleFilter)
        return;
    filter = hostIndex;
    consoleFilter = console;
    rebuild(0);
}

void FavoritesView::updateChips() {
    chips->clearViews();
    if (sources() < 2 && chipPosition() == 0) {
        chips->setVisibility(Visibility::INVISIBLE);
        return;
    }
    chips->setVisibility(Visibility::VISIBLE);

    Theme theme = Application::getTheme();
    auto addChip = [&](const std::string& text, int hostIndex,
                       const std::string& console) {
        const bool active = hostIndex == filter && console == consoleFilter;
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
            [this, hostIndex, console](TapGestureStatus status, Sound* sound) {
                if (status.state != GestureState::END)
                    return;
                // Rebuilding deletes this chip: do it after the gesture.
                std::shared_ptr<bool> token = alive;
                brls::sync([this, token, hostIndex, console] {
                    if (*token)
                        setFilter(hostIndex, console);
                });
            }));
        chips->addView(chip);
    };

    auto* hint = new Label();
    hint->setFontSize(18);
    hint->setMarginRight(8);
    hint->setText(Hint::getKeyIcon(BUTTON_LB));
    chips->addView(hint);
    // Same order as chipPosition(): all, the PCs, then the consoles.
    addChip("favorites/all"_i18n, -1, "");
    for (size_t index : filters)
        addChip(hosts[index].hostname, (int)index, "");
#if defined(PLATFORM_SWITCH)
    for (const std::string& slug : consoleFilters)
        if (const emu::Console* console = emu::findConsole(slug))
            addChip(console->name, -1, slug);
#endif
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

    // PC favorites first, in the same order as always.
    for (size_t h = 0; h < hosts.size() && consoleFilter.empty(); h++) {
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
                                     std::shared_ptr<bool> token = alive;
                                     brls::sync([this, token, index] {
                                         if (*token)
                                             unstar(index);
                                     });
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
            entries.push_back(PcEntry{h, app});
            carousel->addView(cell);
        }
    }

#if defined(PLATFORM_SWITCH)
    // Then the console games, under "all" or their console's chip. Only the
    // covers already on the SD card show: the shelf downloads them.
    for (const LocalFavorite& favorite : Settings::instance().local_favorites()) {
        if (filter >= 0 ||
            (!consoleFilter.empty() && favorite.console != consoleFilter))
            continue;
        const emu::Console* console = emu::findConsole(favorite.console);
        if (!console)
            continue;
        const emu::ConsolePaths paths = emu::pathsFor(*console);
        LocalEntry entry;
        entry.favorite = favorite;
        entry.console = console;
        entry.rom.path = paths.roms + "/" + favorite.romFile;
        entry.rom.fileName = favorite.romFile;
        entry.rom.title = favorite.title;
        entry.missing = !fileExists(entry.rom.path);
        const std::string cover = emu::coverPath(paths, favorite.title);
        entry.hasCover = fileExists(cover);

        auto* cell = new RomCell(*console, entry.rom);
        cell->setFavorite(true);
        cell->setMissing(entry.missing);
        if (entry.hasCover)
            cell->loadCover(cover);
        const int index = (int)entries.size();
        cell->beforeLaunch = [this, index]() {
            if (carousel->selectedIndex() != index)
                return false;
            return Application::getInputType() != InputType::TOUCH ||
                   getCPUTimeUsec() - carousel->selectedAt() > 350000;
        };
        // Launching closes the app: do it once the press is handled.
        cell->onPlay = [this, index] {
            std::shared_ptr<bool> token = alive;
            brls::sync([this, token, index] {
                if (*token)
                    play(index);
            });
        };
        cell->registerAction("app_list/unstar"_i18n, BUTTON_Y,
                             [this, index](View*) {
                                 std::shared_ptr<bool> token = alive;
                                 brls::sync([this, token, index] {
                                     if (*token)
                                         unstar(index);
                                 });
                                 return true;
                             });
        entries.push_back(std::move(entry));
        carousel->addView(cell);
    }
#endif

    if (!selectRom.empty()) {
        for (size_t i = 0; i < entries.size(); i++) {
            const auto* local = std::get_if<LocalEntry>(&entries[i]);
            if (local && local->favorite.console == selectConsole &&
                local->favorite.romFile == selectRom)
                selectIndex = (int)i;
        }
        selectConsole.clear();
        selectRom.clear();
    }

    const bool none = entries.empty();
    setFocusable(none); // something must hold the focus on an empty screen
    carousel->setVisibility(none ? Visibility::GONE : Visibility::VISIBLE);
    title->setVisibility(none ? Visibility::GONE : Visibility::VISIBLE);
    meta->setVisibility(none ? Visibility::GONE : Visibility::VISIBLE);
    dots->setVisibility(none ? Visibility::GONE : Visibility::VISIBLE);
    empty->setVisibility(none ? Visibility::VISIBLE : Visibility::GONE);

    if (none)
        ambient.clear();
    else {
        const int index = std::clamp(selectIndex, 0, (int)entries.size() - 1);
        carousel->jumpTo(index);
        // Before the view is shown, the focus lands on this one.
        carousel->setDefaultFocusedIndex(index);
        onSelected(index);
        if (focusHere || focus == this)
            Application::giveFocus(carousel->getChildren()[index]);
    }

    // Each PC is asked once whether it is on; that connection also lets
    // the covers never downloaded be fetched.
    std::set<size_t> shown;
    for (const Entry& entry : entries)
        if (const auto* pc = std::get_if<PcEntry>(&entry))
            shown.insert(pc->host);
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
        const auto* pc = std::get_if<PcEntry>(&entries[i]);
        if (!pc || pc->host != hostIndex)
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
    dots->setState(entries.size(), index);

#if defined(PLATFORM_SWITCH)
    if (const auto* local = std::get_if<LocalEntry>(&entries[index])) {
        title->setText(local->favorite.title);
        meta->setText(local->missing ? local->console->name + "  ·  " +
                                           "favorites/rom_gone"_i18n
                                     : local->console->name);
        if (local->hasCover)
            ambient.select(emu::coverPath(emu::pathsFor(*local->console),
                                          local->favorite.title));
        else
            ambient.clear();
        return;
    }
#endif

    const auto& pc = std::get<PcEntry>(entries[index]);
    title->setText(pc.app.name);
    const size_t host = pc.host;
    const auto state = hostOnline.find(host);
    const bool offline = state != hostOnline.end() && !state->second;
    meta->setText(offline ? hosts[host].hostname + "  ·  " +
                                "hosts/status_asleep"_i18n
                          : hosts[host].hostname);
    ambient.select(hosts[host], pc.app.app_id);
}

void FavoritesView::play(int index) {
#if defined(PLATFORM_SWITCH)
    if (launching || index < 0 || index >= (int)entries.size())
        return;
    const auto* local = std::get_if<LocalEntry>(&entries[index]);
    if (!local)
        return;
    if (local->missing) {
        Application::notify("emu/rom_missing"_i18n);
        return;
    }
    launching = true;
    const std::string romFile = local->favorite.romFile;
    std::shared_ptr<bool> token = alive;
    launchWhenCoversIdle(
        *local->console, local->rom, emu::Origin::Favorites, chipPosition(),
        alive, [this, token, index, romFile] {
            if (!*token)
                return;
            launching = false;
            // The game may have been deleted since the screen was filled:
            // show it dimmed.
            const auto* now = index < (int)entries.size()
                                  ? std::get_if<LocalEntry>(&entries[index])
                                  : nullptr;
            if (now && now->favorite.romFile == romFile &&
                !fileExists(now->rom.path))
                rebuild(index);
        });
#else
    (void)index;
#endif
}

void FavoritesView::willAppear(bool resetState) {
    Box::willAppear(resetState);
    ambient.attach(this);
}

void FavoritesView::willDisappear(bool resetState) {
    Box::willDisappear(resetState);
    ambient.detach();
}

void FavoritesView::draw(NVGcontext* vg, float x, float y, float width,
                         float height, Style style, FrameContext* ctx) {
    ambient.drawFallback(vg, x, y, width, height);
    Box::draw(vg, x, y, width, height, style, ctx);
}

void FavoritesView::unstar(int index) {
    if (index < 0 || index >= (int)entries.size())
        return;

#if defined(PLATFORM_SWITCH)
    if (const auto* local = std::get_if<LocalEntry>(&entries[index])) {
        const LocalFavorite favorite = local->favorite;
        Settings::instance().toggle_local_favorite(favorite);
        // The console may have no favorites left: drop its chip.
        if (!hasLocalFavorites(favorite.console)) {
            consoleFilters.erase(std::remove(consoleFilters.begin(),
                                             consoleFilters.end(),
                                             favorite.console),
                                 consoleFilters.end());
            if (consoleFilter == favorite.console)
                consoleFilter.clear();
            updateSourceActions();
        }
        rebuild(index);
        return;
    }
#endif

    const PcEntry entry = std::get<PcEntry>(entries[index]);
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
        updateSourceActions();
    }
    rebuild(index);
}
