//
//  game_list_panel.cpp
//  Equinox
//

#include "game_list_panel.hpp"

#if defined(EQX_EMU_UI)

#include "Settings.hpp"
#include "bold_label.hpp"
#include "emu/LibraryLog.hpp"
#include "library_widgets.hpp"

#include <algorithm>
#include <cmath>

using namespace brls;
using namespace brls::literals;

// Scrolls to keep the focused game in the middle, and can do it again once
// the cartridge has finished growing.
class LibraryScroll : public ScrollingFrame {
  public:
    LibraryScroll() {
        setScrollingBehavior(ScrollingBehavior::CENTERED);
        setScrollingIndicatorVisible(false);
        setGrow(1);
    }

    void recenter(bool animated) {
        if (Application::getInputType() != InputType::TOUCH)
            updateScrolling(animated);
    }
    void recenterNextFrame() { updateScrollingOnNextFrame = true; }

    void centerOn(float middle) {
        if (Application::getInputType() == InputType::TOUCH)
            return;
        const float limit = std::max(0.0f, getContentHeight() - getHeight());
        setContentOffsetY(std::clamp(middle - getHeight() / 2, 0.0f, limit), true);
    }
};

// The rows stand at positions the panel works out, in no particular child
// order, so moving between them asks the panel.
class LibraryList : public Box {
  public:
    using NextRow = std::function<View*(View*, FocusDirection)>;
    using DefaultRow = std::function<View*()>;

    LibraryList(NextRow next, DefaultRow first)
        : Box(Axis::COLUMN), nextRow(std::move(next)), defaultRow(std::move(first)) {}

    View* getNextFocus(FocusDirection direction, View* currentView) override {
        View* next = nullptr;
        if (direction == FocusDirection::UP || direction == FocusDirection::DOWN)
            next = nextRow(currentView, direction);
        next = getParentNavigationDecision(this, next, direction);
        if (!next && hasParent())
            next = getParent()->getNextFocus(direction, this);
        return next;
    }

    View* getDefaultFocus() override {
        return getVisibility() == Visibility::VISIBLE ? defaultRow() : nullptr;
    }

  private:
    NextRow nextRow;
    DefaultRow defaultRow;
};

namespace {
constexpr float kPanelWidth = 384;
// Enough to fill the column twice over, so the next row is always ready.
constexpr size_t kPoolSize = 24;
constexpr float kRowGap = 2;
constexpr float kRowStep = GameRowCell::RowHeight + kRowGap;
// Of the column, in mode 0; the neighbours share the rest.
constexpr float kCartShare = 0.52f;
constexpr float kMinTapeRow = 56;
constexpr float kListTop = 4;
// Games per page: the cartridge view shows about three, the list a column's worth
// (kListPage until the column has a height).
constexpr size_t kCartPage = 3;
constexpr size_t kListPage = 10;
constexpr float kListSide = 6;
constexpr float kRowWidth = kPanelWidth - 16 - 24 - 2 * kListSide;
constexpr size_t kNoPlace = GameRowCell::NoGame;
constexpr float kControl = 44;
constexpr float kRowHeight = 48;
constexpr float kRadius = 10;
constexpr int kRecenterMs = 240;
constexpr int kRecenterStillMs = 50;
constexpr int kSortCount = 5;

// material "memory", "view_list"
const char* const kCartGlyph = "\xEE\x8C\xA2";
const char* const kListGlyph = "\xEE\xA3\xAF";

NVGcolor themeColor(const char* name) {
    return Application::getTheme().getColor(name);
}

bool isInside(View* view, View* ancestor) {
    for (View* v = view; v; v = v->hasParent() ? v->getParent() : nullptr)
        if (v == ancestor)
            return true;
    return false;
}

// A focusable box that runs action on A and on a tap.
Box* touchBox(Axis axis, std::function<void()> action) {
    auto* box = new Box(axis);
    box->setFocusable(true);
    box->setAlignItems(AlignItems::CENTER);
    box->addGestureRecognizer(new TapGestureRecognizer(box));
    box->registerClickAction([action](View*) {
        action();
        return true;
    });
    return box;
}

Label* iconLabel(const char* glyph, float size) {
    auto* label = new IconLabel();
    label->setFontSize(size);
    label->setText(glyph);
    label->setShrink(0);
    return label;
}

Label* textLabel(float size) {
    auto* label = new Label();
    label->setFontSize(size);
    label->setSingleLine(true);
    label->setAutoAnimate(false);
    return label;
}

Button* secondaryButton(const std::string& caption, std::function<void()> action) {
    auto* button = new Button();
    button->setStyle(&BUTTONSTYLE_DEFAULT);
    button->setText(caption);
    button->setFontSize(15);
    button->setHeight(kControl);
    button->setShrink(0);
    button->setBackgroundColor(themeColor("equinox/card_inner"));
    button->registerClickAction([action](View*) {
        action();
        return true;
    });
    return button;
}

namespace liblog = emu::liblog;

constexpr int64_t kSecondUs = 1000000;
// Under 25 fps.
constexpr int64_t kSlowFrameUs = 40000;
constexpr int kSlowLinesPerSecond = 5;
constexpr int64_t kIdleSummaryUs = 10 * kSecondUs;
// The cartridge can push the middle of the column this many rows away.
constexpr long kOffWindowMargin = 4;

// One list is on screen at a time, and only the UI thread touches this.
struct PanelStats {
    int64_t lastDraw = 0, tickStart = 0;
    int frames = 0, slow = 0, rebinds = 0, moves = 0;
    int64_t sumGap = 0, maxGap = 0, maxWindow = 0, maxDraw = 0;
    int64_t bindTotal = 0, maxBind = 0;
    int frameRebinds = 0, prevRebinds = 0;
    int64_t prevWindow = 0, prevDraw = 0;
} stats;

std::string sortName(int sort) {
    switch (sort) {
    case 1:
        return "library/sort/name_desc"_i18n;
    case 2:
        return "library/sort/year_asc"_i18n;
    case 3:
        return "library/sort/year_desc"_i18n;
    case 4:
        return "library/sort/genre"_i18n;
    default:
        return "library/sort/name_asc"_i18n;
    }
}

} // namespace

