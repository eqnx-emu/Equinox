//
//  game_list_panel.cpp
//  Equinox
//

#include "game_list_panel.hpp"

#if defined(PLATFORM_SWITCH)

#include "Settings.hpp"
#include "bold_label.hpp"
#include "library_widgets.hpp"

#include <algorithm>

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
};

namespace {
constexpr float kPanelWidth = 384;
constexpr float kControl = 44;
constexpr float kRowHeight = 48;
constexpr float kRadius = 10;
constexpr float kSortWidth = 140;
constexpr int kRecenterMs = 240;
constexpr int kRecenterStillMs = 50;
constexpr int kSortCount = 5;

// material "memory", "view_list", "search", "expand_more", "file_download"
const char* const kCartGlyph = "\xEE\x8C\xA2";
const char* const kListGlyph = "\xEE\xA3\xAF";
const char* const kSearchGlyph = "\xEE\xA2\xB6";
const char* const kArrowGlyph = "\xEE\x97\x8F";
const char* const kDownloadGlyph = "\xEE\x8B\x84";

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

    // Search and sort.
    auto* controls = new Box(Axis::ROW);
    controls->setAlignItems(AlignItems::CENTER);
    controls->setPadding(0, 0, 6, 0);
    controls->setShrink(0);
    addView(controls);

    searchBox = touchBox(Axis::ROW, [this] { openSearch(); });
    searchBox->setHeight(kControl);
    searchBox->setGrow(1);
    searchBox->setShrink(1);
    searchBox->setPadding(0, 12, 0, 12);
    searchBox->setCornerRadius(kRadius);
    searchBox->setHighlightCornerRadius(kRadius + 3);
    searchBox->setBackgroundColor(themeColor("equinox/card_inner"));
    searchIcon = iconLabel(kSearchGlyph, 18);
    searchBox->addView(searchIcon);
    searchLabel = textLabel(15);
    searchLabel->setMarginLeft(8);
    searchLabel->setGrow(1);
    searchLabel->setShrink(1);
    searchBox->addView(searchLabel);
    controls->addView(searchBox);

    sortBox = touchBox(Axis::ROW, [this] { openSort(); });
    sortBox->setWidth(kSortWidth);
    sortBox->setHeight(kControl);
    sortBox->setShrink(0);
    sortBox->setMarginLeft(8);
    sortBox->setPadding(0, 8, 0, 12);
    sortBox->setCornerRadius(kRadius);
    sortBox->setHighlightCornerRadius(kRadius + 3);
    sortBox->setBackgroundColor(themeColor("equinox/card_inner"));
    sortLabel = textLabel(15);
    sortLabel->setGrow(1);
    sortLabel->setShrink(1);
    sortBox->addView(sortLabel);
    sortArrow = iconLabel(kArrowGlyph, 18);
    sortBox->addView(sortArrow);
    controls->addView(sortBox);

    // The games.
    noneLabel = new Label();
    noneLabel->setFontSize(15);
    noneLabel->setMargins(20, 4, 0, 4);
    noneLabel->setShrink(0);
    noneLabel->setVisibility(Visibility::GONE);
    addView(noneLabel);

    scroll = new LibraryScroll();
    list = new Box(Axis::COLUMN);
    list->setPadding(4, 6, 4, 6);
    scroll->setContentView(list);
    addView(scroll);

    // The download-all area: one of four states.
    separator = new Rectangle();
    separator->setHeight(1);
    separator->setMarginTop(8);
    separator->setShrink(0);
    addView(separator);

    footer = new Box(Axis::COLUMN);
    footer->setPadding(12, 0, 0, 0);
    footer->setShrink(0);
    addView(footer);

    idleRow = touchBox(Axis::ROW, [this] { downloadAllOrCancel(); });
    idleRow->setHeight(kRowHeight);
    idleRow->setPadding(0, 14, 0, 14);
    idleRow->setCornerRadius(kRadius);
    idleRow->setHighlightCornerRadius(kRadius + 3);
    idleRow->setBackgroundColor(themeColor("equinox/card_inner"));
    idleIcon = iconLabel(kDownloadGlyph, 18);
    idleRow->addView(idleIcon);
    auto* idleTitle = new BoldLabel();
    idleTitle->setFontSize(15);
    idleTitle->setSingleLine(true);
    idleTitle->setAutoAnimate(false);
    idleTitle->setText("library/list/download_all"_i18n);
    idleTitle->setMarginLeft(10);
    idleTitle->setGrow(1);
    idleRow->addView(idleTitle);
    idleMissing = textLabel(14);
    idleRow->addView(idleMissing);
    footer->addView(idleRow);

