//
//  console_shelf_view.cpp
//  Equinox
//

#include "console_shelf_view.hpp"
#include "options_sheet.hpp"

#if defined(PLATFORM_SWITCH)

#include "HttpDownload.hpp"
#include "Settings.hpp"
#include "bold_label.hpp"
#include "emu/CoverName.hpp"
#include "emu/CoverStore.hpp"
#include "emu/EmuLauncher.hpp"
#include "emu/NetGate.hpp"
#include "emu/PathUtf8.hpp"
#include "game_data_queue.hpp"
#include "game_details_panel.hpp"
#include "game_list_panel.hpp"
#include "game_row_cell.hpp"
#include "helper.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
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
        std::string canonical; // from the game's data, when it has any
        std::string path;
    };

    std::atomic<bool> stop{false};
    std::vector<Job> jobs; // UI thread only: jobs can be added while it runs
    size_t next = 0;       // UI thread only
    Job running;           // copy of jobs[next], written before its task starts
    bool inFlight = false; // `running` was handed to the pool and has not ended
    bool halted = false;   // stopped by a transient failure: nothing else will run
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

constexpr int kBusyRetryMs = 200;
// A tap on a row picks the game; only a tap on the picked one plays.
constexpr brls::Time kTapPlayUsec = 350000;

std::string existingFile(const std::string& path) {
    return fileExists(path) ? path : std::string();
}

// Pool thread. Loaded once per run and never freed: a game's data task may
// still be reading it after its shelf has closed.
std::shared_ptr<const emu::GameDb> loadGameDb(const emu::Console& console) {
    static std::mutex mutex;
    static std::map<std::string, std::shared_ptr<const emu::GameDb>> loaded;
    std::lock_guard<std::mutex> lock(mutex);
    std::shared_ptr<const emu::GameDb>& db = loaded[console.slug];
    if (!db) {
        auto fresh = std::make_shared<emu::GameDb>();
        if (!fresh->loadFile(emu::gameDbPath(console)))
            brls::Logger::warning("ConsoleShelf: no game database for {}", console.slug);
        db = fresh;
    }
    return db;
}

const emu::GameInfo* infoIn(const emu::GameDb* gameDb, const ConsoleShelfView::Game& game) {
    if (!gameDb || !game.hasData || !game.meta.identified)
        return nullptr;
    return gameDb->find(game.meta.crc);
}

emu::LibraryItem makeItem(const emu::GameDb* gameDb, const ConsoleShelfView::Game& game) {
    emu::LibraryItem item;
    const bool canonical = game.hasData && !game.meta.canonical.empty();
    item.name = emu::displayName(canonical ? game.meta.canonical : game.rom.title);
    if (item.name.empty())
        item.name = game.rom.title;
    if (const emu::GameInfo* info = infoIn(gameDb, game)) {
        item.year = info->year;
        item.genre = info->genre;
    }
    return item;
}

void readCoverState(const emu::ConsolePaths& where, ConsoleShelfView::Game& game) {
    game.hasCover = fileExists(emu::coverPath(where, game.rom.title));
    game.missed = !game.hasCover && emu::coverMissed(where, game.rom.title, APP_VERSION);
}

// Off the UI thread: the game's data, when it still describes the ROM.
void readGameData(const emu::ConsolePaths& where, ConsoleShelfView::Game& game) {
    game.snapPath = existingFile(emu::thumbPath(where, emu::ThumbKind::Snap, game.rom.title));
    game.titlePath = existingFile(emu::thumbPath(where, emu::ThumbKind::Title, game.rom.title));
    game.hasData = false;
    game.meta = {};
    uint64_t size = 0;
    int64_t mtime = 0;
    emu::GameMeta meta;
    if (emu::romStat(game.rom.path, size, mtime) &&
        emu::loadMeta(emu::metaPath(where, game.rom.title), meta) &&
        emu::metaFresh(meta, size, mtime)) {
        game.hasData = true;
        game.meta = std::move(meta);
    }
}

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
        emu::netRelease();
        return;
    }
    if (emu::launchRom(*request->console, request->rom, request->origin,
                       request->filter))
        return; // quitting; the cover flag stays set
    emu::netRelease();
    if (request->onFail)
        request->onFail();
}