GameListPanel::GameListPanel(const emu::Console& console)
    : Box(Axis::COLUMN), sort(Settings::instance().library_sort()),
      mode(Settings::instance().library_view()), consoleName(console.name) {
    setWidth(kPanelWidth);
    setShrink(0);
    setPadding(18, 16, 12, 24);

    // Header: name and count, then the mode toggle.
    auto* header = new Box(Axis::ROW);
    header->setAlignItems(AlignItems::CENTER);
    header->setJustifyContent(JustifyContent::SPACE_BETWEEN);
    header->setPadding(0, 4, 6, 4);
    header->setShrink(0);
    addView(header);

    auto* titles = new Box(Axis::COLUMN);
    titles->setGrow(1);
    titles->setShrink(1);
    auto* nameLabel = new BoldLabel();
    nameLabel->setFontSize(15);
    nameLabel->setSingleLine(true);
    nameLabel->setAutoAnimate(false);
    nameLabel->setText(consoleName);
    titles->addView(nameLabel);
    countLabel = textLabel(14);
    countLabel->setMarginTop(2);
    titles->addView(countLabel);
    header->addView(titles);

    auto* toggle = new Box(Axis::ROW);
    toggle->setPadding(2, 2, 2, 2);
    toggle->setCornerRadius(12);
    toggle->setBackgroundColor(themeColor("equinox/card_inner"));
    toggle->setShrink(0);
    const auto makeToggle = [&](const char* glyph, int view, Label*& icon) {
        Box* button = touchBox(Axis::ROW, [this, view] {
            if (mode != view)
                toggleMode();
        });
        button->setWidth(kControl);
        button->setHeight(kControl);
        button->setJustifyContent(JustifyContent::CENTER);
        button->setCornerRadius(kRadius);
        button->setHighlightCornerRadius(kRadius + 3);
        icon = iconLabel(glyph, 20);
        button->addView(icon);
        toggle->addView(button);
        return button;
    };
    cartButton = makeToggle(kCartGlyph, 0, cartIcon);
    listButton = makeToggle(kListGlyph, 1, listIcon);
    listButton->setMarginLeft(2);
    header->addView(toggle);

    // The games.
    noneLabel = new Label();
    noneLabel->setFontSize(15);
    noneLabel->setMargins(20, 4, 0, 4);
    noneLabel->setShrink(0);
    noneLabel->setVisibility(Visibility::GONE);
    addView(noneLabel);

    scroll = new LibraryScroll();
    list = new LibraryList(
        [this](View* from, FocusDirection direction) { return rowNextTo(from, direction); },
        [this] { return defaultRow(); });
    for (size_t i = 0; i < kPoolSize; i++) {
        auto* cell = new GameRowCell();
        cell->detach();
        cell->setWidth(kRowWidth);
        cell->setVisibility(Visibility::GONE);
        cell->onFocused = [this, cell] { focusedGame(cell->index()); };
        list->addView(cell);
        pool.push_back(cell);
    }
    scroll->setContentView(list);
    addView(scroll);

    // Under the games: L, "Page X of Y", R. The shoulders turn the pages; the
    // app's footer leaves them out.
    pageRow = new Box(Axis::ROW);
    pageRow->setJustifyContent(JustifyContent::CENTER);
    pageRow->setAlignItems(AlignItems::CENTER);
    pageRow->setMarginTop(6);
    pageRow->setShrink(0);
    pageRow->setVisibility(Visibility::GONE);
    const auto pageKey = [&](ControllerButton button, int direction, Label*& key) {
        Box* box = touchBox(Axis::ROW, [this, direction] { turnPage(direction); });
        box->setFocusable(false); // a tap target; on the controller, L and R
        box->setPadding(2, 6, 2, 6);
        key = new Label();
        key->setFontSize(18);
        key->setText(Hint::getKeyIcon(button));
        box->addView(key);
        return box;
    };
    pageRow->addView(pageKey(BUTTON_LB, -1, pagePrevKey));
    pageLabel = textLabel(14);
    pageLabel->setMargins(0, 10, 0, 10);
    pageRow->addView(pageLabel);
    pageRow->addView(pageKey(BUTTON_RB, 1, pageNextKey));
    addView(pageRow);

    // While the games' data downloads, or after it stopped: its progress, or
    // why it stopped. Starting it is in More options (and ZR).
    separator = new Rectangle();
    separator->setHeight(1);
    separator->setMarginTop(8);
    separator->setShrink(0);
    addView(separator);

    footer = new Box(Axis::COLUMN);
    footer->setPadding(12, 0, 0, 0);
    footer->setShrink(0);
    addView(footer);

    stoppedRow = new Box(Axis::ROW);
    stoppedRow->setAlignItems(AlignItems::CENTER);
    stoppedLabel = new Label();
    stoppedLabel->setFontSize(14);
    stoppedLabel->setGrow(1);
    stoppedLabel->setShrink(1);
    stoppedLabel->setMarginRight(10);
    stoppedRow->addView(stoppedLabel);
    retryButton = secondaryButton("library/details/retry"_i18n, [this] {
        if (onRetry)
            onRetry();
    });
    stoppedRow->addView(retryButton);
    footer->addView(stoppedRow);

    runningRow = new Box(Axis::COLUMN);
    auto* runningLineBox = new Box(Axis::ROW);
    runningLineBox->setAlignItems(AlignItems::CENTER);
    auto* runningText = new Box(Axis::COLUMN);
    runningText->setGrow(1);
    runningText->setShrink(1);
    runningText->setMarginRight(10);
    runningLine = new BoldLabel();
    runningLine->setFontSize(15);
    runningLine->setSingleLine(true);
    runningLine->setAutoAnimate(false);
    runningText->addView(runningLine);
    runningName = textLabel(14);
    runningName->setMarginTop(2);
    runningText->addView(runningName);
    runningLineBox->addView(runningText);
    cancelButton = secondaryButton("common/cancel"_i18n, [this] {
        if (onCancel)
            onCancel();
    });
    runningLineBox->addView(cancelButton);
    runningRow->addView(runningLineBox);
    bar = new LibraryProgressBar();
    bar->setMarginTop(8);
    runningRow->addView(bar);
    footer->addView(runningRow);

    registerShortcuts(false);

    applyColors();
    applyToggle();
    applyCount();
    applyBatch();
}

