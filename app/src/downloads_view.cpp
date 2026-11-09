//
//  downloads_view.cpp
//  Equinox
//

#include "downloads_view.hpp"

#if defined(PLATFORM_SWITCH)

#include "bold_label.hpp"
#include "download_service.hpp"
#include "library_widgets.hpp"
#include "pressable_box.hpp"
#include "emu/Console.hpp"
#include "emu/RunnerInstaller.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>

using namespace brls;
using namespace brls::literals;

namespace {

// Thousands of cover events arrive during a batch: the screen reads the
// queue again at most four times a second.
constexpr auto kRefreshEvery = std::chrono::milliseconds(250);
// The list pane, its 60 px outer margin included.
constexpr float kListWidth = 440;

NVGcolor themeColor(const char* name) { return Application::getTheme()[name]; }

// A label set to the same text still lays the screen out again.
void setText(Label* label, const std::string& text) {
    if (label->getFullText() != text)
        label->setText(text);
}

void setShown(View* view, bool shown) {
    const Visibility visibility = shown ? Visibility::VISIBLE : Visibility::GONE;
    if (view->getVisibility() != visibility)
        view->setVisibility(visibility);
}

const char* kindKey(emu::DlKind kind) {
    switch (kind) {
    case emu::DlKind::Cover:
        return "cover";
    case emu::DlKind::GameData:
        return "game_data";
    case emu::DlKind::Runner:
        return "runner";
    case emu::DlKind::AppUpdate:
        return "app_update";
    case emu::DlKind::Media:
        return "media";
    }
    return "media";
}

const char* kindIcon(emu::DlKind kind) {
    switch (kind) {
    case emu::DlKind::Cover:
        return ""; // image
    case emu::DlKind::GameData:
        return ""; // description
    case emu::DlKind::Runner:
        return ""; // videogame_asset
    case emu::DlKind::AppUpdate:
        return ""; // system_update
    case emu::DlKind::Media:
        return "";
    }
    return "";
}

// One task at a time: progress is its percent, not a count.
bool singleTask(emu::DlKind kind) {
    return kind == emu::DlKind::Runner || kind == emu::DlKind::AppUpdate;
}

std::string groupKey(const emu::DlGroup& group) {
    return std::string(kindKey(group.kind)) + ":" + group.group;
}

std::string kindName(const emu::DlGroup& group) {
    return brls::getStr(std::string("downloads/kind/") + kindKey(group.kind));
}

// The heading the group is listed under: its console, or the app.
std::string sectionName(const emu::DlGroup& group) {
    if (group.kind == emu::DlKind::AppUpdate)
        return "title"_i18n;
    const emu::Console* console = emu::findConsole(group.group);
    return console ? console->name : group.group;
}

std::string groupTitle(const emu::DlGroup& group) {
    if (group.kind == emu::DlKind::AppUpdate)
        return kindName(group);
    return brls::getStr("downloads/row", sectionName(group), kindName(group));
}

// "12 covers", or "1 cover".
std::string amount(size_t count, emu::DlKind kind) {
    std::string key = std::string("downloads/unit/") + kindKey(kind);
    if (count == 1)
        key += "_one";
    return brls::getStr("downloads/amount", count, brls::getStr(key));
}

enum class Phase { Running, Queued, Held, Paused, Failed };

Phase phaseOf(const emu::DlGroup& group) {
    if (group.running > 0)
        return Phase::Running;
    if (group.queued > 0)
        return Phase::Queued;
    if (group.held > 0)
        return Phase::Held;
    if (group.paused > 0)
        return Phase::Paused;
    return Phase::Failed;
}

int percentOf(const emu::DlGroup& group, float running) {
    float fraction = running;
    if (!singleTask(group.kind))
        fraction = (group.finished + (group.running > 0 ? running : 0.0f)) /
                   (float)std::max<size_t>(group.total(), 1);
    return std::clamp((int)std::floor(fraction * 100), 0, 100);
}

std::string percentText(int percent) { return brls::getStr("downloads/percent", percent); }

// What a row says on its right: "61%", "queued", "quota"...
std::string shortState(const emu::DlGroup& group, const emu::DlOverview& overview,
                       float running) {
    switch (phaseOf(group)) {
    case Phase::Running:
        return percentText(percentOf(group, running));
    case Phase::Queued:
        return "downloads/short/queued"_i18n;
    case Phase::Held:
        return overview.badLogin ? "downloads/short/login"_i18n : "downloads/short/quota"_i18n;
    case Phase::Paused:
        return "downloads/short/paused"_i18n;
    case Phase::Failed:
        return "downloads/short/failed"_i18n;
    }
    return "";
}

std::string stateText(const emu::DlGroup& group) {
    switch (phaseOf(group)) {
    case Phase::Running:
        return "downloads/state/running"_i18n;
    case Phase::Queued:
        return "downloads/state/queued"_i18n;
    case Phase::Held:
        return "downloads/state/held"_i18n;
    case Phase::Paused:
        return "downloads/state/paused"_i18n;
    case Phase::Failed:
        return "downloads/state/failed"_i18n;
    }
    return "";
}

// Stopped by something the user has to see to.
bool alarming(const emu::DlGroup& group) {
    const Phase phase = phaseOf(group);
    return phase == Phase::Held || phase == Phase::Failed;
}

bool pausable(const emu::DlGroup& group) {
    return group.running + group.queued + group.held > 0;
}

// "today at 14:05" or "tomorrow at 14:05": a quota lasts at most a day.
std::string quotaEnd(int64_t until) {
    const std::time_t end = static_cast<std::time_t>(until);
    const std::time_t now = std::time(nullptr);
    const std::tm* endLocal = std::localtime(&end);
    if (!endLocal)
        return "";
    const std::tm endTm = *endLocal;
    const std::tm* nowLocal = std::localtime(&now);
    if (!nowLocal)
        return "";
    const std::tm nowTm = *nowLocal;
    char clock[8];
    std::strftime(clock, sizeof(clock), "%H:%M", &endTm);
    const bool today = endTm.tm_yday == nowTm.tm_yday && endTm.tm_year == nowTm.tm_year;
    return brls::getStr(today ? "downloads/quota_today" : "downloads/quota_tomorrow",
                        std::string(clock));
}

// A full-width card with a warning icon and a sentence, focusable and
// pressed like a row: A runs `action`.
Box* makeNotice(Label** text, const std::string& actionHint, std::function<void()> action) {
    auto* box = new PressableBox(Axis::ROW);
    box->setHighlightCornerRadius(14);
    box->setFocusable(true);
    box->addGestureRecognizer(new TapGestureRecognizer(box));
    box->registerClickAction([box, action](View*) {
        box->press(action);
        return true;
    });
    box->updateActionHint(BUTTON_A, actionHint);
    box->setAlignItems(AlignItems::CENTER);
    box->setPadding(12, 18, 12, 18);
    box->setCornerRadius(12);
    box->setBackgroundColor(themeColor("equinox/card"));
    box->setMarginTop(10);

    auto* icon = new IconLabel();
    icon->setFontSize(22);
    icon->setText(""); // warning
    icon->setTextColor(themeColor("equinox/danger"));
    box->addView(icon);

    auto* label = new Label();
    label->setFontSize(16);
    label->setMarginLeft(14);
    label->setGrow(1.0f);
    label->setShrink(1.0f);
    box->addView(label);

    auto* hint = new BoldLabel();
    hint->setFontSize(16);
    hint->setSingleLine(true);
    hint->setMarginLeft(16);
    hint->setTextColor(themeColor("equinox/status_on"));
    hint->setText(actionHint);
    box->addView(hint);

    *text = label;
    return box;
}

// One of the three figures under the bar: a muted name over a value.
Box* makeTile(const std::string& name, Label** value) {
    auto* tile = new Box(Axis::COLUMN);
    tile->setGrow(1.0f);
    tile->setShrink(1.0f);
    tile->setWidth(0);
    tile->setPadding(14, 16, 14, 16);
    tile->setCornerRadius(12);
    tile->setBackgroundColor(themeColor("equinox/card_inner"));
    auto* label = new Label();
    label->setFontSize(14);
    label->setSingleLine(true);
    label->setTextColor(themeColor("equinox/muted"));
    label->setText(name);
    tile->addView(label);
    auto* text = new Label();
    text->setFontSize(16);
    text->setSingleLine(true);
    text->setMarginTop(4);
    tile->addView(text);
    *value = text;
    return tile;
}

PressableBox* makeButton(Label** icon, Label** label, NVGcolor background, NVGcolor color) {
    auto* button = new PressableBox(Axis::ROW);
    button->setAlignItems(AlignItems::CENTER);
    button->setHeight(52);
    button->setPadding(0, 24, 0, 24);
    button->setCornerRadius(12);
    button->setHighlightCornerRadius(14);
    button->setBackgroundColor(background);
    button->setMarginRight(12);
    button->setFocusable(true);
    button->addGestureRecognizer(new TapGestureRecognizer(button));
    if (icon) {
        auto* glyph = new IconLabel();
        glyph->setFontSize(20);
        glyph->setTextColor(color);
        glyph->setMarginRight(10);
        button->addView(glyph);
        *icon = glyph;
    }
    auto* text = new BoldLabel();
    text->setFontSize(17);
    text->setSingleLine(true);
    text->setTextColor(color);
    button->addView(text);
    *label = text;
    return button;
}

bool isInside(View* view, View* ancestor) {
    for (View* v = view; v; v = v->hasParent() ? v->getParent() : nullptr)
        if (v == ancestor)
            return true;
    return false;
}

// Shown: neither it nor a parent up to `root` is hidden.
bool shownWithin(View* view, View* root) {
    for (View* v = view; v; v = v->hasParent() ? v->getParent() : nullptr) {
        if (v->getVisibility() != Visibility::VISIBLE)
            return false;
        if (v == root)
            return true;
    }
    return false;
}

void cancelGroup(const emu::DlGroup& group) {
    DownloadService& service = DownloadService::instance();
    service.cancelGroup(group.group, group.kind);
    // A runner keeps its .part across pauses. While the worker still runs
    // it, the runner removes it itself; the app update does the same in its
    // own listener.
    if (group.kind != emu::DlKind::Runner)
        return;
    const emu::Console* console = emu::findConsole(group.group);
    if (console && !service.active(emu::runnerId(group.group)))
        emu::discardRunnerDownload(*console);
}

// The groups of one console together, consoles in the order they arrived.
void sortBySection(std::vector<emu::DlGroup>& groups) {
    std::map<std::pair<bool, std::string>, size_t> first;
    for (const emu::DlGroup& group : groups)
        first.emplace(std::make_pair(group.kind == emu::DlKind::AppUpdate, group.group),
                      first.size());
    std::stable_sort(groups.begin(), groups.end(),
                     [&first](const emu::DlGroup& a, const emu::DlGroup& b) {
                         return first[{a.kind == emu::DlKind::AppUpdate, a.group}] <
                                first[{b.kind == emu::DlKind::AppUpdate, b.group}];
                     });
}

} // namespace

