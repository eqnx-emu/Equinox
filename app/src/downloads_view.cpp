//
//  downloads_view.cpp
//  Equinox
//

#include "downloads_view.hpp"

#if defined(PLATFORM_SWITCH)

#include "bold_label.hpp"
#include "download_service.hpp"
#include "library_widgets.hpp"
#include "options_sheet.hpp"
#include "pressable_box.hpp"
#include "emu/Console.hpp"
#include "emu/RunnerInstaller.hpp"

#include <algorithm>
#include <ctime>

using namespace brls;
using namespace brls::literals;

namespace {

// Thousands of cover events arrive during a batch: the screen reads the
// queue again at most four times a second.
constexpr auto kRefreshEvery = std::chrono::milliseconds(250);

NVGcolor themeColor(const char* name) { return Application::getTheme()[name]; }

// A label set to the same text still lays the screen out again.
void setText(Label* label, const std::string& text) {
    if (label->getFullText() != text)
        label->setText(text);
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

std::string groupTitle(const emu::DlGroup& group) {
    const std::string kind = std::string("downloads/kind/") + kindKey(group.kind);
    if (group.kind == emu::DlKind::AppUpdate)
        return brls::getStr(kind);
    const emu::Console* console = emu::findConsole(group.group);
    return brls::getStr("downloads/row", console ? console->name : group.group,
                        brls::getStr(kind));
}

std::string groupStatus(const emu::DlGroup& group) {
    std::string state;
    if (group.running > 0)
        state = "downloads/state/running"_i18n;
    else if (group.queued > 0)
        state = "downloads/state/queued"_i18n;
    else if (group.held > 0)
        state = "downloads/state/held"_i18n;
    else if (group.paused > 0)
        state = "downloads/state/paused"_i18n;
    else
        state = "downloads/state/failed"_i18n;
    if (singleTask(group.kind))
        return state;

    const bool waits = group.running + group.queued > 0;
    auto add = [&state](const std::string& part) { state += "  ·  " + part; };
    if (group.held > 0 && waits)
        add(brls::getStr("downloads/extra_held", group.held));
    if (group.paused > 0 && (waits || group.held > 0))
        add(brls::getStr("downloads/extra_paused", group.paused));
    if (group.failed > 0 && (waits || group.held > 0 || group.paused > 0))
        add(brls::getStr("downloads/extra_failed", group.failed));
    return state;
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

// A rounded card with a warning icon and a sentence.
Box* makeNotice(Label** text) {
    auto* box = new Box(Axis::ROW);
    box->setAlignItems(AlignItems::CENTER);
    box->setPadding(14, 18, 14, 18);
    box->setCornerRadius(14);
    box->setBackgroundColor(themeColor("equinox/card"));
    box->setMarginBottom(10);

    auto* icon = new IconLabel();
    icon->setFontSize(24);
    icon->setText(""); // warning
    icon->setTextColor(themeColor("equinox/danger"));
    box->addView(icon);

    auto* label = new Label();
    label->setFontSize(16);
    label->setMarginLeft(14);
    label->setGrow(1.0f);
    label->setShrink(1.0f);
    box->addView(label);
    *text = label;
    return box;
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

// MARK: - Row

class DownloadsView::Row : public PressableBox {
  public:
    std::string key;
    emu::DlGroup group;

    Row() : PressableBox(Axis::ROW) {
        setAlignItems(AlignItems::CENTER);
        setPadding(14, 20, 14, 18);
        setCornerRadius(14);
        setHighlightCornerRadius(16);
        setBackgroundColor(themeColor("equinox/card"));
        setMarginBottom(10);
        setFocusable(true);
        addGestureRecognizer(new TapGestureRecognizer(this));

        icon = new IconLabel();
        icon->setFontSize(28);
        icon->setTextColor(themeColor("equinox/muted"));
        addView(icon);

        auto* column = new Box(Axis::COLUMN);
        column->setGrow(1.0f);
        column->setShrink(1.0f);
        column->setMarginLeft(16);
        title = new BoldLabel();
        title->setFontSize(18);
        title->setSingleLine(true);
        column->addView(title);
        status = new Label();
        status->setFontSize(15);
        status->setSingleLine(true);
        status->setMarginTop(4);
        status->setTextColor(themeColor("equinox/muted"));
        column->addView(status);
        bar = new LibraryProgressBar();
        bar->setMarginTop(10);
        column->addView(bar);
        addView(column);

        count = new Label();
        count->setFontSize(16);
        count->setSingleLine(true);
        count->setMarginLeft(16);
        count->setTextColor(themeColor("equinox/muted"));
        addView(count);

        registerClickAction([this](View*) {
            press([this] { openActions(); });
            return true;
        });
        updateActionHint(BUTTON_A, "downloads/actions"_i18n);
    }

    void set(const emu::DlGroup& fresh, const DownloadsRefresh& refresher) {
        group = fresh;
        key = groupKey(fresh);
        setText(icon, kindIcon(fresh.kind));
        setText(title, groupTitle(fresh));
        setText(status, groupStatus(fresh));

        const bool tracked = fresh.running > 0 && refresher.runningId() == fresh.runningId;
        const float running = tracked ? refresher.progress() : 0.0f;
        if (singleTask(fresh.kind)) {
            setText(count, fresh.running > 0
                               ? brls::getStr("downloads/percent",
                                              std::clamp((int)(running * 100), 0, 100))
                               : "");
            bar->setFraction(running);
            bar->setVisibility(fresh.running > 0 ? Visibility::VISIBLE : Visibility::GONE);
            return;
        }
        const size_t total = std::max<size_t>(fresh.total(), 1);
        setText(count, brls::getStr("downloads/count", fresh.finished, fresh.total()));
        bar->setFraction((fresh.finished + running) / (float)total);
        bar->setVisibility(Visibility::VISIBLE);
    }

  private:
    IconLabel* icon;
    BoldLabel* title;
    Label* status;
    LibraryProgressBar* bar;
    Label* count;

    // The sheet acts on the group by name, so it is safe after the row
    // shows another one or the screen is gone.
    void openActions() {
        const emu::DlGroup target = group;
        std::vector<OptionsSheet::Item> items;
        if (target.running + target.queued + target.held > 0)
            items.push_back({"downloads/action/pause"_i18n, [target] {
                                 DownloadService::instance().pauseGroup(target.group,
                                                                        target.kind);
                             }});
        if (target.paused > 0)
            items.push_back({"downloads/action/resume"_i18n, [target] {
                                 DownloadService::instance().resumeGroup(target.group,
                                                                         target.kind);
                             }});
        if (target.failed > 0)
            items.push_back({"downloads/action/retry"_i18n, [target] {
                                 DownloadService::instance().retryGroup(target.group,
                                                                        target.kind);
                             }});
        items.push_back({"downloads/action/cancel"_i18n, [target] { cancelGroup(target); }});
        OptionsSheet::open(groupTitle(target), std::move(items));
    }
};

// MARK: - DownloadsView

DownloadsView::DownloadsView() : refresher([this] { refresh(); }) {
    setAxis(Axis::COLUMN);
    getAppletFrameItem()->title = "downloads/title"_i18n;

    auto* scroll = new ScrollingFrame();
    scroll->setGrow(1.0f);
    auto* content = new Box(Axis::COLUMN);
    content->setPadding(30, 60, 30, 60);

    Label* loginText = nullptr;
    loginNotice = makeNotice(&loginText);
    loginText->setText("downloads/login_notice"_i18n);
    content->addView(loginNotice);
    quotaNotice = makeNotice(&quotaText);
    content->addView(quotaNotice);

    // After a full card everything is Paused: this is how it goes on.
    auto* resume = new PressableBox(Axis::ROW);
    resume->setAlignItems(AlignItems::CENTER);
    resume->setHeight(56);
    resume->setPadding(0, 18, 0, 18);
    resume->setCornerRadius(14);
    resume->setHighlightCornerRadius(16);
    resume->setBackgroundColor(themeColor("equinox/card"));
    resume->setMarginBottom(20);
    resume->setFocusable(true);
    resume->addGestureRecognizer(new TapGestureRecognizer(resume));
    auto* resumeIcon = new IconLabel();
    resumeIcon->setFontSize(24);
    resumeIcon->setText(""); // play_arrow
    resumeIcon->setTextColor(themeColor("equinox/status_on"));
    resume->addView(resumeIcon);
    auto* resumeLabel = new BoldLabel();
    resumeLabel->setFontSize(18);
    resumeLabel->setMarginLeft(14);
    resumeLabel->setText("downloads/resume_all"_i18n);
    resume->addView(resumeLabel);
    resume->registerClickAction([this, resume](View*) {
        resume->press([this] {
            DownloadService::instance().resumePaused();
            refresher.now();
        });
        return true;
    });
    resumeAll = resume;
    content->addView(resumeAll);

    list = new Box(Axis::COLUMN);
    content->addView(list);

    // Focusable, so the focus has somewhere to stay when the last row goes.
    empty = new Box(Axis::COLUMN);
    empty->setAlignItems(AlignItems::CENTER);
    empty->setMarginTop(120);
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
    content->addView(empty);

    scroll->setContentView(content);
    addView(scroll);

    refresher.now();
}

void DownloadsView::draw(NVGcontext* vg, float x, float y, float width, float height,
                         Style style, FrameContext* ctx) {
    refresher.tick();
    // A sheet closing hands the focus back to a row that may have gone meanwhile.
    keepFocus("");
    Box::draw(vg, x, y, width, height, style, ctx);
}

void DownloadsView::refresh() {
    const emu::DlOverview overview = DownloadService::instance().overview();

    std::string focusedKey;
    View* focus = Application::getCurrentFocus();
    for (Row* row : rows)
        if (row == focus && row->getVisibility() == Visibility::VISIBLE)
            focusedKey = row->key;

    loginNotice->setVisibility(overview.badLogin ? Visibility::VISIBLE : Visibility::GONE);
    if (overview.quotaUntil > 0)
        setText(quotaText, brls::getStr("downloads/quota_notice", quotaEnd(overview.quotaUntil)));
    quotaNotice->setVisibility(overview.quotaUntil > 0 ? Visibility::VISIBLE : Visibility::GONE);

    const bool anyPaused = std::any_of(overview.groups.begin(), overview.groups.end(),
                                       [](const emu::DlGroup& g) { return g.paused > 0; });
    resumeAll->setVisibility(anyPaused ? Visibility::VISIBLE : Visibility::GONE);

    while (rows.size() < overview.groups.size()) {
        auto* row = new Row();
        rows.push_back(row);
        list->addView(row);
    }
    for (size_t i = 0; i < rows.size(); i++) {
        if (i < overview.groups.size()) {
            rows[i]->set(overview.groups[i], refresher);
            rows[i]->setVisibility(Visibility::VISIBLE);
        } else {
            rows[i]->setVisibility(Visibility::GONE);
        }
    }
    empty->setVisibility(overview.groups.empty() ? Visibility::VISIBLE : Visibility::GONE);

    keepFocus(focusedKey);
}

void DownloadsView::keepFocus(const std::string& focusedKey) {
    View* focus = Application::getCurrentFocus();
    if (!focus || !isInside(focus, this))
        return; // a sheet or another screen is on top
    if (!focusedKey.empty()) {
        for (Row* row : rows) {
            if (row->getVisibility() != Visibility::VISIBLE || row->key != focusedKey)
                continue;
            if (row != focus)
                Application::giveFocus(row);
            return;
        }
    }
    if (!shownWithin(focus, this))
        Application::giveFocus(this);
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