GameListPanel::~GameListPanel() {
    *alive = false;
}

void GameListPanel::willAppear(bool resetState) {
    Box::willAppear(resetState);
    applyColors();
}

void GameListPanel::applyColors() {
    const NVGcolor muted = themeColor("equinox/muted");
    countLabel->setTextColor(muted);
    pageLabel->setTextColor(muted);
    pagePrevKey->setTextColor(muted);
    pageNextKey->setTextColor(muted);
    runningName->setTextColor(muted);
    noneLabel->setTextColor(muted);
    separator->setColor(themeColor("equinox/card"));
    retryButton->setBackgroundColor(themeColor("equinox/card_inner"));
    cancelButton->setBackgroundColor(themeColor("equinox/card_inner"));
    applyToggle();
}

void GameListPanel::applyCount() {
    const size_t total = items.size();
    std::string text;
    if (scanning)
        text = "emu/scanning"_i18n;
    else if (shown.size() != total)
        text = brls::getStr("library/list/count_filtered", shown.size(), total);
    else if (total == 1)
        text = "library/list/count_one"_i18n;
    else
        text = brls::getStr("library/list/count_many", total);
    if (!scanning && !query.empty())
        text += "  ·  “" + query + "”";
    countLabel->setText(text);
    applyPage();
}

size_t GameListPanel::pageSize() const {
    if (mode == 0)
        return kCartPage;
    const float column = scroll->getHeight();
    if (column <= 0)
        return kListPage;
    return std::max<size_t>(1, static_cast<size_t>(column / kRowStep));
}

size_t GameListPanel::currentPlace() const {
    if (current && *current < shownAt.size() && shownAt[*current] != kNoPlace)
        return shownAt[*current];
    return 0;
}

void GameListPanel::applyPage() {
    const size_t size = pageSize();
    shownPageSize = size;
    const size_t pages = (shown.size() + size - 1) / size;
    if (scanning || pages <= 1) {
        pageRow->setVisibility(Visibility::GONE);
        return;
    }
    pageLabel->setText(brls::getStr("library/list/page", currentPlace() / size + 1, pages));
    pageRow->setVisibility(Visibility::VISIBLE);
}