// MARK: - DownloadsRefresh

DownloadsRefresh::DownloadsRefresh(std::function<void()> refresh)
    : refresh_(std::move(refresh)) {
    token_ = DownloadService::instance().subscribe([this](const DlEvent& event) {
        stale_ = true;
        if (event.state == emu::DlState::Running) {
            runningId_ = event.id;
            progress_ = event.progress;
        }
    });
}

DownloadsRefresh::~DownloadsRefresh() { DownloadService::instance().unsubscribe(token_); }

void DownloadsRefresh::tick() {
    if (!stale_)
        return;
    const Clock::time_point at = Clock::now();
    if (at - last_ < kRefreshEvery)
        return;
    now();
}

void DownloadsRefresh::now() {
    stale_ = false;
    last_ = Clock::now();
    if (refresh_)
        refresh_();
}

float DownloadsRefresh::progressOf(const emu::DlGroup& group) const {
    return group.running > 0 && group.runningId == runningId_ ? progress_ : 0.0f;
}

// MARK: - Row

// A section heading, shown when the group starts a new console, over the
// row itself: icon, kind and a short state on the right.
class DownloadsView::Row : public Box {
  public:
    std::string key;
    PressableBox* item = nullptr;

    explicit Row(DownloadsView* owner) : Box(Axis::COLUMN), owner(owner) {
        heading = new BoldLabel();
        heading->setFontSize(14);
        heading->setSingleLine(true);
        heading->setTextColor(themeColor("equinox/muted"));
        heading->setMarginLeft(14);
        heading->setMarginBottom(6);
        addView(heading);

        item = new PressableBox(Axis::ROW);
        item->setAlignItems(AlignItems::CENTER);
        item->setPadding(12, 14, 12, 14);
        item->setCornerRadius(12);
        item->setHighlightCornerRadius(14);
        item->setMarginBottom(6);
        item->setFocusable(true);
        // A tap selects the row; its actions are on the right.
        item->addGestureRecognizer(new TapGestureRecognizer(item, [] {}));
        icon = new IconLabel();
        icon->setFontSize(22);
        item->addView(icon);
        name = new Label();
        name->setFontSize(17);
        name->setSingleLine(true);
        name->setGrow(1.0f);
        name->setShrink(1.0f);
        name->setMarginLeft(12);
        item->addView(name);
        state = new Label();
        state->setFontSize(15);
        state->setSingleLine(true);
        state->setMarginLeft(12);
        item->addView(state);
        item->registerClickAction([this](View*) {
            if (View* action = this->owner->firstAction())
                Application::giveFocus(action);
            return true;
        });
        item->updateActionHint(BUTTON_A, "downloads/select"_i18n);
        setSelected(false);
    }

