//
//  console_shelf_view.cpp
//  Equinox
//

#include "console_shelf_view.hpp"

#if defined(PLATFORM_SWITCH)

#include "HttpDownload.hpp"
#include "Settings.hpp"
#include "bold_label.hpp"
#include "emu/CoverName.hpp"
#include "emu/CoverStore.hpp"
#include "emu/EmuLauncher.hpp"
#include "emu/PathUtf8.hpp"
#include "emu/PlayedAgo.hpp"
#include "helper.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <system_error>

using namespace brls;
using namespace brls::literals;

// One run of the cover queue. Each cover (all its names) is one task on
// the brls::async pool; the next task is scheduled from the UI thread when
// it ends, so the pool is free between covers.
struct ConsoleShelfView::CoverQueue {
    struct Job {
        size_t index;
        std::string title;
        std::string path;
    };

    std::atomic<bool> stop{false};
    std::vector<Job> jobs;
    size_t next = 0; // UI thread only
    const emu::Console* console = nullptr;
    emu::ConsolePaths where;
    uint64_t generation = 0;
    ConsoleShelfView* view = nullptr; // only used when *alive
    std::shared_ptr<bool> alive;
};

namespace {
constexpr long kCoverTimeout = 20;
// Before playing, the cover task gets this long to stop (checked every
// kStopPollMs on the UI thread).
constexpr int kStopPollMs = 100;
constexpr int kStopPolls = 50;

NVGcolor themeColor(const std::string& name) {
    return Application::getTheme().getColor(name);
}

bool isInside(View* view, View* ancestor) {
    for (View* v = view; v; v = v->hasParent() ? v->getParent() : nullptr)
        if (v == ancestor)
            return true;
    return false;
}

bool fileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(emu::fromUtf8(path), ec);
}

void removeFile(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(emu::fromUtf8(path), ec);
}

// Set while a cover task touches the network or the SD card, and by a
// launch from the moment it starts handing over (kept until the app quits).
// Covers download one at a time across every shelf. Only ever taken with
// tryAcquireCovers(): nothing waits on it, so no pool thread blocks.
std::atomic<bool> coversBusy{false};

bool tryAcquireCovers() { return !coversBusy.exchange(true); }
void releaseCovers() { coversBusy.store(false); }

constexpr int kBusyRetryMs = 200;

struct LaunchRequest {
    const emu::Console* console;
    emu::RomEntry rom;
    emu::Origin origin;
    int filter;
    std::shared_ptr<bool> alive;
    std::function<void()> onFail;
    Dialog* loader = nullptr;
};

void launchWhenIdle(std::shared_ptr<LaunchRequest> request, int attempt);

void finishLaunch(std::shared_ptr<LaunchRequest> request) {
    if (!*request->alive) {
        releaseCovers();
        return;
    }
    if (emu::launchRom(*request->console, request->rom, request->origin,
                       request->filter))
        return; // quitting; the cover flag stays set
    releaseCovers();
    if (request->onFail)
        request->onFail();
}

void launchWhenIdle(std::shared_ptr<LaunchRequest> request, int attempt) {
    if (tryAcquireCovers()) {
        if (Dialog* loader = request->loader) {
            request->loader = nullptr;
            loader->close([request] { finishLaunch(request); });
        } else {
            finishLaunch(request);
        }
        return;
    }

    if (attempt >= kStopPolls) {
        brls::Logger::warning("Launch: cover task did not stop");
        auto giveUp = [request] {
            if (!*request->alive)
                return;
            Application::notify("emu/try_again"_i18n);
            if (request->onFail)
                request->onFail();
        };
        if (Dialog* loader = request->loader) {
            request->loader = nullptr;
            loader->close(giveUp);
        } else {
            giveUp();
        }
        return;
    }

    if (attempt == 0) {
        request->loader = createLoadingDialog("emu/opening"_i18n);
        request->loader->open();
    }
    brls::delay(kStopPollMs, [request, attempt] {
        launchWhenIdle(request, attempt + 1);
    });
}

std::string upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return (char)std::toupper(c);
    });
    return text;
}

std::string joinExtensions(const std::vector<std::string>& extensions) {
    std::string text;
    for (const std::string& ext : extensions)
        text += (text.empty() ? "" : ", ") + ext;
    return text;
}
} // namespace