void GameListPanel::turnPage(int direction) {
    if (shown.empty())
        return;
    const size_t size = pageSize();
    const size_t page = currentPlace() / size;
    size_t place = 0;
    if (direction > 0)
        place = std::min((page + 1) * size, shown.size() - 1);
    else if (page > 0)
        place = (page - 1) * size;
    focusGame(shown[place]);
}

void GameListPanel::setScanning(bool on) {
    scanning = on;
    applyCount();
}

void GameListPanel::applyToggle() {
    const auto style = [](Box* button, Label* icon, bool active) {
        button->setBackgroundColor(active ? themeColor("brls/accent")
                                          : nvgRGBA(0, 0, 0, 0));
        icon->setTextColor(active ? themeColor("equinox/pill_text")
                                  : themeColor("equinox/muted"));
    };
    style(cartButton, cartIcon, mode == 0);
    style(listButton, listIcon, mode == 1);
}

void GameListPanel::setLook(int view) {
    mode = view == 1 ? 1 : 0;
    applyToggle();
    applyPage();
    applyLooks(true);
    if (focusInList())
        scheduleRecenter();
}

void GameListPanel::toggleMode() {
    setLook(mode == 0 ? 1 : 0);
    Settings::instance().set_library_view(mode);
    Settings::instance().save();
}

emu::PreparedOrder GameListPanel::orderRequest() const {
    emu::PreparedOrder request;
    request.query = query;
    request.sort = static_cast<emu::LibrarySort>(sort);
    return request;
}

void GameListPanel::setItems(std::vector<emu::LibraryItem> nextItems,
                             emu::PreparedOrder prepared) {
    const int64_t started = liblog::nowUs();
    // The old rows are about to go; the focus waits on the mode toggle.
    if (focusInList())
        Application::giveFocus(cartButton);
    for (GameRowCell* cell : pool)
        cell->release();
    live.clear();
    const bool hadCurrent = current.has_value();
    current.reset();
    shown.clear();

    items = std::move(nextItems);
    const bool stillValid = prepared.query == query &&
                            prepared.sort == static_cast<emu::LibrarySort>(sort);
    applyOrder(true, stillValid ? &prepared.shown : nullptr);
    // Nothing to pick in the new set; applyOrder only reports a first result.
    if (hadCurrent && !current && onFocusGame)
        onFocusGame(std::nullopt);
    liblog::note("setItems: {} games, {} shown, prepared order {}, {} ms", items.size(),
                 shown.size(), stillValid ? "used" : "discarded",
                 (liblog::nowUs() - started) / 1000);
}

void GameListPanel::setItem(size_t index, emu::LibraryItem item) {
    if (index >= items.size())
        return;
    const emu::LibraryItem& old = items[index];
    const bool sameOrder =
        old.name == item.name && old.year == item.year && old.genre == item.genre;
    items[index] = std::move(item);
    refreshRow(index);
    if (sameOrder)
        return;
    if (orderHeld) {
        orderStale = true;
        return;
    }
    // Only the data changed: the list stays where the user left it.
    applyOrder(false);
}

void GameListPanel::holdOrder(bool hold) {
    orderHeld = hold;
    if (hold || !orderStale)
        return;
    orderStale = false;
    applyOrder(false);
}

void GameListPanel::refreshRow(size_t index) {
    for (GameRowCell* cell : live)
        if (cell->index() == index && bindRow)
            bindRow(*cell, index);
}

void GameListPanel::refreshRows() {
    if (!bindRow)
        return;
    for (GameRowCell* cell : live)
        bindRow(*cell, cell->index());
}

void GameListPanel::applyOrder(bool resetScroll, std::vector<size_t>* ready) {
    orderStale = false; // whatever setItem held back is in this order too
    const std::optional<size_t> focused = focusedIndex();
    const int64_t started = liblog::nowUs();
    if (ready) {
        shown = std::move(*ready);
    } else {
        shown = emu::libraryOrder(items, query, static_cast<emu::LibrarySort>(sort));
        liblog::note("order on UI thread: {} of {} games, sort {}, query '{}', {} ms",
                     shown.size(), items.size(), sort, query,
                     (liblog::nowUs() - started) / 1000);
    }

    shownAt.assign(items.size(), kNoPlace);
    for (size_t place = 0; place < shown.size(); place++)
        shownAt[shown[place]] = place;
    const auto visible = [&](size_t index) { return shownAt[index] != kNoPlace; };

    const bool none = shown.empty() && !query.empty();
    noneLabel->setText(brls::getStr("library/list/none", query));
    noneLabel->setVisibility(none ? Visibility::VISIBLE : Visibility::GONE);
    applyCount();

    const bool focusLeaves = focused && !visible(*focused);
    if (resetScroll && !focused)
        scroll->setContentOffsetY(0, false);
    windowDirty = true;
    fillWindow(focusLeaves ? 0 : anchorPlace());

    // The game that leaves the filter hands the focus to the first result.
    if (focusLeaves) {
        if (GameRowCell* first = rowAt(0))
            Application::giveFocus(first);
        else
            Application::giveFocus(cartButton);
    }
    // Focus on the mode toggle: the current game still follows the filter.
    std::optional<size_t> next = current;
    if (next && !visible(*next))
        next.reset();
    if (!next && !shown.empty())
        next = shown.front();
    setCurrent(next, false);
    applyLooks(false);
    placeRows();

    if (focusInList())
        scroll->recenterNextFrame();
    else if (resetScroll)
        scroll->setContentOffsetY(0, false);
    applyBatch();
}