    void set(const emu::DlGroup& group, bool startsSection, const emu::DlOverview& overview,
             const DownloadsRefresh& refresher) {
        key = groupKey(group);
        setShown(heading, startsSection);
        if (startsSection) {
            setText(heading, sectionName(group));
            // Set only on a change: a margin lays the list out again.
            const float top = this == owner->rows.front() ? 0 : 14;
            if (top != headingTop) {
                headingTop = top;
                heading->setMarginTop(top);
            }
        }
        setText(icon, kindIcon(group.kind));
        setText(name, kindName(group));
        setText(state, shortState(group, overview, refresher.progressOf(group)));
        state->setTextColor(themeColor(alarming(group) ? "equinox/danger" : "equinox/muted"));
    }

    void setSelected(bool on) {
        if (on == selected)
            return;
        selected = on;
        item->setBackgroundColor(on ? themeColor("equinox/card") : nvgRGBA(0, 0, 0, 0));
        icon->setTextColor(themeColor(on ? "brls/text" : "equinox/muted"));
    }

    void onChildFocusGained(View* directChild, View* focusedView) override {
        Box::onChildFocusGained(directChild, focusedView);
        owner->select(key);
    }

  private:
    DownloadsView* owner;
    Label* heading;
    IconLabel* icon;
    Label* name;
    Label* state;
    float headingTop = 0;
    bool selected = true;  // so the constructor's setSelected(false) applies
};