void launchWhenCoversIdle(const emu::Console& console, const emu::RomEntry& rom,
                          emu::Origin origin, int filter,
                          std::shared_ptr<bool> alive,
                          std::function<void()> onFail) {
    auto request = std::make_shared<LaunchRequest>(LaunchRequest{
        &console, rom, origin, filter, std::move(alive), std::move(onFail)});
    launchWhenIdle(request, 0);
}

ConsoleShelfView::ConsoleShelfView(const emu::Console& console,
                                   const std::string& selectRom)
    : consoleRef(console), paths(emu::pathsFor(console)),
      restoreRom(selectRom) {
    setAxis(Axis::COLUMN);
    setJustifyContent(JustifyContent::CENTER);
    setAlignItems(AlignItems::STRETCH);
    setFocusable(true);
    setHideHighlight(true);
    getAppletFrameItem()->title = console.name;

    // "DISPOSITIVOS › SUPER NINTENDO", the last part lit.
    auto* crumbRow = new Box(Axis::ROW);
    crumbRow->setJustifyContent(JustifyContent::CENTER);
    crumbRow->setAlignItems(AlignItems::CENTER);
    crumbRow->setHeight(20);
    auto* crumb = new BoldLabel();
    crumb->setFontSize(15);
    crumb->setSingleLine(true);
    crumb->setTextColor(themeColor("equinox/muted"));
    crumb->setText(upper("home/pcs"_i18n) + "   ›   ");
    crumbRow->addView(crumb);
    auto* lit = new BoldLabel();
    lit->setFontSize(15);
    lit->setSingleLine(true);
    lit->setTextColor(themeColor("equinox/sun"));
    lit->setText(upper(console.name));
    crumbRow->addView(lit);
    addView(crumbRow);

    shelf = new Box(Axis::COLUMN);
    shelf->setAlignItems(AlignItems::STRETCH);
    carousel = new CoverCarousel(ShelfStyle::covers());
    carousel->setMarginTop(6);
    carousel->onSelect = [this](int index) { onSelected(index); };
    shelf->addView(carousel);

    titleLabel = new Label();
    titleLabel->setFontSize(26);
    titleLabel->setSingleLine(true);
    titleLabel->setHorizontalAlign(HorizontalAlign::CENTER);
    titleLabel->setMarginTop(12);
    shelf->addView(titleLabel);

    metaLabel = new Label();
    metaLabel->setFontSize(17);
    metaLabel->setSingleLine(true);
    metaLabel->setHorizontalAlign(HorizontalAlign::CENTER);
    metaLabel->setTextColor(Application::getTheme()["brls/header/subtitle"]);
    metaLabel->setMarginTop(4);
    shelf->addView(metaLabel);

    dots = new CarouselDots();
    dots->setMarginTop(14);
    shelf->addView(dots);
    shelf->setVisibility(Visibility::GONE);
    addView(shelf);

    // No games yet: where to put them.
    empty = new Box(Axis::COLUMN);
    empty->setAlignItems(AlignItems::CENTER);
    empty->setMarginTop(40);
    empty->setFocusable(true);
    empty->setHideHighlight(true);

    auto* emptyTitle = new BoldLabel();
    emptyTitle->setFontSize(26);
    emptyTitle->setText("emu/empty_title"_i18n);
    empty->addView(emptyTitle);

    auto* emptyBody = new Label();
    emptyBody->setFontSize(18);
    emptyBody->setMarginTop(16);
    emptyBody->setHorizontalAlign(HorizontalAlign::CENTER);
    emptyBody->setTextColor(themeColor("equinox/muted"));
    emptyBody->setText(
        brls::getStr("emu/empty_body", joinExtensions(console.extensions)));
    empty->addView(emptyBody);

    auto* emptyPath = new BoldLabel();
    emptyPath->setFontSize(22);
    emptyPath->setMarginTop(12);
    emptyPath->setTextColor(themeColor("equinox/sun"));
    emptyPath->setText(paths.roms + "/");
    empty->addView(emptyPath);

    auto* emptyCovers = new Label();
    emptyCovers->setFontSize(18);
    emptyCovers->setMarginTop(16);
    emptyCovers->setTextColor(themeColor("equinox/muted"));
    emptyCovers->setText("emu/empty_covers"_i18n);
    empty->addView(emptyCovers);

    empty->registerAction("emu/rescan_short"_i18n, BUTTON_Y, [this](View*) {
        rescan(true);
        return true;
    });
    empty->setVisibility(Visibility::GONE);
    addView(empty);

    registerAction("emu/rescan"_i18n, BUTTON_X, [this](View*) {
        rescan(true);
        return true;
    });

    rescan(false);
}