void launchWhenIdle(std::shared_ptr<LaunchRequest> request, int attempt) {
    if (emu::netTryAcquire()) {
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

    library = new Box(Axis::ROW);
    library->setGrow(1);
    library->setAlignItems(AlignItems::STRETCH);
    listPanel = new GameListPanel(console);
    listPanel->hideShortcutHints();
    listPanel->bindRow = [this](GameRowCell& cell, size_t index) { bindRow(cell, index); };
    listPanel->onFocusGame = [this](std::optional<size_t> index) {
        onCurrent(index);
    };
    listPanel->onDownloadAll = [this] { downloadAll(); };
    listPanel->onRetry = [this] { retryStopped(); };
    listPanel->onCancel = [this] { cancelData(); };
    library->addView(listPanel);
    detailsPanel = new GameDetailsPanel();
    detailsPanel->onDownload = [this] {
        if (current)
            download(*current);
    };
    library->addView(detailsPanel);
    library->setVisibility(Visibility::GONE);
    addView(library);

    loading = new Box(Axis::COLUMN);
    loading->setGrow(1);
    loading->setAlignItems(AlignItems::CENTER);
    loading->setJustifyContent(JustifyContent::CENTER);
    auto* spinner = new ProgressSpinner(ProgressSpinnerSize::LARGE);
    spinner->View::setSize(Size(92, 92));
    loading->addView(spinner);
    auto* loadingLabel = new Label();
    loadingLabel->setFontSize(20);
    loadingLabel->setMarginTop(16);
    loadingLabel->setTextColor(themeColor("equinox/muted"));
    loadingLabel->setText("emu/scanning"_i18n);
    loading->addView(loadingLabel);
    loading->setVisibility(Visibility::GONE);
    addView(loading);

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
    emptyPath->setTextColor(themeColor("equinox/status_on"));
    emptyPath->setText(paths.roms + "/");
    empty->addView(emptyPath);

    auto* emptyCovers = new Label();
    emptyCovers->setFontSize(18);
    emptyCovers->setMarginTop(16);
    emptyCovers->setTextColor(themeColor("equinox/muted"));
    emptyCovers->setText("emu/empty_covers"_i18n);
    empty->addView(emptyCovers);

    empty->registerAction("emu/rescan_short"_i18n, BUTTON_Y, [this](View*) {
        announceScan = true;
        rescan(true);
        return true;
    });
    empty->setVisibility(Visibility::GONE);
    addView(empty);

    registerAction("emu/more_options"_i18n, BUTTON_X, [this](View*) {
        openMore();
        return true;
    });
    registerAction("app_list/star"_i18n, BUTTON_Y, [this](View*) {
        if (!current)
            return true;
        const emu::RomEntry& rom = games[*current].rom;
        Settings::instance().toggle_local_favorite(
            {consoleRef.slug, rom.fileName, rom.title});
        applyActions();
        return true;
    });
    registerAction("emu/rescan"_i18n, BUTTON_LT, [this](View*) {
        announceScan = true;
        rescan(true);
        return true;
    }, true);
    applyActions();

    openLibrary();
}

ConsoleShelfView::~ConsoleShelfView() {
    *alive = false;
    stopCovers();
}

void ConsoleShelfView::willAppear(bool resetState) {
    Box::willAppear(resetState);
    ambient.attach(this);
}

void ConsoleShelfView::willDisappear(bool resetState) {
    Box::willDisappear(resetState);
    ambient.detach();
    saveIndex();
}

// A list on screen stays usable while it is refreshed.
void ConsoleShelfView::showLoading() {
    listPanel->setScanning(true);
    if (library->getVisibility() == Visibility::VISIBLE)
        return;
    if (empty->getVisibility() == Visibility::VISIBLE) {
        Application::giveFocus(this);
        empty->setVisibility(Visibility::GONE);
    }
    loading->setVisibility(Visibility::VISIBLE);
}

void ConsoleShelfView::openLibrary() {
    scanning = true;
    showLoading();

    const emu::Console* console = &consoleRef;
    const emu::ConsolePaths where = paths;
    std::shared_ptr<bool> token = alive;
    emu::PreparedOrder request = listPanel->orderRequest();
    brls::async([this, token, console, where, request] {
        namespace liblog = emu::liblog;
        const int64_t began = liblog::nowUs();
        std::shared_ptr<const emu::GameDb> gameDb = loadGameDb(*console);
        const int64_t dbDone = liblog::nowUs();
        auto scan = std::make_shared<Scan>();
        const bool usable = emu::loadLibraryIndex(where, APP_VERSION, scan->games);
        const int64_t indexDone = liblog::nowUs();
        if (usable) {
            scan->items.reserve(scan->games.size());
            for (const Game& game : scan->games)
                scan->items.push_back(makeItem(gameDb.get(), game));
            scan->order = request;
            scan->order.shown = emu::libraryOrder(scan->items, request.query, request.sort);
        }
        liblog::note("index: {} games in {} ms | db {} read {} names+order {}, usable {}",
                     scan->games.size(), (liblog::nowUs() - began) / 1000,
                     (dbDone - began) / 1000, (indexDone - dbDone) / 1000,
                     (liblog::nowUs() - indexDone) / 1000, usable);
        liblog::flushSoon();
        brls::sync([this, token, scan, gameDb, usable] {
            if (!*token)
                return;
            scanning = false;
            if (!usable) {
                rescan(false);
                return;
            }
            if (!db) {
                db = gameDb;
                createDataQueue();
            }
            showGames(*scan);
            checkFolder();
        });
    });
}

void ConsoleShelfView::checkFolder() {
    std::vector<std::string> known;
    known.reserve(games.size());
    for (const Game& game : games)
        known.push_back(game.rom.fileName);

    const emu::Console* console = &consoleRef;
    const emu::ConsolePaths where = paths;
    std::shared_ptr<bool> token = alive;
    const uint64_t forGeneration = generation;
    brls::async([this, token, console, where, known, forGeneration] {
        namespace liblog = emu::liblog;
        const int64_t began = liblog::nowUs();
        std::vector<std::string> names;
        if (!emu::listRomNames(where.roms, *console, names)) {
            // A card that cannot be read says nothing about the games on it.
            liblog::note("folder check: could not read {}", where.roms);
            liblog::flushSoon();
            return;
        }
        emu::LibraryDiff diff = emu::diffLibrary(known, names);
        auto change = std::make_shared<FolderChange>();
        change->removed = std::move(diff.removed);
        for (const std::string& name : diff.added) {
            Game game;
            game.rom = emu::romEntryFor(where.roms, name);
            if (!emu::romFileUsable(game.rom.path))
                continue;
            readCoverState(where, game);
            readGameData(where, game);
            change->added.push_back(std::move(game));
        }
        liblog::note("folder check: {} names in {} ms, {} new, {} gone", names.size(),
                     (liblog::nowUs() - began) / 1000, change->added.size(),
                     change->removed.size());
        liblog::flushSoon();
        if (change->added.empty() && change->removed.empty())
            return;
        brls::sync([this, token, change, forGeneration] {
            if (*token)
                applyFolderChange(*change, forGeneration);
        });
    });
}

void ConsoleShelfView::applyFolderChange(const FolderChange& change, uint64_t forGeneration) {
    // Renumbering now would break what is running. The index stays as it was,
    // so the next opening finds the same difference.
    if (forGeneration != generation || scanning || launching ||
        (dataQueue && dataQueue->running())) {
        emu::liblog::note("folder check: change left for the next opening");
        return;
    }
    if (current)
        restoreRom = games[*current].rom.fileName;
    stopCovers();

    std::vector<std::string> removed = change.removed;
    std::sort(removed.begin(), removed.end());
    Scan scan;
    scan.games.reserve(games.size() + change.added.size());
    for (const Game& game : games)
        if (!std::binary_search(removed.begin(), removed.end(), game.rom.fileName))
            scan.games.push_back(game);
    scan.games.insert(scan.games.end(), change.added.begin(), change.added.end());
    std::stable_sort(scan.games.begin(), scan.games.end(), [](const Game& a, const Game& b) {
        return emu::romFileNameLess(a.rom.fileName, b.rom.fileName);
    });
    scan.items.reserve(scan.games.size());
    for (const Game& game : scan.games)
        scan.items.push_back(itemFor(game));
    scan.order = listPanel->orderRequest();
    scan.order.shown = emu::libraryOrder(scan.items, scan.order.query, scan.order.sort);

    showGames(scan);
    indexDirty = true;
    saveIndex();
}

void ConsoleShelfView::saveIndex(bool wait) {
    if (!indexDirty || scanning)
        return;
    if (wait) {
        indexDirty = false;
        emu::saveLibraryIndex(paths, APP_VERSION, games);
        emu::writeRomCount(paths, static_cast<int>(games.size()));
        return;
    }
    if (indexSaving)
        return; // saved again when the one running ends
    indexDirty = false;
    indexSaving = true;
    auto snapshot = std::make_shared<std::vector<Game>>(games);
    const emu::ConsolePaths where = paths;
    std::shared_ptr<bool> token = alive;
    brls::async([this, token, snapshot, where] {
        if (!emu::saveLibraryIndex(where, APP_VERSION, *snapshot))
            emu::liblog::bug("library index could not be saved");
        emu::writeRomCount(where, static_cast<int>(snapshot->size()));
        brls::sync([this, token] {
            if (!*token)
                return;
            indexSaving = false;
            saveIndex();
        });
    });
}

void ConsoleShelfView::rescan(bool clearMisses) {
    if (scanning)
        return;
    scanning = true;
    stopCovers();
    // The rows are about to be numbered again.
    cancelData();
    failure.reset();
    listPanel->setBatchStopped(std::nullopt);
    showLoading();

    if (current)
        restoreRom = games[*current].rom.fileName;

    const emu::Console* console = &consoleRef;
    const emu::ConsolePaths where = paths;
    std::shared_ptr<bool> token = alive;
    emu::PreparedOrder request = listPanel->orderRequest();
    emu::liblog::note("rescan start: {}, clearMisses {}", console->name, clearMisses);
    brls::async([this, token, console, where, clearMisses, request] {
        namespace liblog = emu::liblog;
        const int64_t began = liblog::nowUs();
        std::shared_ptr<const emu::GameDb> gameDb = loadGameDb(*console);
        const int64_t dbDone = liblog::nowUs();
        emu::ensureFolders(where);
        if (clearMisses)
            emu::clearCoverMisses(where);
        const int64_t foldersDone = liblog::nowUs();
        // Shared, not captured: closures passed to brls::sync get copied.
        auto scan = std::make_shared<Scan>();
        std::vector<Game>& found = scan->games;
        std::vector<emu::RomEntry> roms = emu::scanRoms(where.roms, *console);
        emu::writeRomCount(where, static_cast<int>(roms.size()));
        const int64_t listDone = liblog::nowUs();
        int64_t coverUs = 0, dataUs = 0, worstUs = 0;
        std::string worstRom;
        found.reserve(roms.size());
        for (emu::RomEntry& rom : roms) {
            Game game;
            const int64_t gameStart = liblog::nowUs();
            game.rom = std::move(rom);
            readCoverState(where, game);
            const int64_t coverDone = liblog::nowUs();
            readGameData(where, game);
            const int64_t gameDone = liblog::nowUs();
            coverUs += coverDone - gameStart;
            dataUs += gameDone - coverDone;
            if (gameDone - gameStart > worstUs) {
                worstUs = gameDone - gameStart;
                worstRom = game.rom.title;
            }
            found.push_back(std::move(game));
        }
        const int64_t readDone = liblog::nowUs();
        scan->items.reserve(found.size());
        for (const Game& game : found)
            scan->items.push_back(makeItem(gameDb.get(), game));
        const int64_t namesDone = liblog::nowUs();
        scan->order = request;
        scan->order.shown = emu::libraryOrder(scan->items, request.query, request.sort);
        const int64_t finished = liblog::nowUs();
        liblog::note("scan: {} games in {} ms | db {} folders {} list {} covers {} data {} "
                     "(worst {} ms: {}) names {} order {}",
                     found.size(), (finished - began) / 1000, (dbDone - began) / 1000,
                     (foldersDone - dbDone) / 1000, (listDone - foldersDone) / 1000,
                     coverUs / 1000, dataUs / 1000, worstUs / 1000, worstRom,
                     (namesDone - readDone) / 1000, (finished - namesDone) / 1000);
        if (scan->items.size() != found.size())
            liblog::bug("scan made {} items for {} games", scan->items.size(), found.size());
        for (size_t index : scan->order.shown)
            if (index >= found.size()) {
                liblog::bug("scan order points at game {} of {}", index, found.size());
                break;
            }
        if (!emu::saveLibraryIndex(where, APP_VERSION, found))
            liblog::bug("library index could not be saved");
        liblog::flushSoon();
        brls::sync([this, token, scan, gameDb, finished] {
            if (!*token)
                return;
            const int64_t arrived = emu::liblog::nowUs();
            scanning = false;
            if (!db) {
                db = gameDb;
                createDataQueue();
            }
            showGames(*scan);
            emu::liblog::note("show: waited {} ms for the UI thread, showGames {} ms",
                              (arrived - finished) / 1000,
                              (emu::liblog::nowUs() - arrived) / 1000);
            emu::liblog::flushSoon();
        });
    });
}

void ConsoleShelfView::createDataQueue() {
    dataQueue = std::make_unique<GameDataQueue>(consoleRef);
    dataQueue->onStep = [this](size_t index, emu::DataStep step) {
        if (dataQueue->current() == index)
            dataStep = step;
        applyProgress();
    };
    dataQueue->onFinished = [this](size_t index, const emu::DataOutcome& outcome) {
        dataFinished(index, outcome);
    };
    dataQueue->onStopped = [this](emu::DataStop stop) { dataStopped(stop); };
    dataQueue->onAdvanced = [this] { applyProgress(); };
}

void ConsoleShelfView::showGames(Scan& scan) {
    generation++;
    loading->setVisibility(Visibility::GONE);
    listPanel->setScanning(false);
    if (announceScan) {
        announceScan = false;
        const size_t found = scan.games.size();
        Application::notify(brls::getStr(
            "emu/scan_done", found == 1 ? "hosts/games_one"_i18n
                                        : brls::getStr("hosts/games_count", found)));
    }
    // The downloads go on: the queue follows the games by file name.
    dataStep.reset();
    failure.reset();
    stoppedIndex.reset();
    retry.reset();
    listPanel->setBatchStopped(std::nullopt);

    View* focus = Application::getCurrentFocus();
    const bool focusHere = !focus || isInside(focus, this);

    games = std::move(scan.games);
    const bool none = games.empty();
    std::vector<std::string> fileNames;
    fileNames.reserve(games.size());
    for (const Game& game : games)
        fileNames.push_back(game.rom.fileName);
    dataQueue->setGames(std::move(fileNames));
    batchActive = dataQueue->running();

    // The panel reports its first result as current while it fills.
    listPanel->setItems(std::move(scan.items), std::move(scan.order));

    library->setVisibility(none ? Visibility::GONE : Visibility::VISIBLE);
    empty->setVisibility(none ? Visibility::VISIBLE : Visibility::GONE);
    setActionAvailable(BUTTON_LT, !none);
    if (none) {
        onCurrent(std::nullopt);
        if (focusHere)
            Application::giveFocus(empty);
        applyProgress();
        return;
    }

    size_t index = 0;
    for (size_t i = 0; i < games.size(); i++)
        if (games[i].rom.fileName == restoreRom)
            index = i;
    if (focusHere)
        listPanel->focusGame(index);
    applyProgress();

    // A launch waiting for the network restarts them if it gives up.
    if (!launching)
        fetchCovers(current.value_or(index));
}

const emu::GameInfo* ConsoleShelfView::infoFor(const Game& game) const {
    return infoIn(db.get(), game);
}

emu::LibraryItem ConsoleShelfView::itemFor(const Game& game) const {
    return makeItem(db.get(), game);
}

GameDetails ConsoleShelfView::detailsFor(size_t index) const {
    const Game& game = games[index];
    GameDetails details;
    details.name = itemFor(game).name;
    details.fileName = game.rom.fileName;
    details.hasData = game.hasData;
    // Before the download nothing says the game is unknown.
    details.identified = !game.hasData || game.meta.identified;
    details.info = infoFor(game);
    if (game.hasData && game.meta.identified)
        details.regions = emu::noIntroRegions(game.meta.canonical);
    details.snapPath = game.snapPath;
    details.titlePath = game.titlePath;
    details.snap = game.meta.snap;
    details.title = game.meta.title;
    details.synopsis = game.meta.synopsisText;
    return details;
}

void ConsoleShelfView::bindRow(GameRowCell& cell, size_t index) {
    if (index >= games.size())
        return;
    const Game& game = games[index];
    // Played once the press is handled: launching closes the app.
    const uint64_t forGeneration = generation;
    cell.onPlay = [this, index, forGeneration] {
        if (current != index)
            return;
        if (Application::getInputType() == InputType::TOUCH &&
            getCPUTimeUsec() - currentAt < kTapPlayUsec)
            return;
        std::shared_ptr<bool> token = alive;
        brls::sync([this, token, index, forGeneration] {
            if (*token && forGeneration == generation)
                play(index);
        });
    };

    if (game.hasCover)
        cell.setCover(emu::coverPath(paths, game.rom.title));
    else
        cell.clearCover();

    const std::optional<size_t> downloading =
        dataQueue ? dataQueue->current() : std::nullopt;
    const bool stepping = downloading == index && dataStep.has_value();
    std::optional<int> percent;
    if (stepping)
        percent = (int)std::lround(emu::stepProgress(*dataStep) * 100);
    // Waiting, or picked by the queue but not at its first step yet.
    cell.setDataState(game.hasData, percent,
                      dataQueue && dataQueue->queued(index) && !stepping);

    const emu::LibraryItem item = itemFor(game);
    std::string sub;
    if (!game.hasData) {
        sub = "emu/no_data"_i18n;
    } else {
        if (item.year > 0)
            sub = std::to_string(item.year);
        if (!item.genre.empty())
            sub += (sub.empty() ? "" : " · ") + item.genre;
    }
    cell.setGame(item.name, sub, item.year > 0 ? std::to_string(item.year) : "");
}

void ConsoleShelfView::onCurrent(std::optional<size_t> index) {
    if (index && *index >= games.size())
        index.reset();
    current = index;
    currentAt = getCPUTimeUsec();
    if (!current) {
        // Nothing matches the search: no game to show.
        detailsPanel->setVisibility(Visibility::INVISIBLE);
    } else {
        detailsPanel->setVisibility(Visibility::VISIBLE);
        detailsPanel->show(detailsFor(*current), true);
    }
    applyAmbient();
    applyProgress();
    applyActions();
}

// The gameplay capture when there is one, else the cover.
void ConsoleShelfView::applyAmbient() {
    if (!current) {
        ambient.clear();
        return;
    }
    const Game& game = games[*current];
    if (!game.snapPath.empty())
        ambient.select(game.snapPath);
    else if (game.hasCover)
        ambient.select(emu::coverPath(paths, game.rom.title));
    else
        ambient.clear();
}

void ConsoleShelfView::openMore() {
    std::vector<OptionsSheet::Item> items;
    if (current) {
        items.push_back({games[*current].hasData ? "library/details/update"_i18n
                                                 : "library/hint/download"_i18n,
                         [this] {
                             if (current)
                                 download(*current);
                         }});
    }
    items.push_back({"library/list/hint_search"_i18n, [this] { listPanel->openSearch(); }});
    items.push_back({"library/list/hint_sort"_i18n, [this] { listPanel->openSort(); }});
    items.push_back({"library/list/hint_mode"_i18n, [this] { listPanel->toggleMode(); }});
    if (listPanel->batchActionAvailable())
        items.push_back({listPanel->batchActionLabel(),
                         [this] { listPanel->downloadAllOrCancel(); }});
    items.push_back({"emu/rescan"_i18n, [this] {
                         announceScan = true;
                         rescan(true);
                     }});
    OptionsSheet::open("emu/more_options"_i18n, std::move(items));
}

void ConsoleShelfView::applyActions() {
    const bool any = current.has_value();
    setActionAvailable(BUTTON_X, !games.empty());
    setActionAvailable(BUTTON_Y, any);
    if (any) {
        const Game& game = games[*current];
        const bool favorite = Settings::instance().is_local_favorite(
            consoleRef.slug, game.rom.fileName);
        updateActionHint(BUTTON_Y,
                         favorite ? "app_list/unstar"_i18n : "app_list/star"_i18n);
    }
    Application::getGlobalHintsUpdateEvent()->fire();
}

void ConsoleShelfView::applyProgress() {
    const std::optional<size_t> downloading =
        dataQueue ? dataQueue->current() : std::nullopt;
    const auto stepOf = [&](size_t i) -> std::optional<emu::DataStep> {
        if (downloading == i)
            return dataStep;
        return std::nullopt;
    };
    // Waiting, or picked by the queue but not at its first step yet.
    const auto waiting = [&](size_t i) {
        return dataQueue && dataQueue->queued(i) && !stepOf(i);
    };

    listPanel->refreshRows();
    size_t missing = 0;
    for (const Game& game : games)
        if (!game.hasData)
            missing++;

    if (current) {
        detailsPanel->setFailed(failure && failure->index == *current
                                    ? std::optional<emu::DataStop>(failure->stop)
                                    : std::nullopt);
        detailsPanel->setProgress(stepOf(*current), waiting(*current));
    }

    const bool running = dataQueue && dataQueue->running();
    const size_t total = running ? dataQueue->total() : 0;
    const size_t done = running ? dataQueue->done() : 0;
    int percent = 0;
    if (total > 0) {
        const float step = dataStep ? emu::stepProgress(*dataStep) : 0.0f;
        percent = (int)std::lround((done + step) * 100 / total);
    }
    std::string name;
    if (downloading && *downloading < games.size())
        name = itemFor(games[*downloading]).name;
    listPanel->setBatch(running, done, total, percent, name, missing);
    keepFocus();
}

void ConsoleShelfView::keepFocus() {
    View* focus = Application::getCurrentFocus();
    if (!current || !focus || !isInside(focus, detailsPanel))
        return;
    for (View* v = focus; v && v != detailsPanel; v = v->getParent()) {
        if (v->getVisibility() != Visibility::VISIBLE) {
            listPanel->focusGame(*current);
            return;
        }
    }
}

void ConsoleShelfView::download(size_t index) {
    // The scan may number the games differently.
    if (!dataQueue || scanning || index >= games.size())
        return;
    failure.reset();
    dataQueue->enqueue(index, games[index].rom);
    applyProgress();
}

void ConsoleShelfView::downloadAll() {
    if (!dataQueue || scanning)
        return;
    failure.reset();
    retry.reset();
    listPanel->setBatchStopped(std::nullopt);
    batchActive = true;
    // The picked game first, then the others.
    if (current && !games[*current].hasData)
        dataQueue->enqueue(*current, games[*current].rom, emu::DlPriority::Batch);
    for (size_t i = 0; i < games.size(); i++)
        if (!games[i].hasData)
            dataQueue->enqueue(i, games[i].rom, emu::DlPriority::Batch);
    if (!dataQueue->running())
        batchActive = false;
    applyProgress();
}

void ConsoleShelfView::retryStopped() {
    if (!dataQueue || scanning || !retry)
        return;
    const Retry again = *retry;
    retry.reset();
    failure.reset();
    listPanel->setBatchStopped(std::nullopt);
    // A refresh stops on a game that already has data: it goes first too.
    if (again.index < games.size())
        dataQueue->enqueue(again.index, games[again.index].rom);
    if (again.batch) {
        batchActive = true;
        for (size_t i = 0; i < games.size(); i++)
            if (!games[i].hasData)
                dataQueue->enqueue(i, games[i].rom, emu::DlPriority::Batch);
    }
    if (!dataQueue->running())
        batchActive = false;
    applyProgress();
}

void ConsoleShelfView::cancelData() {
    if (dataQueue)
        dataQueue->cancel();
    dataStep.reset();
    batchActive = false;
    applyProgress();
}

void ConsoleShelfView::dataFinished(size_t index, const emu::DataOutcome& outcome) {
    dataStep.reset();
    if (outcome.stop != emu::DataStop::None) {
        stoppedIndex = index;
        stoppedInBatch = batchActive;
    }
    // The queue is already empty here when this was the last game or it stopped.
    if (!dataQueue->running())
        batchActive = false;
    if (index < games.size()) {
        Game& game = games[index];
        readGameData(paths, game);
        indexDirty = true;
        listPanel->setItem(index, itemFor(game));
        if (current == index) {
            detailsPanel->show(detailsFor(index), false);
            applyAmbient();
            applyActions();
        }
        retryCover(index);
    }
    if (!dataQueue->running())
        saveIndex();
    applyProgress();
}

void ConsoleShelfView::dataStopped(emu::DataStop stop) {
    if (stop != emu::DataStop::Cancelled && stoppedIndex) {
        failure = Failure{*stoppedIndex, stop};
        retry = Retry{*stoppedIndex, stoppedInBatch};
        listPanel->setBatchStopped(stop);
    }
    stoppedIndex.reset();
    applyProgress();
}

void ConsoleShelfView::play(size_t index) {
    // A scan in progress is about to renumber the games.
    if (launching || scanning || index >= games.size())
        return;
    launching = true;
    // Nothing may be downloading or writing to the SD card while the app
    // hands over: the game starts only once the network gate is free.
    cancelData();
    stopCovers();
    waitForCovers({index, games[index].rom, generation}, 0);
}

void ConsoleShelfView::waitForCovers(const LaunchTarget& target, int attempt) {
    if (target.generation != generation) {
        endLaunch(false, false);
        return;
    }
    if (emu::netTryAcquire()) {
        // Held until the app quits, or released below if it does not.
        if (stopLoader) {
            Dialog* loader = stopLoader;
            stopLoader = nullptr;
            std::shared_ptr<bool> token = alive;
            loader->close([this, token, target] {
                if (!*token) {
                    emu::netRelease();
                    return;
                }
                launch(target);
            });
        } else {
            launch(target);
        }
        return;
    }

    if (attempt >= kStopPolls) {
        brls::Logger::warning("ConsoleShelf: download did not stop");
        endLaunch(false, true);
        return;
    }

    // Usually free right away; a download in flight stops within a second.
    if (attempt == 0) {
        stopLoader = createLoadingDialog("emu/opening"_i18n);
        stopLoader->open();
    }
    std::shared_ptr<bool> token = alive;
    brls::delay(kStopPollMs, [this, token, target, attempt] {
        if (*token)
            waitForCovers(target, attempt + 1);
    });
}

void ConsoleShelfView::launch(const LaunchTarget& target) {
    // The list was rebuilt while waiting: the game may not be where it was.
    if (target.generation != generation) {
        endLaunch(true, false);
        return;
    }
    saveIndex(true);
    if (emu::launchRom(consoleRef, target.rom, emu::Origin::Shelf, -1))
        return; // quitting; the network gate stays taken
    emu::netRelease();
    launching = false;

    // The game may have been deleted since the shelf was filled.
    if (!fileExists(target.rom.path))
        rescan(false);
    else
        fetchCovers(target.index);
}

void ConsoleShelfView::endLaunch(bool releaseGate, bool notify) {
    if (releaseGate)
        emu::netRelease();
    launching = false;
    Dialog* loader = stopLoader;
    stopLoader = nullptr;
    std::shared_ptr<bool> token = alive;
    auto resume = [this, token, notify] {
        if (!*token)
            return;
        if (notify)
            Application::notify("emu/try_again"_i18n);
        fetchCovers(current.value_or(0));
    };
    if (loader)
        loader->close(resume);
    else
        resume();
}

void ConsoleShelfView::stopCovers() {
    if (covers)
        covers->stop.store(true);
    covers.reset();
}

void ConsoleShelfView::fetchCovers(size_t first) {
    stopCovers();

    auto queue = newCoverQueue();
    for (size_t i = 0; i < games.size(); i++) {
        const Game& game = games[i];
        if (game.hasCover || game.missed)
            continue;
        queue->jobs.push_back({i, game.rom.title,
                               game.hasData ? game.meta.canonical : std::string(),
                               emu::coverPath(paths, game.rom.title)});
    }
    if (queue->jobs.empty())
        return;
    // The selected game first, then outwards.
    const auto distance = [first](size_t index) {
        return index > first ? index - first : first - index;
    };
    std::stable_sort(queue->jobs.begin(), queue->jobs.end(),
                     [&](const CoverQueue::Job& a, const CoverQueue::Job& b) {
                         return distance(a.index) < distance(b.index);
                     });

    covers = queue;
    runNextCover(queue);
}

std::shared_ptr<ConsoleShelfView::CoverQueue> ConsoleShelfView::newCoverQueue() {
    auto queue = std::make_shared<CoverQueue>();
    queue->console = &consoleRef;
    queue->where = paths;
    queue->generation = generation;
    queue->view = this;
    queue->alive = alive;
    return queue;
}

void ConsoleShelfView::retryCover(size_t index) {
    const Game& game = games[index];
    if (launching || game.hasCover || !game.hasData || game.meta.canonical.empty())
        return;
    // Only worth it when the data brought a name that was not tried yet.
    if (emu::thumbNameCandidates(game.meta.canonical, game.rom.title).size() <=
        emu::coverNameCandidates(game.rom.title).size())
        return;

    emu::clearCoverMiss(paths, game.rom.title);
    games[index].missed = false;
    indexDirty = true;
    const CoverQueue::Job job{index, game.rom.title, game.meta.canonical,
                              emu::coverPath(paths, game.rom.title)};
    if (covers && !covers->halted && covers->next < covers->jobs.size()) {
        // Waiting in the running queue: it just learned the name. The job
        // at `next` may already be on the pool with the old one, so that
        // game goes again at the end.
        for (size_t i = covers->next + (covers->inFlight ? 1 : 0); i < covers->jobs.size(); i++) {
            if (covers->jobs[i].index == index) {
                covers->jobs[i].canonical = job.canonical;
                return;
            }
        }
        covers->jobs.push_back(job);
        return;
    }
    stopCovers();
    auto queue = newCoverQueue();
    queue->jobs.push_back(job);
    covers = queue;
    runNextCover(queue);
}

// UI thread: hands the next cover to the pool.
void ConsoleShelfView::runNextCover(std::shared_ptr<CoverQueue> queue) {
    if (queue->stop.load() || !*queue->alive)
        return;
    if (queue->next >= queue->jobs.size()) {
        queue->view->saveIndex();
        return;
    }
    // A game's data waits for the same network gate and polls it less often
    // than covers take it: covers give way until the data queue is idle.
    if (queue->view->dataQueue && queue->view->dataQueue->running()) {
        brls::delay(kBusyRetryMs, [queue] { runNextCover(queue); });
        return;
    }
    queue->running = queue->jobs[queue->next];
    queue->inFlight = true;
    brls::async([queue] { fetchOneCover(queue); });
}

// Pool thread: one cover, every name it may have on libretro.
void ConsoleShelfView::fetchOneCover(std::shared_ptr<CoverQueue> queue) {
    if (queue->stop.load())
        return;
    // Another shelf's cover, a game's data or a launch has the network: try
    // again soon, without holding this worker.
    if (!emu::netTryAcquire()) {
        brls::delay(kBusyRetryMs, [queue] {
            queue->inFlight = false;
            runNextCover(queue);
        });
        return;
    }
    if (queue->stop.load()) {
        emu::netRelease();
        return;
    }

    const CoverQueue::Job job = queue->running;
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
              : emu::thumbNameCandidates(job.canonical, job.title);
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
    const bool missed = !found && !transient && !diskError;
    if (missed)
        emu::markCoverMissed(queue->where, job.title, APP_VERSION);
    emu::netRelease();

    if (diskError)
        emu::liblog::bug("cover could not be saved: {}", job.path);
    brls::sync([queue, job, found, transient, missed] {
        if (!*queue->alive)
            return;
        if (transient)
            emu::liblog::note("cover downloads halted at '{}' (no answer or server error)",
                              job.title);
        if (found)
            queue->view->coverArrived(queue->generation, job.index, job.path);
        if (missed)
            queue->view->coverMissedOn(queue->generation, job.index);
        queue->inFlight = false;
        if (transient) {
            queue->halted = true; // generic covers; tried again next time
            queue->view->saveIndex();
            return;
        }
        queue->next++;
        runNextCover(queue);
    });
}

void ConsoleShelfView::coverArrived(uint64_t forGeneration, size_t index,
                                    const std::string& path) {
    if (forGeneration != generation || index >= games.size())
        return;
    // The same game can be fetched twice (its data brought a new name); the
    // cell takes one cover only.
    if (games[index].hasCover)
        return;
    games[index].hasCover = true;
    indexDirty = true;
    listPanel->refreshRow(index);
    if (current == index)
        applyAmbient();
}

void ConsoleShelfView::coverMissedOn(uint64_t forGeneration, size_t index) {
    if (forGeneration != generation || index >= games.size())
        return;
    games[index].missed = true;
    indexDirty = true;
}

void ConsoleShelfView::draw(NVGcontext* vg, float x, float y, float width,
                            float height, Style style, FrameContext* ctx) {
    // Without a BackdropFrame the glow stays inside the content area.
    ambient.drawFallback(vg, x, y, width, height);
    Box::draw(vg, x, y, width, height, style, ctx);
}

#endif // PLATFORM_SWITCH