// MARK: - Panes

// The list and the detail side by side: right from the list goes to the
// actions, left from them back to the selected row, whatever was focused
// there before.
class DownloadsView::Panes : public Box {
  public:
    explicit Panes(DownloadsView* owner) : Box(Axis::ROW), owner(owner) {}

    View* listPane = nullptr;
    View* detailPane = nullptr;

    View* getDefaultFocus() override {
        if (getVisibility() != Visibility::VISIBLE)
            return nullptr;
        if (Row* row = owner->selectedRow())
            return row->item;
        return Box::getDefaultFocus();
    }

    View* getNextFocus(FocusDirection direction, View* currentView) override {
        if (direction == FocusDirection::RIGHT && currentView == listPane)
            if (View* action = owner->firstAction())
                return action;
        if (direction == FocusDirection::LEFT && currentView == detailPane)
            if (Row* row = owner->selectedRow())
                return row->item;
        return Box::getNextFocus(direction, currentView);
    }

  private:
    DownloadsView* owner;
};

// MARK: - DownloadsView

DownloadsView::DownloadsView() : refresher([this] { refresh(); }) {
    setAxis(Axis::COLUMN);
    getAppletFrameItem()->title = "downloads/title"_i18n;

    summary = new Label();
    summary->setFontSize(16);
    summary->setSingleLine(true);
    summary->setTextColor(themeColor("equinox/muted"));
    getAppletFrameItem()->setHintView(summary);

    // Above both panes, full width.
    notices = new Box(Axis::COLUMN);
    notices->setPadding(6, 60, 0, 60);
    // The user's "try now": lifts the ScreenScraper block for one more go.
    auto tryNow = [this] {
        DownloadService::instance().retryHost(emu::kHostScreenScraper);
        refresher.now();
    };
    Label* loginText = nullptr;
    loginNotice = makeNotice(&loginText, "downloads/try_now"_i18n, tryNow);
    loginText->setText("downloads/login_notice"_i18n);
    notices->addView(loginNotice);
    quotaNotice = makeNotice(&quotaText, "downloads/try_now"_i18n, tryNow);
    notices->addView(quotaNotice);
    // After a full card everything is Paused: this is how it goes on.
    Label* diskText = nullptr;
    diskNotice = makeNotice(&diskText, "downloads/resume_all"_i18n, [this] {
        DownloadService::instance().resumePaused();
        refresher.now();
    });
    diskText->setText("downloads/disk_notice"_i18n);
    notices->addView(diskNotice);
    addView(notices);

    panes = new Panes(this);
    panes->setGrow(1.0f);

    auto* scroll = new ScrollingFrame();
    scroll->setWidth(kListWidth);
    scroll->setShrink(0);
    // Centred, so up from the first row leaves the list for the notices.
    scroll->setScrollingBehavior(ScrollingBehavior::CENTERED);
    scroll->setScrollingIndicatorVisible(false);
    scroll->setLineColor(themeColor("brls/applet_frame/separator"));
    scroll->setLineRight(1);
    list = new Box(Axis::COLUMN);
    list->setPadding(22, 20, 22, 60);
    scroll->setContentView(list);
    panes->addView(scroll);
    panes->listPane = scroll;

    auto* detail = new Box(Axis::COLUMN);
    detail->setGrow(1.0f);
    detail->setShrink(1.0f);
    detail->setPadding(30, 60, 22, 44);

    paneConsole = new Label();
    paneConsole->setFontSize(15);
    paneConsole->setSingleLine(true);
    paneConsole->setTextColor(themeColor("equinox/muted"));
    detail->addView(paneConsole);
    paneTitle = new BoldLabel();
    paneTitle->setFontSize(32);
    paneTitle->setSingleLine(true);
    paneTitle->setMarginTop(6);
    detail->addView(paneTitle);

    auto* figures = new Box(Axis::ROW);
    figures->setAlignItems(AlignItems::FLEX_END);
    figures->setMarginTop(22);
    panePercent = new BoldLabel();
    panePercent->setFontSize(64);
    panePercent->setSingleLine(true);
    panePercent->setMarginRight(14);
    figures->addView(panePercent);
    paneCount = new Label();
    paneCount->setFontSize(17);
    paneCount->setSingleLine(true);
    paneCount->setShrink(1.0f);
    paneCount->setMarginBottom(10);
    paneCount->setTextColor(themeColor("equinox/muted"));
    figures->addView(paneCount);
    detail->addView(figures);

    paneBar = new LibraryProgressBar();
    paneBar->setHeight(10);
    paneBar->setMarginTop(22);
    detail->addView(paneBar);

    tiles = new Box(Axis::ROW);
    tiles->setMarginTop(22);
    nowTile = makeTile("downloads/tile/now"_i18n, &nowValue);
    nowTile->setMarginRight(12);
    tiles->addView(nowTile);
    Box* leftTile = makeTile("downloads/tile/left"_i18n, &leftValue);
    leftTile->setMarginRight(12);
    tiles->addView(leftTile);
    tiles->addView(makeTile("downloads/tile/failed"_i18n, &failedValue));
    detail->addView(tiles);

    auto* spacer = new Box();
    spacer->setGrow(1.0f);
    detail->addView(spacer);

    // The actions act on the group shown when pressed, by name, so they are
    // safe once it is gone.
    actions = new Box(Axis::ROW);
    pauseButton = makeButton(&pauseIcon, &pauseLabel, themeColor("equinox/status_on"),
                             themeColor("brls/background"));
    pauseButton->registerClickAction([this](View*) {
        const emu::DlGroup* group = selectedGroup();
        if (!group)
            return true;
        const emu::DlGroup target = *group;
        pauseButton->press([this, target] {
            DownloadService& service = DownloadService::instance();
            if (pausable(target))
                service.pauseGroup(target.group, target.kind);
            else
                service.resumeGroup(target.group, target.kind);
            refresher.now();
        });
        return true;
    });
    actions->addView(pauseButton);
    retryButton = makeButton(nullptr, &retryLabel, themeColor("equinox/card"),
                             themeColor("brls/text"));
    retryButton->registerClickAction([this](View*) {
        const emu::DlGroup* group = selectedGroup();
        if (!group)
            return true;
        const emu::DlGroup target = *group;
        retryButton->press([this, target] {
            DownloadService::instance().retryGroup(target.group, target.kind);
            refresher.now();
        });
        return true;
    });
    actions->addView(retryButton);
    Label* cancelLabel = nullptr;
    cancelButton = makeButton(nullptr, &cancelLabel, themeColor("equinox/card"),
                              themeColor("equinox/danger"));
    cancelLabel->setText("downloads/action/cancel"_i18n);
    cancelButton->registerClickAction([this](View*) {
        const emu::DlGroup* group = selectedGroup();
        if (!group)
            return true;
        const emu::DlGroup target = *group;
        cancelButton->press([target] {
            auto* confirm =
                new Dialog(brls::getStr("downloads/cancel_message", groupTitle(target)));
            confirm->addButton("downloads/keep"_i18n, [] {});
            confirm->addButton("downloads/action/cancel"_i18n,
                               [target] { cancelGroup(target); });
            confirm->open();
        });
        return true;
    });
    actions->addView(cancelButton);
    detail->addView(actions);
    panes->addView(detail);
    panes->detailPane = detail;
    addView(panes);

    // Centred across both panes. Focusable, so the focus has somewhere to
    // stay when the last row goes.
    empty = new Box(Axis::COLUMN);
    empty->setGrow(1.0f);
    empty->setJustifyContent(JustifyContent::CENTER);
    empty->setAlignItems(AlignItems::CENTER);
    empty->setPadding(0, 120, 60, 120);
    empty->setFocusable(true);
    empty->setHideHighlight(true);
    auto* emptyIcon = new IconLabel();
    emptyIcon->setFontSize(56);
    emptyIcon->setText(""); // file_download
    emptyIcon->setTextColor(themeColor("equinox/muted"));
    empty->addView(emptyIcon);
    auto* emptyTitle = new BoldLabel();
    emptyTitle->setFontSize(22);
    emptyTitle->setMarginTop(12);
    emptyTitle->setText("downloads/empty_title"_i18n);
    empty->addView(emptyTitle);
    auto* emptyBody = new Label();
    emptyBody->setFontSize(16);
    emptyBody->setMarginTop(8);
    emptyBody->setHorizontalAlign(HorizontalAlign::CENTER);
    emptyBody->setTextColor(themeColor("equinox/muted"));
    emptyBody->setText("downloads/empty_body"_i18n);
    empty->addView(emptyBody);
    addView(empty);

    // The list first, not the notices.
    setDefaultFocusedIndex(1);

    yHint = "downloads/pause_all"_i18n;
    registerAction(yHint, BUTTON_Y, [this](View*) {
        if (shown.groups.empty())
            return true;
        DownloadService& service = DownloadService::instance();
        if (resumesAll()) {
            service.resumePaused();
        } else {
            for (const emu::DlGroup& group : shown.groups)
                if (pausable(group))
                    service.pauseGroup(group.group, group.kind);
        }
        refresher.now();
        return true;
    });

    refresher.now();
}