RowLook GameListPanel::lookOf(size_t index) const {
    if (mode != 0)
        return RowLook::Name;
    return current && *current == index ? RowLook::Cartridge : RowLook::Tape;
}

float GameListPanel::step() const {
    return mode == 0 ? tapeRow : kRowStep;
}

float GameListPanel::extra() const {
    return mode == 0 ? cartRow - tapeRow : 0;
}

// The column's height decides the rows' in mode 0: three games fill it.
void GameListPanel::applySizes() {
    const float column = scroll->getHeight();
    if (column <= 0 || std::isnan(column))
        return;
    const float cart = std::round(column * kCartShare);
    const float tape = std::max(kMinTapeRow, std::floor((column - cart) / 2));
    if (cart == cartRow && tape == tapeRow)
        return;
    cartRow = cart;
    tapeRow = tape;
    for (GameRowCell* cell : pool)
        cell->setSizes(cartRow, tapeRow);
    if (focusInList())
        scroll->recenterNextFrame();
}

void GameListPanel::applyLooks(bool animate) {
    for (GameRowCell* cell : live)
        cell->setLook(lookOf(cell->index()), animate);
}

size_t GameListPanel::anchorPlace() {
    if (shown.empty())
        return 0;
    if (const auto focused = focusedIndex(); focused && shownAt[*focused] != kNoPlace)
        return shownAt[*focused];
    // Dragged by touch, nothing is focused: follow the middle of the column.
    float middle = scroll->getContentOffsetY() + scroll->getHeight() / 2 - kListTop;
    if (mode == 0 && current && shownAt[*current] != kNoPlace &&
        middle > shownAt[*current] * step() + extra())
        middle -= extra();
    if (middle <= 0)
        return 0;
    return std::min(static_cast<size_t>(middle / step()), shown.size() - 1);
}

// Rows that still belong in the window stay as they are, the focused one
// among them; only the ones that fell out take another game.
void GameListPanel::fillWindow(size_t anchor) {
    const size_t count = std::min(kPoolSize, shown.size());
    size_t start = anchor > kPoolSize / 2 ? anchor - kPoolSize / 2 : 0;
    start = std::min(start, shown.size() - count);
    if (!windowDirty && start == windowStart && live.size() == count)
        return;
    windowDirty = false;
    windowStart = start;

    live.assign(count, nullptr);
    std::vector<GameRowCell*> spare;
    for (GameRowCell* cell : pool) {
        const size_t index = cell->index();
        const size_t place = index < shownAt.size() ? shownAt[index] : kNoPlace;
        if (place != kNoPlace && place >= start && place < start + count) {
            if (live[place - start])
                liblog::bug("two rows hold game {} at place {}", index, place);
            live[place - start] = cell;
        } else {
            spare.push_back(cell);
        }
    }
    for (size_t slot = 0; slot < count; slot++) {
        if (live[slot])
            continue;
        GameRowCell* cell = spare.back();
        spare.pop_back();
        const size_t index = shown[start + slot];
        cell->rebind(index, lookOf(index));
        if (bindRow) {
            const int64_t before = liblog::nowUs();
            bindRow(*cell, index);
            const int64_t took = liblog::nowUs() - before;
            stats.bindTotal += took;
            stats.maxBind = std::max(stats.maxBind, took);
        }
        stats.rebinds++;
        stats.frameRebinds++;
        live[slot] = cell;
    }
    for (GameRowCell* cell : spare)
        if (cell->index() != GameRowCell::NoGame)
            cell->release();
    checkRows();
}

void GameListPanel::checkRows() const {
    if (shownAt.size() != items.size())
        liblog::bug("shownAt has {} entries for {} items", shownAt.size(), items.size());
    if (live.size() != std::min(kPoolSize, shown.size()) ||
        windowStart + live.size() > shown.size())
        liblog::bug("window start {} with {} rows for {} shown", windowStart, live.size(),
                    shown.size());
    for (size_t slot = 0; slot < live.size() && windowStart + slot < shown.size(); slot++) {
        if (!live[slot])
            liblog::bug("empty row at slot {} (start {})", slot, windowStart);
        else if (live[slot]->index() != shown[windowStart + slot])
            liblog::bug("slot {} holds game {} but place {} is game {}", slot,
                        live[slot]->index(), windowStart + slot, shown[windowStart + slot]);
    }
    if (current && *current >= items.size())
        liblog::bug("current game {} of {}", *current, items.size());
}