ConsoleShelfView::~ConsoleShelfView() {
    *alive = false;
    fetchingIndex = -1; // the cells go with the view
    stopCovers();
}

void ConsoleShelfView::willAppear(bool resetState) {
    Box::willAppear(resetState);
    ambient.attach(this);
}

void ConsoleShelfView::willDisappear(bool resetState) {
    Box::willDisappear(resetState);
    ambient.detach();
}

void ConsoleShelfView::rescan(bool clearMisses) {
    if (scanning)
        return;
    scanning = true;
    stopCovers();

    const int sel = carousel->selectedIndex();
    if (sel >= 0 && sel < (int)games.size())
        restoreRom = games[sel].rom.fileName;

    const emu::Console* console = &consoleRef;
    const emu::ConsolePaths where = paths;
    std::shared_ptr<bool> token = alive;
    brls::async([this, token, console, where, clearMisses] {
        emu::ensureFolders(where);
        if (clearMisses)
            emu::clearCoverMisses(where);
        std::vector<Game> found;
        for (emu::RomEntry& rom : emu::scanRoms(where.roms, *console)) {
            Game game;
            game.hasCover = fileExists(emu::coverPath(where, rom.title));
            game.missed = !game.hasCover &&
                          emu::coverMissed(where, rom.title, APP_VERSION);
            game.rom = std::move(rom);
            found.push_back(std::move(game));
        }
        brls::sync([this, token, found] {
            if (!*token)
                return;
            scanning = false;
            showGames(found);
        });
    });
}

void ConsoleShelfView::showGames(std::vector<Game> found) {
    generation++;

    // The cells are about to go: keep the focus on this view meanwhile.
    View* focus = Application::getCurrentFocus();
    const bool focusHere = !focus || isInside(focus, this);
    if (focusHere)
        Application::giveFocus(this);

    carousel->clearViews();
    cells.clear();
    fetchingIndex = -1;
    games = std::move(found);

    const bool none = games.empty();
    shelf->setVisibility(none ? Visibility::GONE : Visibility::VISIBLE);
    empty->setVisibility(none ? Visibility::VISIBLE : Visibility::GONE);
    setActionAvailable(BUTTON_X, !none);
    if (none) {
        ambient.clear();
        titleLabel->setText("");
        metaLabel->setText("");
        dots->setState(0, 0);
        if (focusHere)
            Application::giveFocus(empty);
        Application::getGlobalHintsUpdateEvent()->fire();
        return;
    }

    for (size_t i = 0; i < games.size(); i++) {
        auto* cell = new RomCell(consoleRef, games[i].rom);
        const int index = (int)i;
        // A tap on a side cover brings it to the centre; a tap on the
        // centre one plays.
        cell->beforeLaunch = [this, index]() {
            if (carousel->selectedIndex() != index)
                return false;
            return Application::getInputType() != InputType::TOUCH ||
                   getCPUTimeUsec() - carousel->selectedAt() > 350000;
        };
        // Played once the press is handled: launching closes the app.
        cell->onPlay = [this, index] {
            std::shared_ptr<bool> token = alive;
            brls::sync([this, token, index] {
                if (*token)
                    play(index);
            });
        };
        cell->setFavorite(Settings::instance().is_local_favorite(
            consoleRef.slug, games[i].rom.fileName));
        updateFavoriteAction(cell);
        if (games[i].hasCover)
            cell->loadCover(emu::coverPath(paths, games[i].rom.title));
        cells.push_back(cell);
        carousel->addView(cell);
    }

    int index = 0;
    for (size_t i = 0; i < games.size(); i++)
        if (games[i].rom.fileName == restoreRom)
            index = (int)i;
    carousel->jumpTo(index);
    if (focusHere)
        Application::giveFocus(cells[index]);
    onSelected(index);
    Application::getGlobalHintsUpdateEvent()->fire();

    fetchCovers(index);
}