void DownloadsView::draw(NVGcontext* vg, float x, float y, float width, float height,
                         Style style, FrameContext* ctx) {
    refresher.tick();
    // A dialog closing hands the focus back to a view that may have gone
    // meanwhile.
    keepFocus();
    Box::draw(vg, x, y, width, height, style, ctx);
}

const emu::DlGroup* DownloadsView::selectedGroup() const {
    for (const emu::DlGroup& group : shown.groups)
        if (groupKey(group) == selectedKey)
            return &group;
    return nullptr;
}

DownloadsView::Row* DownloadsView::selectedRow() const {
    for (Row* row : rows)
        if (row->getVisibility() == Visibility::VISIBLE && row->key == selectedKey)
            return row;
    return nullptr;
}

View* DownloadsView::firstAction() const {
    if (panes->getVisibility() != Visibility::VISIBLE)
        return nullptr;
    for (View* button : actions->getChildren())
        if (button->getVisibility() == Visibility::VISIBLE)
            return button;
    return nullptr;
}

bool DownloadsView::resumesAll() const {
    return shown.active() == 0 &&
           std::any_of(shown.groups.begin(), shown.groups.end(),
                       [](const emu::DlGroup& g) { return g.paused > 0; });
}

void DownloadsView::refresh() {
    emu::DlOverview fresh = DownloadService::instance().overview();
    sortBySection(fresh.groups);

    // What had the focus and the selection, by group and by place.
    std::string focusedKey;
    size_t focusedAt = 0;
    View* focus = Application::getCurrentFocus();
    for (size_t i = 0; i < rows.size(); i++) {
        if (rows[i]->item == focus && rows[i]->getVisibility() == Visibility::VISIBLE) {
            focusedKey = rows[i]->key;
            focusedAt = i;
        }
    }
    size_t selectedAt = 0;
    for (size_t i = 0; i < shown.groups.size(); i++)
        if (groupKey(shown.groups[i]) == selectedKey)
            selectedAt = i;
    shown = std::move(fresh);
    const std::vector<emu::DlGroup>& groups = shown.groups;

    setShown(loginNotice, shown.badLogin);
    if (shown.quotaUntil > 0)
        setText(quotaText, brls::getStr("downloads/quota_notice", quotaEnd(shown.quotaUntil)));
    setShown(quotaNotice, shown.quotaUntil > 0);
    setShown(diskNotice, shown.diskFull);

    // "2 downloading  ·  1 paused", without the parts at zero.
    size_t downloading = 0, paused = 0;
    for (const emu::DlGroup& group : groups) {
        if (group.running + group.queued > 0)
            downloading++;
        else if (phaseOf(group) == Phase::Paused)
            paused++;
    }
    std::string line;
    if (downloading > 0)
        line = brls::getStr("downloads/summary_running", downloading);
    if (paused > 0) {
        if (!line.empty())
            line += "  ·  ";
        line += brls::getStr(paused == 1 ? "downloads/summary_paused_one"
                                         : "downloads/summary_paused",
                             paused);
    }
    setText(summary, line);

    while (rows.size() < groups.size()) {
        auto* row = new Row(this);
        rows.push_back(row);
        list->addView(row);
    }
    for (size_t i = 0; i < rows.size(); i++) {
        if (i >= groups.size()) {
            setShown(rows[i], false);
            continue;
        }
        const bool startsSection = i == 0 || sectionName(groups[i]) != sectionName(groups[i - 1]);
        rows[i]->set(groups[i], startsSection, shown, refresher);
        setShown(rows[i], true);
    }
    setShown(panes, !groups.empty());
    setShown(empty, groups.empty());

    // A selected group that finished leaves its place to its neighbour.
    if (!selectedGroup())
        selectedKey = groups.empty()
                          ? ""
                          : groupKey(groups[std::min(selectedAt, groups.size() - 1)]);
    for (Row* row : rows)
        row->setSelected(row->getVisibility() == Visibility::VISIBLE && row->key == selectedKey);
    fillPane();

    // The focused row follows its group, or its neighbour once it is gone.
    if (!focusedKey.empty() && !groups.empty()) {
        Row* target = nullptr;
        for (Row* row : rows)
            if (row->getVisibility() == Visibility::VISIBLE && row->key == focusedKey)
                target = row;
        if (!target)
            target = rows[std::min(focusedAt, groups.size() - 1)];
        if (target->item != focus)
            Application::giveFocus(target->item);
    }

    const std::string hint =
        groups.empty() ? "" : (resumesAll() ? "downloads/resume_all"_i18n : "downloads/pause_all"_i18n);
    if (hint != yHint) {
        if (!hint.empty())
            updateActionHint(BUTTON_Y, hint);
        if (hint.empty() != yHint.empty())
            setActionAvailable(BUTTON_Y, !hint.empty());
        else
            Application::getGlobalHintsUpdateEvent()->fire();
        yHint = hint;
    }

    keepFocus();
}