// Not from fillWindow: a filter hides the focused row for a moment there.
void GameListPanel::checkFocusAndView() const {
    auto* cell = dynamic_cast<GameRowCell*>(Application::getCurrentFocus());
    if (cell && std::find(pool.begin(), pool.end(), cell) != pool.end()) {
        const size_t index = cell->index();
        if (index == GameRowCell::NoGame)
            liblog::bug("focus is on a released row");
        else if (index >= shownAt.size() || shownAt[index] == kNoPlace)
            liblog::bug("focus is on game {}, which is not shown", index);
        else if (!rowAt(shownAt[index]))
            liblog::bug("focused game {} at place {} is outside the window {}+{}", index,
                        shownAt[index], windowStart, live.size());
    }
    if (live.empty())
        return;
    const long middle = static_cast<long>(
        (scroll->getContentOffsetY() + scroll->getHeight() / 2 - kListTop) / step());
    const long first = static_cast<long>(windowStart);
    const long last = first + static_cast<long>(live.size());
    if (middle + kOffWindowMargin < first || middle > last + kOffWindowMargin)
        liblog::bug("blank list: screen is at row {}, rows exist for {}..{}", middle, first,
                    last);
}

// A frame's duration is only known when the next one starts, so a slow gap
// is reported with the work of the frame before it.
void GameListPanel::measureFrame(int64_t start, int64_t windowUs, int64_t drawUs) {
    const int64_t gap = start - stats.lastDraw;
    if (gap > kSecondUs) {
        // Off screen until now: this gap says nothing about the list.
        stats = PanelStats{};
        stats.lastDraw = stats.tickStart = start;
        return;
    }
    stats.lastDraw = start;
    stats.frames++;
    stats.sumGap += gap;
    stats.maxGap = std::max(stats.maxGap, gap);
    stats.maxWindow = std::max(stats.maxWindow, windowUs);
    stats.maxDraw = std::max(stats.maxDraw, drawUs);
    const long place = current && *current < shownAt.size() && shownAt[*current] != kNoPlace
                           ? static_cast<long>(shownAt[*current])
                           : -1;
    if (gap > kSlowFrameUs && stats.slow++ < kSlowLinesPerSecond)
        liblog::note("slow frame {:.1f} ms: window {} us, panel draw {} us, {} rebinds, "
                     "place {}/{}",
                     gap / 1000.0, stats.prevWindow, stats.prevDraw, stats.prevRebinds, place,
                     shown.size());
    stats.prevWindow = windowUs;
    stats.prevDraw = drawUs;
    stats.prevRebinds = stats.frameRebinds;
    stats.frameRebinds = 0;

    const bool active = stats.rebinds > 0 || stats.moves > 0 || stats.slow > 0;
    if (start - stats.tickStart < (active ? kSecondUs : kIdleSummaryUs))
        return;

    checkFocusAndView();
    const int decoded = liblog::covers.decoded.exchange(0);
    const int64_t decodeTotal = liblog::covers.decodeTotalUs.exchange(0);
    liblog::note("fps {:.0f} (worst {:.1f} ms, {} slow) | moves {} rebinds {} bind avg {} max {} us "
                 "| window max {} us, panel draw max {} us | covers asked {} cached {} decoded {} "
                 "(avg {} ms, max {} ms) dropped {} upload max {} us | place {}/{} window {} | "
                 "ram {} MB",
                 stats.frames * 1e6 / stats.sumGap, stats.maxGap / 1000.0, stats.slow, stats.moves,
                 stats.rebinds, stats.rebinds ? stats.bindTotal / stats.rebinds : 0, stats.maxBind,
                 stats.maxWindow, stats.maxDraw, liblog::covers.asked.exchange(0),
                 liblog::covers.cacheHits.exchange(0), decoded,
                 decoded ? decodeTotal / decoded / 1000 : 0,
                 liblog::covers.decodeMaxUs.exchange(0) / 1000, liblog::covers.dropped.exchange(0),
                 liblog::covers.uploadMaxUs.exchange(0), place, shown.size(), windowStart,
                 liblog::usedMemoryMb());
    liblog::flushSoon();
    const int64_t lastDraw = stats.lastDraw;
    stats = PanelStats{};
    stats.lastDraw = lastDraw;
    stats.tickStart = start;
}

// Every frame while the cartridge eases, the rows under it follow.
void GameListPanel::placeRows() {
    const std::optional<size_t> cartridge =
        mode == 0 && current && shownAt[*current] != kNoPlace
            ? std::optional<size_t>(shownAt[*current])
            : std::nullopt;
    const float gap = mode == 0 ? 0 : kRowGap;
    float y = kListTop + windowStart * step();
    if (cartridge && *cartridge < windowStart)
        y += extra();
    for (GameRowCell* cell : live) {
        cell->setDetachedPosition(kListSide, y);
        y += cell->currentHeight() + gap;
    }
    const size_t after = windowStart + live.size();
    y += (shown.size() - after) * step() + kListTop;
    if (cartridge && *cartridge >= after)
        y += extra();
    if (std::abs(list->getHeight() - y) > 0.5f)
        list->setHeight(y);
}