void ConsoleShelfView::updateFavoriteAction(RomCell* cell) {
    const emu::RomEntry& rom = cell->rom();
    const bool favorite =
        Settings::instance().is_local_favorite(consoleRef.slug, rom.fileName);
    cell->registerAction(
        favorite ? "app_list/unstar"_i18n : "app_list/star"_i18n, BUTTON_Y,
        [this, cell](View*) {
            const emu::RomEntry& rom = cell->rom();
            Settings::instance().toggle_local_favorite(
                {consoleRef.slug, rom.fileName, rom.title});
            cell->setFavorite(Settings::instance().is_local_favorite(
                consoleRef.slug, rom.fileName));
            // Registering again replaces this action: only its label
            // changes, after it has returned.
            std::shared_ptr<bool> token = alive;
            const uint64_t forGeneration = generation;
            brls::sync([this, token, cell, forGeneration] {
                if (*token && forGeneration == generation) {
                    updateFavoriteAction(cell);
                    Application::getGlobalHintsUpdateEvent()->fire();
                }
            });
            return true;
        });
}

std::string ConsoleShelfView::metaText(const Game& game) const {
    const int64_t played =
        Settings::instance().last_played(consoleRef.slug, game.rom.fileName);
    const int days =
        emu::daysSincePlayed(played, (int64_t)std::time(nullptr));
    std::string when;
    if (days < 0)
        when = "emu/never_played"_i18n;
    else if (days == 0)
        when = "emu/played_today"_i18n;
    else if (days == 1)
        when = "emu/played_yesterday"_i18n;
    else
        when = brls::getStr("emu/played_days", days);
    return consoleRef.name + "  ·  " + when;
}

void ConsoleShelfView::onSelected(int index) {
    if (index < 0 || index >= (int)games.size())
        return;
    const Game& game = games[index];

    titleLabel->setText(game.rom.title);
    metaLabel->setText(metaText(game));
    detailsFade.reset(0.0f);
    detailsFade.addStep(1.0f, 260, EasingFunction::quadraticOut);
    detailsFade.start();
    dots->setState(games.size(), index);

    if (game.hasCover)
        ambient.select(emu::coverPath(paths, game.rom.title));
    else
        ambient.clear();
}

void ConsoleShelfView::play(int index) {
    if (launching || index < 0 || index >= (int)games.size())
        return;
    launching = true;
    // Nothing may be downloading or writing to the SD card while the app
    // hands over: the game starts only once the cover task has finished.
    stopCovers();
    waitForCovers(index, 0);
}

void ConsoleShelfView::waitForCovers(int index, int attempt) {
    if (tryAcquireCovers()) {
        // Held until the app quits, or released below if it does not.
        if (stopLoader) {
            Dialog* loader = stopLoader;
            stopLoader = nullptr;
            std::shared_ptr<bool> token = alive;
            loader->close([this, token, index] {
                if (!*token) {
                    releaseCovers();
                    return;
                }
                launch(index);
            });
        } else {
            launch(index);
        }
        return;
    }

    if (attempt >= kStopPolls) {
        brls::Logger::warning("ConsoleShelf: cover task did not stop");
        Dialog* loader = stopLoader;
        stopLoader = nullptr;
        launching = false;
        auto giveUp = [this, index] {
            Application::notify("emu/try_again"_i18n);
            fetchCovers(index);
        };
        if (loader) {
            std::shared_ptr<bool> token = alive;
            loader->close([token, giveUp] {
                if (*token)
                    giveUp();
            });
        } else {
            giveUp();
        }
        return;
    }

    // Usually free right away; a download in flight stops within a second.
    if (attempt == 0) {
        stopLoader = createLoadingDialog("emu/opening"_i18n);
        stopLoader->open();
    }
    std::shared_ptr<bool> token = alive;
    brls::delay(kStopPollMs, [this, token, index, attempt] {
        if (*token)
            waitForCovers(index, attempt + 1);
    });
}

void ConsoleShelfView::launch(int index) {
    if (emu::launchRom(consoleRef, games[index].rom, emu::Origin::Shelf, -1))
        return; // quitting; the cover flag stays set
    releaseCovers();
    launching = false;

    // The game may have been deleted since the shelf was filled.
    if (!fileExists(games[index].rom.path))
        rescan(false);
    else
        fetchCovers(index);
}

void ConsoleShelfView::stopCovers() {
    if (covers)
        covers->stop.store(true);
    covers.reset();
    showFetching(-1);
}

// UI thread. -1 hides it.
void ConsoleShelfView::showFetching(int index) {
    if (fetchingIndex == index)
        return;
    if (fetchingIndex >= 0 && fetchingIndex < (int)cells.size())
        cells[fetchingIndex]->setFetching(false);
    fetchingIndex = index;
    if (index >= 0 && index < (int)cells.size())
        cells[index]->setFetching(true);
}