void DownloadsView::select(const std::string& key) {
    if (key == selectedKey)
        return;
    selectedKey = key;
    for (Row* row : rows)
        row->setSelected(row->getVisibility() == Visibility::VISIBLE && row->key == selectedKey);
    fillPane();
}

void DownloadsView::fillPane() {
    const emu::DlGroup* found = selectedGroup();
    if (!found)
        return;
    const emu::DlGroup& group = *found;
    const float running = refresher.progressOf(group);
    const int percent = percentOf(group, running);

    setText(paneConsole, sectionName(group));
    setText(paneTitle, kindName(group));
    setText(panePercent, percentText(percent));
    paneBar->setFraction(percent / 100.0f);

    if (singleTask(group.kind)) {
        // One file: its percent while it runs, and its state.
        setShown(panePercent, group.running > 0);
        setText(paneCount, stateText(group));
        setShown(tiles, false);
    } else {
        setShown(panePercent, true);
        setText(paneCount, brls::getStr("downloads/progress", group.finished, group.total(),
                                        brls::getStr(std::string("downloads/unit/") +
                                                     kindKey(group.kind))));
        setShown(tiles, true);
        setShown(nowTile, !group.runningName.empty());
        setText(nowValue, group.runningName);
        setText(leftValue, amount(group.remaining() - group.failed, group.kind));
        setText(failedValue, amount(group.failed, group.kind));
        failedValue->setTextColor(themeColor(group.failed > 0 ? "equinox/danger" : "brls/text"));
    }
    paneCount->setTextColor(
        themeColor(singleTask(group.kind) && alarming(group) ? "equinox/danger" : "equinox/muted"));

    const bool pause = pausable(group);
    setShown(pauseButton, pause || group.paused > 0);
    setText(pauseIcon, pause ? "" : ""); // pause, play_arrow
    setText(pauseLabel, pause ? "downloads/action/pause"_i18n : "downloads/action/resume"_i18n);
    setShown(retryButton, group.failed > 0);
    if (group.failed > 0)
        setText(retryLabel, group.failed == 1
                                ? "downloads/action/retry_one"_i18n
                                : brls::getStr("downloads/action/retry_failed", group.failed));
}