GameRowCell* GameListPanel::rowAt(size_t place) const {
    if (place < windowStart || place - windowStart >= live.size())
        return nullptr;
    return live[place - windowStart];
}

View* GameListPanel::rowNextTo(View* from, FocusDirection direction) const {
    auto* cell = dynamic_cast<GameRowCell*>(from);
    if (!cell || cell->index() >= shownAt.size() || shownAt[cell->index()] == kNoPlace)
        return nullptr;
    const size_t place = shownAt[cell->index()];
    const bool up = direction == FocusDirection::UP;
    if (up ? place == 0 : place + 1 >= shown.size())
        return nullptr;
    GameRowCell* next = rowAt(up ? place - 1 : place + 1);
    if (!next)
        liblog::bug("navigation stuck at place {}: next row is outside the window {}+{}", place,
                    windowStart, live.size());
    return next;
}

View* GameListPanel::defaultRow() const {
    // The scroll asks while the focus is arriving, before current has followed.
    if (const auto focused = focusedIndex(); focused && shownAt[*focused] != kNoPlace)
        if (GameRowCell* cell = rowAt(shownAt[*focused]))
            return cell;
    if (current && *current < shownAt.size())
        if (GameRowCell* cell = rowAt(shownAt[*current]))
            return cell;
    return live.empty() ? nullptr : live[live.size() / 2];
}

void GameListPanel::draw(NVGcontext* vg, float x, float y, float width, float height,
                         Style style, FrameContext* ctx) {
    const int64_t start = liblog::nowUs();
    if (mode == 0)
        applySizes();
    fillWindow(anchorPlace());
    placeRows();
    if (pageSize() != shownPageSize) // the column got its height, or a new one
        applyPage();
    const int64_t placed = liblog::nowUs();
    Box::draw(vg, x, y, width, height, style, ctx);
    measureFrame(start, placed - start, liblog::nowUs() - placed);
}

// The one place where the current game changes; onFocusGame fires only then.
void GameListPanel::setCurrent(std::optional<size_t> next, bool animate) {
    if (next == current)
        return;
    current = next;
    applyLooks(animate);
    applyPage();
    scheduleRecenter();
    if (onFocusGame)
        onFocusGame(current);
}

void GameListPanel::focusedGame(size_t index) {
    stats.moves++;
    setCurrent(index, true);
    if (index >= shownAt.size() || shownAt[index] == kNoPlace)
        return;
    // Aim at where the row ends up once the heights settle, in one movement:
    // the focused row is the only cartridge, so every row above it is plain.
    recenterToken++;
    const float height = mode == 0 ? cartRow : GameRowCell::RowHeight;
    scroll->centerOn(kListTop + shownAt[index] * step() + height / 2);
}

void GameListPanel::focusGame(size_t index) {
    if (shown.empty()) {
        Application::giveFocus(cartButton);
        setCurrent(std::nullopt, true);
        return;
    }
    const size_t place =
        index < shownAt.size() && shownAt[index] != kNoPlace ? shownAt[index] : 0;
    fillWindow(place);
    placeRows();
    GameRowCell* target = rowAt(place);
    Application::giveFocus(target);
    // Already focused, or the focus not yet on screen: the cell may not report it.
    setCurrent(target->index(), true);
}

// The rows keep growing and shrinking for a moment after the focus moved.
void GameListPanel::scheduleRecenter() {
    if (mode != 0)
        return;
    const unsigned token = ++recenterToken;
    std::shared_ptr<bool> token_alive = alive;
    const bool still = Settings::instance().reduce_motion();
    brls::delay(still ? kRecenterStillMs : kRecenterMs, [this, token_alive, token] {
        if (*token_alive && token == recenterToken && focusInList())
            scroll->recenter(true);
    });
}

bool GameListPanel::focusInList() const {
    return isInside(Application::getCurrentFocus(), list);
}

std::optional<size_t> GameListPanel::focusedIndex() const {
    auto* cell = dynamic_cast<GameRowCell*>(Application::getCurrentFocus());
    if (!cell || cell->index() >= items.size() ||
        std::find(pool.begin(), pool.end(), cell) == pool.end())
        return std::nullopt;
    return cell->index();
}

void GameListPanel::openSearch() {
    std::shared_ptr<bool> token = alive;
    Application::getImeManager()->openForText(
        [this, token](const std::string& text) {
            if (*token)
                setQuery(text);
        },
        "library/list/search"_i18n, "", 64, query, 0);
}

void GameListPanel::setQuery(const std::string& text) {
    if (text == query)
        return;
    query = text;
    applyOrder(true);
}