void ConsoleShelfView::fetchCovers(int first) {
    stopCovers();

    auto queue = std::make_shared<CoverQueue>();
    for (size_t i = 0; i < games.size(); i++)
        if (!games[i].hasCover && !games[i].missed)
            queue->jobs.push_back({i, games[i].rom.title,
                                   emu::coverPath(paths, games[i].rom.title)});
    if (queue->jobs.empty())
        return;
    // The selected game first, then outwards.
    std::stable_sort(queue->jobs.begin(), queue->jobs.end(),
                     [first](const CoverQueue::Job& a, const CoverQueue::Job& b) {
                         return std::abs((int)a.index - first) <
                                std::abs((int)b.index - first);
                     });

    queue->console = &consoleRef;
    queue->where = paths;
    queue->generation = generation;
    queue->view = this;
    queue->alive = alive;
    covers = queue;
    runNextCover(queue);
}

// UI thread: hands the next cover to the pool.
void ConsoleShelfView::runNextCover(std::shared_ptr<CoverQueue> queue) {
    if (queue->stop.load() || !*queue->alive ||
        queue->next >= queue->jobs.size())
        return;
    brls::async([queue] { fetchOneCover(queue); });
}

// Pool thread: one cover, every name it may have on libretro.
void ConsoleShelfView::fetchOneCover(std::shared_ptr<CoverQueue> queue) {
    if (queue->stop.load())
        return;
    // Another shelf's cover, or a launch, has the network: try again soon,
    // without holding this worker.
    if (!tryAcquireCovers()) {
        brls::delay(kBusyRetryMs, [queue] { runNextCover(queue); });
        return;
    }
    if (queue->stop.load()) {
        releaseCovers();
        return;
    }

    const CoverQueue::Job job = queue->jobs[queue->next];
    // Spinner on this game's cell. Posted before the end below, and syncs
    // run in order, so it cannot outlive the task.
    brls::sync([queue, job] {
        if (*queue->alive && !queue->stop.load() &&
            queue->view->covers == queue &&
            queue->generation == queue->view->generation)
            queue->view->showFetching((int)job.index);
    });
    const Progress keepGoing = [queue](uint64_t, uint64_t) {
        return !queue->stop.load();
    };

    // Fetched meanwhile by another shelf.
    bool found = fileExists(job.path);
    // A miss is remembered only when libretro clearly answered "no" for
    // every name: 404/403, or a 200 that is not a PNG. Anything else (no
    // answer, 5xx, 429, a body cut short) is like being offline: the queue
    // stops and nothing is remembered.
    bool transient = false, diskError = false;
    const std::vector<std::string> names =
        found ? std::vector<std::string>{}
              : emu::coverNameCandidates(job.title);
    for (const std::string& name : names) {
        if (queue->stop.load()) {
            transient = true;
            break;
        }
        const std::string part = job.path + ".part";
        std::string error;
        long status = 0;
        const bool ok =
            httpGetToFile(emu::coverUrl(*queue->console, name), part,
                          kCoverTimeout, keepGoing, error, status);
        const bool png = ok && emu::looksLikePng(part);
        if (png) {
            if (emu::replaceFile(part, job.path)) {
                found = true;
            } else {
                removeFile(part);
                diskError = true;
            }
            break;
        }
        removeFile(part);
        const bool notThere = ok || status == 404 || status == 403;
        if (!notThere) {
            transient = true;
            break;
        }
    }
    if (!found && !transient && !diskError)
        emu::markCoverMissed(queue->where, job.title, APP_VERSION);
    releaseCovers();

    brls::sync([queue, job, found, transient] {
        if (!*queue->alive)
            return;
        if (queue->view->covers == queue &&
            queue->view->fetchingIndex == (int)job.index)
            queue->view->showFetching(-1);
        if (found)
            queue->view->coverArrived(queue->generation, job.index, job.path);
        if (transient)
            return; // generic covers; tried again next time
        queue->next++;
        runNextCover(queue);
    });
}

void ConsoleShelfView::coverArrived(uint64_t forGeneration, size_t index,
                                    const std::string& path) {
    if (forGeneration != generation || index >= cells.size())
        return;
    games[index].hasCover = true;
    cells[index]->loadCover(path);
    if (carousel->selectedIndex() == (int)index)
        ambient.select(path);
}

void ConsoleShelfView::draw(NVGcontext* vg, float x, float y, float width,
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

#endif // PLATFORM_SWITCH