void DownloadsView::keepFocus() {
    View* focus = Application::getCurrentFocus();
    if (!focus || !isInside(focus, this))
        return; // a dialog or another screen is on top
    if (shownWithin(focus, this))
        return;
    if (isInside(focus, actions))
        if (View* action = firstAction()) {
            Application::giveFocus(action);
            return;
        }
    if (Row* row = selectedRow()) {
        Application::giveFocus(row->item);
        return;
    }
    Application::giveFocus(this);
}

// MARK: - Full card

void watchDiskStops() {
    static bool watching = false;
    if (watching)
        return;
    watching = true;
    // One stop sends an event per paused task: said once, until a task runs
    // again after the user freed space and resumed.
    auto said = std::make_shared<bool>(false);
    // Never unsubscribed: it lives as long as the app.
    DownloadService::instance().subscribe([said](const DlEvent& event) {
        if (event.state == emu::DlState::Running) {
            *said = false;
            return;
        }
        if (event.result != emu::DlResult::Disk || *said)
            return;
        *said = true;
        Application::notify("downloads/disk_paused"_i18n);
    });
}

// MARK: - DownloadIndicator

DownloadIndicator::DownloadIndicator() : Box(Axis::ROW), refresher([this] { refresh(); }) {
    setAlignItems(AlignItems::CENTER);
    setPadding(6, 14, 6, 12);
    setCornerRadius(16);
    addGestureRecognizer(new TapGestureRecognizer([this](TapGestureStatus status, Sound*) {
        if (status.state == GestureState::END && onOpen && count->getVisibility() == Visibility::VISIBLE)
            onOpen();
    }));

    auto* icon = new IconLabel();
    icon->setFontSize(20);
    icon->setText(""); // file_download
    icon->setTextColor(themeColor("equinox/status_on"));
    addView(icon);

    count = new Label();
    count->setFontSize(15);
    count->setSingleLine(true);
    count->setMarginLeft(6);
    addView(count);

    refresher.now();
}

void DownloadIndicator::draw(NVGcontext* vg, float x, float y, float width, float height,
                             Style style, FrameContext* ctx) {
    // Drawn even while idle, with nothing in it, so events can show it again.
    refresher.tick();
    Box::draw(vg, x, y, width, height, style, ctx);
}

void DownloadIndicator::refresh() {
    const size_t active = DownloadService::instance().overview().active();
    const Visibility shown = active > 0 ? Visibility::VISIBLE : Visibility::GONE;
    for (View* child : getChildren())
        child->setVisibility(shown);
    setBackgroundColor(active > 0 ? themeColor("equinox/card") : nvgRGBA(0, 0, 0, 0));
    if (active > 0)
        setText(count, std::to_string(active));
}

#endif // PLATFORM_SWITCH