    doneLabel = new Label();
    doneLabel->setFontSize(15);
    doneLabel->setHeight(kRowHeight);
    doneLabel->setVerticalAlign(VerticalAlign::CENTER);
    doneLabel->setText("library/list/done"_i18n);
    footer->addView(doneLabel);

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

    registerAction("library/list/hint_mode"_i18n, BUTTON_RB, [this](View*) {
        toggleMode();
        return true;
    });
    registerAction("library/list/hint_sort"_i18n, BUTTON_LB, [this](View*) {
        openSort();
        return true;
    });
    registerAction("library/list/hint_search"_i18n, BUTTON_BACK, [this](View*) {
        openSearch();
        return true;
    });
    registerAction("library/list/download_all"_i18n, BUTTON_RT, [this](View*) {
        downloadAllOrCancel();
        return true;
    });

    applyColors();
    applyQuery();
    applySort();
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
    searchIcon->setTextColor(muted);
    sortArrow->setTextColor(muted);
    idleMissing->setTextColor(muted);
    doneLabel->setTextColor(muted);
    runningName->setTextColor(muted);
    noneLabel->setTextColor(muted);
    separator->setColor(themeColor("equinox/card"));
    idleIcon->setTextColor(themeColor("brls/text"));
    sortLabel->setTextColor(themeColor("brls/text"));
    retryButton->setBackgroundColor(themeColor("equinox/card_inner"));
    cancelButton->setBackgroundColor(themeColor("equinox/card_inner"));
    applyQuery();
    applyToggle();
}

void GameListPanel::applyCount() {
    const size_t total = items.size();
    std::string text;
    if (shown.size() != total)
        text = brls::getStr("library/list/count_filtered", shown.size(), total);
    else if (total == 1)
        text = "library/list/count_one"_i18n;
    else
        text = brls::getStr("library/list/count_many", total);
    countLabel->setText(text);
}

void GameListPanel::applyQuery() {
    searchLabel->setText(query.empty() ? "library/list/search"_i18n : query);
    searchLabel->setTextColor(query.empty() ? themeColor("equinox/muted")
                                            : themeColor("brls/text"));
}

void GameListPanel::applySort() {
    sortLabel->setText(sortName(sort));
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
    applyLooks(true);
    if (focusInList())
        scheduleRecenter();
}

void GameListPanel::toggleMode() {
    setLook(mode == 0 ? 1 : 0);
    Settings::instance().set_library_view(mode);
    Settings::instance().save();
}

void GameListPanel::setItems(std::vector<emu::LibraryItem> nextItems,
                             std::vector<GameRowCell*> nextCells) {
    // The old rows are about to go; the focus waits on the search field.
    if (focusInList())
        Application::giveFocus(searchBox);
    list->clearViews();
    const bool hadCurrent = current.has_value();
    current.reset();
    shown.clear();

    items = std::move(nextItems);
    cells = std::move(nextCells);
    for (size_t i = 0; i < cells.size(); i++) {
        GameRowCell* cell = cells[i];
        cell->setMarginBottom(2);
        cell->onFocused = [this, i] { focusedGame(i); };
        list->addView(cell);
    }
    applyOrder(true);
    // Nothing to pick in the new set; applyOrder only reports a first result.
    if (hadCurrent && !current && onFocusGame)
        onFocusGame(std::nullopt);
}

void GameListPanel::setItem(size_t index, emu::LibraryItem item) {
    if (index >= items.size())
        return;
    items[index] = std::move(item);
    // Only the data changed: the list stays where the user left it.
    applyOrder(false);
}