void GameListPanel::openSort() {
    std::vector<std::string> names;
    for (int i = 0; i < kSortCount; i++)
        names.push_back(sortName(i));
    std::shared_ptr<bool> token = alive;
    auto* dropdown = new Dropdown(
        "library/list/sort_title"_i18n, names,
        [this, token](int selected) {
            if (*token)
                chooseSort(selected);
        },
        sort);
    Application::pushActivity(new Activity(dropdown));
}

void GameListPanel::chooseSort(int value) {
    if (value < 0 || value >= kSortCount || value == sort)
        return;
    sort = value;
    Settings::instance().set_library_sort(sort);
    Settings::instance().save();
    applyOrder(true);
}

void GameListPanel::downloadAllOrCancel() {
    switch (batchState()) {
    case Batch::Running:
        if (onCancel)
            onCancel();
        break;
    case Batch::Idle:
        if (onDownloadAll)
            onDownloadAll();
        break;
    case Batch::Stopped:
        if (onRetry)
            onRetry();
        break;
    case Batch::Done:
        break;
    }
}

void GameListPanel::setBatch(bool isRunning, size_t done, size_t total, int percent,
                             const std::string& currentName, size_t withoutData) {
    running = isRunning;
    batchDone = done;
    batchTotal = total;
    batchPercent = std::clamp(percent, 0, 100);
    batchName = currentName;
    missing = withoutData;
    if (running)
        stopped.reset();
    applyBatch();
}

void GameListPanel::setBatchStopped(std::optional<emu::DataStop> stop) {
    if (stop && (*stop == emu::DataStop::None || *stop == emu::DataStop::Cancelled))
        stop.reset();
    stopped = stop;
    applyBatch();
}

GameListPanel::Batch GameListPanel::batchState() const {
    if (running)
        return Batch::Running;
    if (stopped)
        return Batch::Stopped;
    return missing == 0 ? Batch::Done : Batch::Idle;
}

void GameListPanel::registerShortcuts(bool hidden) {
    // Sort and view live in More options; the shoulders turn whole pages,
    // and the list shows them under the games instead of the app's footer.
    registerAction("library/list/page_prev"_i18n, BUTTON_LB, [this](View*) {
        turnPage(-1);
        return true;
    }, true);
    registerAction("library/list/page_next"_i18n, BUTTON_RB, [this](View*) {
        turnPage(1);
        return true;
    }, true);
    // Search is the one shortcut always shown in the footer: the list has no
    // search field of its own.
    registerAction("library/list/hint_search"_i18n, BUTTON_BACK, [this](View*) {
        openSearch();
        return true;
    });
    registerAction("library/list/download_all"_i18n, BUTTON_RT, [this](View*) {
        downloadAllOrCancel();
        return true;
    }, hidden);
}

void GameListPanel::hideShortcutHints() {
    registerShortcuts(true);
    applyBatch();
}

std::string GameListPanel::batchActionLabel() const {
    const Batch state = batchState();
    return state == Batch::Running   ? "common/cancel"_i18n
           : state == Batch::Stopped ? "library/details/retry"_i18n
                                     : "library/list/download_all"_i18n;
}

bool GameListPanel::batchActionAvailable() const {
    return batchState() != Batch::Done && !items.empty();
}

void GameListPanel::applyBatch() {
    const Batch state = batchState();
    const bool footerFocused = isInside(Application::getCurrentFocus(), footer);
    const Visibility area = items.empty() || state == Batch::Idle || state == Batch::Done
                                ? Visibility::GONE
                                : Visibility::VISIBLE;
    footer->setVisibility(area);
    separator->setVisibility(area);

    const auto shownWhen = [&](Batch b) {
        return state == b ? Visibility::VISIBLE : Visibility::GONE;
    };
    stoppedRow->setVisibility(shownWhen(Batch::Stopped));
    runningRow->setVisibility(shownWhen(Batch::Running));

    if (stopped)
        stoppedLabel->setText(brls::getStr("library/list/stopped", libraryStopText(*stopped)));
    if (state == Batch::Running) {
        runningLine->setText(brls::getStr("library/list/running",
                                          std::min(batchDone + 1, batchTotal), batchTotal,
                                          batchPercent));
        runningName->setText(batchName);
        static_cast<LibraryProgressBar*>(bar)->setFraction(batchPercent / 100.0f);
    }

    // ZR starts, cancels or retries; nothing to do once every game has data.
    updateActionHint(BUTTON_RT, batchActionLabel());
    setActionAvailable(BUTTON_RT, batchActionAvailable());

    // The control that had the focus may be the one that just went away.
    if (footerFocused) {
        View* now = Application::getCurrentFocus();
        const bool gone = !now || !now->isFocusable() ||
                          (isInside(now, stoppedRow) && state != Batch::Stopped) ||
                          (isInside(now, runningRow) && state != Batch::Running);
        if (gone) {
            View* target = state == Batch::Running   ? static_cast<View*>(runningRow)
                           : state == Batch::Stopped ? static_cast<View*>(stoppedRow)
                                                     : static_cast<View*>(scroll);
            Application::giveFocus(target);
        }
    }
}

#endif // EQX_EMU_UI