void GameListPanel::applyOrder(bool resetScroll) {
    const std::optional<size_t> focused = focusedIndex();
    shown = emu::libraryOrder(items, query, static_cast<emu::LibrarySort>(sort));

    std::vector<bool> visible(cells.size(), false);
    for (size_t index : shown)
        visible[index] = true;

    list->clearViews(false);
    for (size_t index : shown) {
        cells[index]->setVisibility(Visibility::VISIBLE);
        list->addView(cells[index]);
    }
    for (size_t i = 0; i < cells.size(); i++) {
        if (visible[i])
            continue;
        cells[i]->setVisibility(Visibility::GONE);
        list->addView(cells[i]);
    }

    const bool none = shown.empty() && !query.empty();
    noneLabel->setText(brls::getStr("library/list/none", query));
    noneLabel->setVisibility(none ? Visibility::VISIBLE : Visibility::GONE);
    applyCount();

    // The game that leaves the filter hands the focus to the first result.
    if (focused && !visible[*focused]) {
        if (!shown.empty())
            Application::giveFocus(cells[shown.front()]);
        else
            Application::giveFocus(searchBox);
    }
    // Focus on the search field: the current game still follows the filter.
    std::optional<size_t> next = current;
    if (next && !visible[*next])
        next.reset();
    if (!next && !shown.empty())
        next = shown.front();
    setCurrent(next, false);
    applyLooks(false);

    if (focusInList())
        scroll->recenterNextFrame();
    else if (resetScroll)
        scroll->setContentOffsetY(0, false);
    applyBatch();
}

void GameListPanel::applyLooks(bool animate) {
    for (size_t i = 0; i < cells.size(); i++) {
        RowLook look = RowLook::Name;
        if (mode == 0)
            look = current && *current == i ? RowLook::Cartridge : RowLook::Thumb;
        const bool visible = cells[i]->getVisibility() == Visibility::VISIBLE;
        cells[i]->setLook(look, animate && visible);
    }
}

// The one place where the current game changes; onFocusGame fires only then.
void GameListPanel::setCurrent(std::optional<size_t> next, bool animate) {
    if (next == current)
        return;
    current = next;
    applyLooks(animate);
    scheduleRecenter();
    if (onFocusGame)
        onFocusGame(current);
}

void GameListPanel::focusedGame(size_t index) {
    setCurrent(index, true);
}

void GameListPanel::focusGame(size_t index) {
    GameRowCell* target = nullptr;
    if (index < cells.size() && cells[index]->getVisibility() == Visibility::VISIBLE)
        target = cells[index];
    else if (!shown.empty())
        target = cells[shown.front()];

    if (!target) {
        Application::giveFocus(searchBox);
        setCurrent(std::nullopt, true);
        return;
    }
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
    if (!cell || cell->index() >= cells.size() || cells[cell->index()] != cell)
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
    applyQuery();
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
    applySort();
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

void GameListPanel::applyBatch() {
    const Batch state = batchState();
    const bool footerFocused = isInside(Application::getCurrentFocus(), footer);
    const Visibility area = items.empty() ? Visibility::GONE : Visibility::VISIBLE;
    footer->setVisibility(area);
    separator->setVisibility(area);

    const auto shownWhen = [&](Batch b) {
        return state == b ? Visibility::VISIBLE : Visibility::GONE;
    };
    idleRow->setVisibility(shownWhen(Batch::Idle));
    doneLabel->setVisibility(shownWhen(Batch::Done));
    stoppedRow->setVisibility(shownWhen(Batch::Stopped));
    runningRow->setVisibility(shownWhen(Batch::Running));

    idleMissing->setText(brls::getStr("library/list/missing", missing));
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
    updateActionHint(BUTTON_RT, state == Batch::Running
                                    ? "common/cancel"_i18n
                                    : state == Batch::Stopped
                                          ? "library/details/retry"_i18n
                                          : "library/list/download_all"_i18n);
    setActionAvailable(BUTTON_RT, state != Batch::Done && !items.empty());

    // The control that had the focus may be the one that just went away.
    if (footerFocused) {
        View* now = Application::getCurrentFocus();
        const bool gone = !now || !now->isFocusable() ||
                          (isInside(now, idleRow) && state != Batch::Idle) ||
                          (isInside(now, stoppedRow) && state != Batch::Stopped) ||
                          (isInside(now, runningRow) && state != Batch::Running);
        if (gone) {
            View* target = state == Batch::Running   ? static_cast<View*>(runningRow)
                           : state == Batch::Stopped ? static_cast<View*>(stoppedRow)
                           : state == Batch::Idle    ? static_cast<View*>(idleRow)
                                                     : static_cast<View*>(scroll);
            Application::giveFocus(target);
        }
    }
}

#endif // PLATFORM_SWITCH
