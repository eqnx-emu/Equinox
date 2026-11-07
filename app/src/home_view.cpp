//
//  home_view.cpp
//  Equinox
//

#include "home_view.hpp"
#include "about_tab.hpp"
#include "add_host_tab.hpp"
#include "bold_label.hpp"
#include "favorites_view.hpp"
#include "helper.hpp"
#include "settings_tab.hpp"
#include <Settings.hpp>

#if defined(PLATFORM_SWITCH)
#include "emu/PathUtf8.hpp"
#include "emu/RunnerInstaller.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fmt/core.h>
#include <map>
#include <memory>

using namespace brls;
using namespace brls::literals;

namespace {
// The focused card slides this far left when entered; the panel sits right.
constexpr float kEnterOffset = -330;
constexpr float kPanelLeft = 560;
constexpr float kPanelWidth = 640;
constexpr float kPanelTop = 72;
constexpr float kOptionGap = 12;
constexpr int kZoomMs = 620;
constexpr size_t kOptionRows = 10;

NVGcolor themeColor(const std::string& name) {
    return Application::getTheme().getColor(name);
}

NVGcolor hex(unsigned rgb) {
    return nvgRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

bool isInside(View* view, View* ancestor) {
    for (View* v = view; v; v = v->hasParent() ? v->getParent() : nullptr)
        if (v == ancestor)
            return true;
    return false;
}

std::string mbps(int kbps) {
    return kbps % 1000 == 0 ? std::to_string(kbps / 1000)
                            : fmt::format("{:.1f}", kbps / 1000.0);
}

// "1080p · 60 FPS · 20 Mbps"
std::string streamQuality() {
    const Settings& s = Settings::instance();
    const std::string res = s.resolution() <= 0
                                ? "settings/resolution_native"_i18n
                                : std::to_string(s.resolution()) + "p";
    return res + "  ·  " + std::to_string(s.fps()) + " FPS  ·  " +
           mbps(s.bitrate()) + " Mbps";
}

std::string gamesText(int count) {
    return count == 1 ? "hosts/games_one"_i18n
                      : brls::getStr("hosts/games_count", count);
}

std::string pcsText(int count) {
    return count == 1 ? "home/pcs_one"_i18n
                      : brls::getStr("home/pcs_count", count);
}

std::string consolesText(int count) {
    return count == 1 ? "home/consoles_one"_i18n
                      : brls::getStr("home/consoles_count", count);
}

#if defined(PLATFORM_SWITCH)
// Latest manifest seen per console (update check or install), kept while
// the app runs: the "Add" choice shows its size, the cards their update.
std::map<std::string, emu::Manifest>& latestManifests() {
    static std::map<std::string, emu::Manifest> manifests;
    return manifests;
}

// One install at a time; the daily check leaves installed.json alone then.
std::atomic<bool> installing{false};
std::atomic<bool> checkingUpdates{false};

// Translated reason for a failed install; curl's text only adds detail.
std::string runnerErrorText(emu::RunnerError code, const std::string& detail) {
    switch (code) {
    case emu::RunnerError::Network:
        return detail.empty() ? "emu/error_network"_i18n
                              : "emu/error_network"_i18n + "\n(" + detail + ")";
    case emu::RunnerError::BadManifest:
        return "emu/error_manifest"_i18n;
    case emu::RunnerError::WrongConsole:
        return "emu/error_console"_i18n;
    case emu::RunnerError::Folders:
        return "emu/error_folders"_i18n;
    case emu::RunnerError::Mismatch:
        return "emu/error_mismatch"_i18n;
    case emu::RunnerError::Move:
        return "emu/error_move"_i18n;
    case emu::RunnerError::WriteInstalled:
        return "emu/error_installed"_i18n;
    default:
        return detail;
    }
}

bool runnerInstalled(const emu::Console& console) {
    const emu::ConsolePaths paths = emu::pathsFor(console);
    std::error_code ec;
    return std::filesystem::is_regular_file(emu::fromUtf8(paths.runner), ec) &&
           emu::readInstalled(paths.installed).has_value();
}
#endif

// Favorite games per PC, without the apps the library hides.
struct FavoriteSummary {
    int games = 0;
    std::vector<std::pair<size_t, int>> perHost; // index in hosts(), count
    std::vector<std::string> names;
};

FavoriteSummary favoriteSummary() {
    FavoriteSummary summary;
    const auto hosts = Settings::instance().hosts();
    for (size_t i = 0; i < hosts.size(); i++) {
        int count = 0;
        for (const App& app : hosts[i].favorites)
            if (!isHiddenApp(app.name))
                count++;
        if (count == 0)
            continue;
        summary.games += count;
        summary.perHost.push_back({i, count});
        summary.names.push_back(hosts[i].hostname);
    }
    return summary;
}

std::string joinNames(const std::vector<std::string>& names) {
    std::string text;
    for (size_t i = 0; i < names.size(); i++) {
        if (i > 0)
            text += i + 1 == names.size() ? "home/fav_and"_i18n : ", ";
        text += names[i];
    }
    return text;
}

// MARK: - Card art

// The art is drawn in a 272 x 128 space, scaled to the view.
class HomeArt : public View {
  public:
    explicit HomeArt(HomeCardKind kind) : kind(kind) {
        setWidth(272);
        setHeight(128);
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              Style style, FrameContext* ctx) override {
        nvgSave(vg);
        nvgTranslate(vg, x, y);
        nvgScale(vg, width / 272, height / 128);
        switch (kind) {
        case HomeCardKind::PCS:
            drawPcs(vg);
            break;
        case HomeCardKind::FAVORITES:
            drawFavorites(vg);
            break;
        case HomeCardKind::SETTINGS:
            drawSettings(vg);
            break;
        case HomeCardKind::ABOUT:
            drawAbout(vg);
            break;
        }
        nvgRestore(vg);
    }

  private:
    HomeCardKind kind;

    void fillRect(NVGcontext* vg, float x, float y, float w, float h, float r,
                  unsigned color) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, w, h, r);
        nvgFillColor(vg, a(hex(color)));
        nvgFill(vg);
    }

    void circle(NVGcontext* vg, float x, float y, float r, unsigned color) {
        nvgBeginPath(vg);
        nvgCircle(vg, x, y, r);
        nvgFillColor(vg, a(hex(color)));
        nvgFill(vg);
    }

    void line(NVGcontext* vg, float x1, float y1, float x2, float y2,
              unsigned color, float width) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x1, y1);
        nvgLineTo(vg, x2, y2);
        nvgStrokeColor(vg, a(hex(color)));
        nvgStrokeWidth(vg, width);
        nvgStroke(vg);
    }

    void ground(NVGcontext* vg) { line(vg, 0, 110, 272, 110, 0x6B74B8, 2); }

    void stand(NVGcontext* vg, float x, unsigned color) {
        nvgBeginPath(vg);
        nvgRect(vg, x + 28, 78, 12, 8);
        nvgMoveTo(vg, x + 22, 88);
        nvgLineTo(vg, x + 46, 88);
        nvgStrokeColor(vg, a(hex(color)));
        nvgStrokeWidth(vg, 2);
        nvgStroke(vg);
    }

    // Three monitors: online, running a game, asleep.
    void drawPcs(NVGcontext* vg) {
        fillRect(vg, 0, 0, 272, 128, 11, 0x2E3466);
        for (int i = 0; i < 2; i++) {
            const float x = 22 + i * 80;
            fillRect(vg, x, 30, 68, 48, 6, 0x3B4280);
            circle(vg, x + 46, 52, 10, 0xF4B860);
            if (i == 1)
                fillRect(vg, x + 6, 36, 22, 9, 4.5f, 0xF4B860);
            line(vg, x, 66, x + 68, 66, 0x6B74B8, 2);
            stand(vg, x, 0x6B74B8);
        }
        fillRect(vg, 182, 30, 68, 48, 6, 0x1A1E3D);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 182, 30, 68, 48, 6);
        nvgStrokeColor(vg, a(hex(0x3A4074)));
        nvgStrokeWidth(vg, 2);
        nvgStroke(vg);
        circle(vg, 226, 50, 9, 0x9DB8FF);
        circle(vg, 231, 46, 8, 0x1A1E3D);
        line(vg, 184, 66, 248, 66, 0x5A6198, 2);
        stand(vg, 182, 0x5A6198);
        ground(vg);
    }

    // Three covers fanned out, a star on the middle one.
    void drawFavorites(NVGcontext* vg) {
        fillRect(vg, 0, 0, 272, 128, 11, 0x2E3466);
        auto cover = [&](float cx, float cy, float angle, unsigned color) {
            nvgSave(vg);
            nvgTranslate(vg, cx, cy);
            nvgRotate(vg, nvgDegToRad(angle));
            fillRect(vg, -27, -36, 54, 72, 6, color);
            nvgRestore(vg);
        };
        cover(101, 62, -10, 0x4A2F5E);
        cover(171, 62, 10, 0x1F5C6B);
        cover(136, 54, 0, 0x3B4A8C);
        static const float star[][2] = {
            {136, 34},    {141.6f, 45.4f}, {154, 47.2f},    {145, 56},
            {147.1f, 68.4f}, {136, 62.6f}, {124.9f, 68.4f}, {127, 56},
            {118, 47.2f}, {130.4f, 45.4f}};
        nvgBeginPath(vg);
        nvgMoveTo(vg, star[0][0], star[0][1]);
        for (int i = 1; i < 10; i++)
            nvgLineTo(vg, star[i][0], star[i][1]);
        nvgClosePath(vg);
        nvgFillColor(vg, a(hex(0xF4B860)));
        nvgFill(vg);
        ground(vg);
    }

    // Three sliders.
    void drawSettings(NVGcontext* vg) {
        fillRect(vg, 0, 0, 272, 128, 11, 0x1F2347);
        nvgLineCap(vg, NVG_ROUND);
        const float rows[] = {38, 64, 90};
        const float knobs[] = {150, 110, 182};
        const unsigned colors[] = {0xF4B860, 0x9DB8FF, 0xF4B860};
        for (int i = 0; i < 3; i++) {
            line(vg, 62, rows[i], 210, rows[i], 0x3A4074, 6);
            line(vg, 62, rows[i], knobs[i], rows[i], colors[i], 6);
        }
        nvgLineCap(vg, NVG_BUTT);
        for (int i = 0; i < 3; i++)
            circle(vg, knobs[i], rows[i], 10, 0xF1F2FA);
    }

    // The Equinox mark: half moon, half sun.
    void drawAbout(NVGcontext* vg) {
        fillRect(vg, 0, 0, 272, 128, 11, 0x1A1E3D);
        circle(vg, 136, 64, 36, 0x9DB8FF);
        nvgBeginPath(vg);
        nvgArc(vg, 136, 64, 36, -NVG_PI / 2, NVG_PI / 2, NVG_CW);
        nvgClosePath(vg);
        nvgFillColor(vg, a(hex(0xF4B860)));
        nvgFill(vg);
    }
};

// Logo next to the name in the header.
class LogoMark : public View {
  public:
    LogoMark() {
        setWidth(30);
        setHeight(30);
    }
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              Style style, FrameContext* ctx) override {
        const float cx = x + width / 2, cy = y + height / 2, r = width / 2 - 2;
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, r);
        nvgFillColor(vg, a(hex(0x9DB8FF)));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgArc(vg, cx, cy, r, -NVG_PI / 2, NVG_PI / 2, NVG_CW);
        nvgClosePath(vg);
        nvgFillColor(vg, a(hex(0xF4B860)));
        nvgFill(vg);
    }
};

// MARK: - Options panel

class OptionButton : public PressableBox {
  public:
    std::function<void()> action;

    OptionButton() : PressableBox(Axis::ROW) {
        setHeight(56);
        setPadding(0, 18, 0, 18);
        setAlignItems(AlignItems::CENTER);
        setCornerRadius(14);
        setHighlightCornerRadius(16);
        setHideHighlightBackground(true);
        setBackgroundColor(themeColor("equinox/card"));
        setFocusable(true);
        setMarginBottom(10);
        addGestureRecognizer(new TapGestureRecognizer(this));

        icon = new IconLabel();
        icon->setFontSize(22);
        addView(icon);

        label = new BoldLabel();
        label->setFontSize(18);
        label->setMarginLeft(14);
        label->setSingleLine(true);
        label->setGrow(1.0f);
        label->setShrink(1.0f);
        addView(label);

        hint = new Label();
        hint->setFontSize(14);
        hint->setMarginLeft(10);
        hint->setSingleLine(true);
        hint->setTextColor(themeColor("equinox/muted"));
        addView(hint);

        registerClickAction([this](View*) {
            if (disabled)
                return true;
            press([this] {
                // The action may refill the panel, replacing this function.
                auto run = action;
                if (run)
                    run();
            });
            return true;
        });
    }

    void set(const HomeOption& option, bool first) {
        const NVGcolor danger = nvgRGB(0xFF, 0xA7, 0x9B);
        icon->setText(option.icon);
        icon->setTextColor(option.danger ? danger
                           : first       ? themeColor("equinox/sun")
                                         : themeColor("equinox/muted"));
        label->setText(option.label);
        label->setTextColor(option.danger ? danger : themeColor("brls/text"));
        hint->setText(option.hint);
        hint->setVisibility(option.hint.empty() ? Visibility::GONE
                                                : Visibility::VISIBLE);
        action = option.action;
    }

    // Shown but not selectable, e.g. "More consoles soon".
    void disable() {
        disabled = true;
        setFocusable(false);
        action = nullptr;
        icon->setTextColor(themeColor("brls/text_disabled"));
        label->setTextColor(themeColor("brls/text_disabled"));
    }

  private:
    bool disabled = false;
    IconLabel* icon;
    BoldLabel* label;
    Label* hint;
};
} // namespace

// MARK: - MotionBox

void MotionBox::set(float s, float a, float x) {
    scale.stop();
    alpha.stop();
    shiftX.stop();
    scale.reset(s);
    alpha.reset(a);
    shiftX.reset(x);
}

void MotionBox::animate(float s, float a, float x, int ms,
                        std::function<void()> done) {
    auto go = [ms](Animatable& value, float to) {
        value.stop();
        value.reset(value.getValue());
        value.addStep(to, ms, EasingFunction::cubicOut);
    };
    go(scale, s);
    go(shiftX, x);
    go(alpha, a);
    alpha.setEndCallback([done](bool finished) {
        if (finished && done)
            done();
    });
    scale.start();
    shiftX.start();
    alpha.start();
}

void MotionBox::frame(FrameContext* ctx) {
    const float a = alpha.getValue();
    if (a <= 0.004f)
        return;
    const float s = scale.getValue();
    const float dx = shiftX.getValue();
    if (s == 1.0f && a >= 1.0f && dx == 0.0f) {
        Box::frame(ctx);
        return;
    }

    NVGcontext* vg = ctx->vg;
    const float cx = getX() + getWidth() / 2;
    const float cy = getY() + getHeight() * originY;
    nvgSave(vg);
    if (s != 1.0f)
        nvgTextScaleSnap(vg, 0.06f);
    nvgTranslate(vg, cx + dx, cy);
    nvgScale(vg, s, s);
    nvgTranslate(vg, -cx, -cy);
    nvgGlobalAlpha(vg, a);
    Box::frame(ctx);
    nvgRestore(vg);
}

// MARK: - HomeCard

HomeCard::HomeCard(HomeCardKind kind) : PressableBox(Axis::COLUMN), kind(kind) {
    setWidth(300);
    setHeight(230);
    setPadding(14, 14, 14, 14);
    setCornerRadius(18);
    setBackgroundColor(themeColor("equinox/card"));
    setFocusable(true);
    addGestureRecognizer(new TapGestureRecognizer(this));

    addView(new HomeArt(kind));

    std::string title;
    NVGcolor color = themeColor("equinox/muted");
    switch (kind) {
    case HomeCardKind::PCS:
        title = "home/pcs"_i18n;
        color = themeColor("equinox/online");
        break;
    case HomeCardKind::FAVORITES:
        title = "home/favorites"_i18n;
        color = themeColor("equinox/sun");
        break;
    case HomeCardKind::SETTINGS:
        title = "home/settings"_i18n;
        break;
    case HomeCardKind::ABOUT:
        title = "home/about"_i18n;
        break;
    }

    auto* name = new BoldLabel();
    name->setFontSize(22);
    name->setMarginTop(14);
    name->setMarginLeft(4);
    name->setSingleLine(true);
    name->setText(title);
    addView(name);

    auto* row = new Box(Axis::ROW);
    row->setAlignItems(AlignItems::CENTER);
    row->setMarginTop(4);
    row->setMarginLeft(4);
    auto* dot = new Box();
    dot->setWidth(9);
    dot->setHeight(9);
    dot->setCornerRadius(4.5f);
    dot->setMarginRight(8);
    dot->setBackgroundColor(color);
    row->addView(dot);
    status = new Label();
    status->setFontSize(15);
    status->setSingleLine(true);
    status->setGrow(1.0f);
    status->setShrink(1.0f);
    status->setTextColor(color);
    row->addView(status);
    addView(row);

    registerClickAction([this](View*) {
        press([this] {
            if (onEnter)
                onEnter();
        });
        return true;
    });
    updateActionHint(BUTTON_A, kind == HomeCardKind::PCS ? "home/enter"_i18n
                                                         : "home/open"_i18n);
}

void HomeCard::setStatus(const std::string& text) { status->setText(text); }

// MARK: - HomeView

HomeView::HomeView() {
    instance = this;
    setAxis(Axis::COLUMN);

    header = new Box(Axis::ROW);
    header->setPositionType(PositionType::ABSOLUTE);
    header->setPositionTop(34);
    header->setPositionLeft(44);
    header->setAlignItems(AlignItems::CENTER);
    header->addView(new LogoMark());
    auto* title = new BoldLabel();
    title->setFontSize(24);
    title->setMarginLeft(12);
    title->setText("title"_i18n);
    header->addView(title);
    addView(header);

    homeLayer = makeLayer(home, false);
    pcLayer = makeLayer(pcs, true);
    addView(homeLayer);
    addView(pcLayer);
    pcLayer->set(0.7f, 0, 0);
    pcLayer->setVisibility(Visibility::INVISIBLE);

    // Options panel, right of the entered card.
    panel = new MotionBox(Axis::COLUMN);
    panel->setPositionType(PositionType::ABSOLUTE);
    panel->setPositionTop(kPanelTop);
    panel->setPositionLeft(kPanelLeft);
    panel->setWidth(kPanelWidth);

    panelName = new BoldLabel();
    panelName->setFontSize(30);
    panelName->setSingleLine(true);
    panel->addView(panelName);

    auto* statusRow = new Box(Axis::ROW);
    statusRow->setAlignItems(AlignItems::CENTER);
    statusRow->setMarginTop(8);
    panelDot = new Box();
    panelDot->setWidth(10);
    panelDot->setHeight(10);
    panelDot->setCornerRadius(5);
    panelDot->setMarginRight(10);
    statusRow->addView(panelDot);
    panelStatus = new Label();
    panelStatus->setFontSize(17);
    panelStatus->setSingleLine(true);
    statusRow->addView(panelStatus);
    panel->addView(statusRow);

    panelDetail = new Label();
    panelDetail->setFontSize(16);
    panelDetail->setMarginTop(6);
    panelDetail->setTextColor(themeColor("equinox/muted"));
    panel->addView(panelDetail);

    panelOptions = new Box(Axis::COLUMN);
    panelOptions->setMarginTop(24);
    optionSlots.clear();
    for (size_t r = 0; r < kOptionRows; r++) {
        auto* row = new Box(Axis::ROW);
        std::vector<PressableBox*> slots;
        for (int c = 0; c < 2; c++) {
            auto* button = new OptionButton();
            row->addView(button);
            slots.push_back(button);
        }
        panelOptions->addView(row);
        optionSlots.push_back(slots);
    }
    panel->addView(panelOptions);
    panel->set(1, 0, 60);
    panel->setVisibility(Visibility::GONE);
    addView(panel);

    buildHomeCards();
    buildHostCards();

    registerAction("home/back"_i18n, BUTTON_B, [this](View*) {
        if (entered)
            leave();
        else if (level == Level::PCS)
            closePcs();
        else
            confirmExit();
        return true;
    });

    updateHomeCards();
    updateShelfInfo();
    updateBackAction();
}

HomeView::~HomeView() {
    if (instance == this)
        instance = nullptr;
}

MotionBox* HomeView::makeLayer(Shelf& shelf, bool forPcs) {
    auto* layer = new MotionBox(Axis::COLUMN);
    layer->setPositionType(PositionType::ABSOLUTE);
    layer->setPositionTop(0);
    layer->setPositionLeft(0);
    layer->setPositionRight(0);
    layer->setPositionBottom(0);
    layer->setAlignItems(AlignItems::STRETCH);
    layer->originY = 0.42f;

    // Breadcrumb: "INÍCIO", or "INÍCIO › SEUS PCS" with the last part lit.
    auto* crumbRow = new Box(Axis::ROW);
    crumbRow->setJustifyContent(JustifyContent::CENTER);
    crumbRow->setAlignItems(AlignItems::CENTER);
    crumbRow->setMarginTop(104);
    crumbRow->setHeight(20);
    shelf.crumb = new BoldLabel();
    shelf.crumb->setFontSize(15);
    shelf.crumb->setSingleLine(true);
    shelf.crumb->setTextColor(themeColor("equinox/muted"));
    shelf.crumb->setText(forPcs ? "home/crumb"_i18n + "   ›   " : "home/crumb"_i18n);
    crumbRow->addView(shelf.crumb);
    if (forPcs) {
        auto* lit = new BoldLabel();
        lit->setFontSize(15);
        lit->setSingleLine(true);
        lit->setTextColor(themeColor("equinox/sun"));
        std::string text = "home/pcs"_i18n;
        std::transform(text.begin(), text.end(), text.begin(), ::toupper);
        lit->setText(text);
        crumbRow->addView(lit);
    }
    layer->addView(crumbRow);

    shelf.carousel = new CoverCarousel(ShelfStyle::cards());
    shelf.carousel->setMarginTop(16);
    shelf.carousel->onSelect = [this](int) { updateShelfInfo(); };
    layer->addView(shelf.carousel);

    shelf.info = new Label();
    shelf.info->setFontSize(18);
    shelf.info->setSingleLine(true);
    shelf.info->setHorizontalAlign(HorizontalAlign::CENTER);
    shelf.info->setMarginTop(30);
    layer->addView(shelf.info);

    shelf.detail = new Label();
    shelf.detail->setFontSize(15);
    shelf.detail->setSingleLine(true);
    shelf.detail->setHorizontalAlign(HorizontalAlign::CENTER);
    shelf.detail->setTextColor(themeColor("equinox/muted"));
    shelf.detail->setMarginTop(6);
    layer->addView(shelf.detail);

    shelf.dots = new CarouselDots();
    shelf.dots->setMarginTop(16);
    layer->addView(shelf.dots);
    return layer;
}

void HomeView::buildHomeCards() {
    const HomeCardKind kinds[] = {HomeCardKind::PCS, HomeCardKind::FAVORITES,
                                  HomeCardKind::SETTINGS, HomeCardKind::ABOUT};
    for (HomeCardKind kind : kinds) {
        auto* card = new HomeCard(kind);
        const int index = (int)homeCards.size();
        card->onEnter = [this, card, index] {
            if (home.carousel->selectedIndex() != index) {
                Application::giveFocus(card);
                return;
            }
            if (card->getKind() == HomeCardKind::PCS)
                openPcs();
            else
                enter();
        };
        homeCards.push_back(card);
        home.carousel->addView(card);
    }
}

void HomeView::buildHostCards() {
    pcs.carousel->clearViews();
    hostCards.clear();

    const auto hosts = Settings::instance().hosts();
    for (size_t i = 0; i < hosts.size(); i++) {
        auto* card = new HostCard(hosts[i]);
        const int index = (int)i;
        card->onEnter = [this, card, index] {
            if (pcs.carousel->selectedIndex() != index) {
                Application::giveFocus(card);
                return;
            }
            enter();
        };
        card->onChanged = [this, card] {
            updateHomeCards();
            updateShelfInfo();
            // Keep the options of the last known state while it re-queries.
            if (entered && level == Level::PCS && selectedHost() == card &&
                card->getState() != HostCardState::FETCHING)
                fillPanel();
        };
        hostCards.push_back(card);
        pcs.carousel->addView(card);
    }

    consoleCards.clear();
#if defined(PLATFORM_SWITCH)
    for (const emu::Console& console : emu::consoles()) {
        if (!runnerInstalled(console))
            continue;
        auto* card = new ConsoleCard(console);
        const int index = (int)(hostCards.size() + consoleCards.size());
        card->onEnter = [this, card, index] {
            if (pcs.carousel->selectedIndex() != index) {
                Application::giveFocus(card);
                return;
            }
            enter();
        };
        auto latest = latestManifests().find(console.slug);
        if (latest != latestManifests().end())
            card->setLatest(latest->second);
        card->onChanged = [this, card] {
            updateHomeCards();
            updateShelfInfo();
            if (entered && level == Level::PCS && selectedConsole() == card)
                fillPanel();
        };
        consoleCards.push_back(card);
        pcs.carousel->addView(card);
    }
#endif
    pcs.carousel->addView(
        new AddHostCard(hosts.empty() && consoleCards.empty()));
}

void HomeView::rebuildHosts(int selectIndex) {
    if (entered && level == Level::PCS) {
        entered = false;
        pcs.carousel->setEntered(false);
        panel->set(1, 0, 60);
        panel->setVisibility(Visibility::GONE);
        chromeFade.reset(1.0f);
    }

    View* focus = Application::getCurrentFocus();
    const bool focusHere =
        focus && (isInside(focus, pcLayer) || isInside(focus, panel));
    // The PC cards are about to be deleted: park the focus on a card that
    // stays while they are rebuilt.
    if (focusHere)
        Application::giveFocus(homeCards[0]);

    int index = selectIndex >= 0 ? selectIndex : pcs.carousel->selectedIndex();
    buildHostCards();
    index = std::clamp(index, 0, (int)pcs.carousel->count() - 1);
    pcs.carousel->jumpTo(index);
    if (level == Level::PCS && focusHere)
        Application::giveFocus(pcs.carousel->getChildren()[index]);

    updateHomeCards();
    updateShelfInfo();
    updateBackAction();
}

HostCard* HomeView::selectedHost() {
    const int index = pcs.carousel->selectedIndex();
    return index >= 0 && index < (int)hostCards.size() ? hostCards[index]
                                                       : nullptr;
}

ConsoleCard* HomeView::selectedConsole() {
    const int index = pcs.carousel->selectedIndex() - (int)hostCards.size();
    return index >= 0 && index < (int)consoleCards.size() ? consoleCards[index]
                                                          : nullptr;
}

void HomeView::updateHomeCards() {
    const auto hosts = Settings::instance().hosts();
    int running = 0;
    for (HostCard* card : hostCards)
        if (card->getState() == HostCardState::RUNNING)
            running++;
    std::string pcsStatus;
    if (!hosts.empty())
        pcsStatus = pcsText((int)hosts.size());
    if (!consoleCards.empty())
        pcsStatus += (pcsStatus.empty() ? "" : "  ·  ") +
                     consolesText((int)consoleCards.size());
    if (pcsStatus.empty())
        pcsStatus = "home/pcs_none"_i18n;
    if (running > 0)
        pcsStatus += "  ·  " + brls::getStr("home/pcs_running", running);
    homeCards[0]->setStatus(pcsStatus);

    const FavoriteSummary fav = favoriteSummary();
    homeCards[1]->setStatus(
        fav.games == 0 ? "home/fav_empty"_i18n
                       : brls::getStr("home/fav_status", gamesText(fav.games),
                                      pcsText((int)fav.perHost.size())));
    homeCards[2]->setStatus(streamQuality());
    homeCards[3]->setStatus(brls::getStr("home/about_status", APP_VERSION));
}

void HomeView::updateShelfInfo() {
    // Home shelf.
    const int sel = home.carousel->selectedIndex();
    std::string info, detail;
    switch (sel) {
    case 0: {
        const int count = (int)hostCards.size();
        const int consoles = (int)consoleCards.size();
        info = count == 0   ? ""
               : count == 1 ? "home/pcs_info_one"_i18n
                            : brls::getStr("home/pcs_info", count);
        if (consoles > 0)
            info += (info.empty() ? "" : "  ·  ") + consolesText(consoles);
        if (info.empty())
            info = "home/pcs_none"_i18n;
        detail = count == 0 && consoles == 0 ? "home/pcs_detail_empty"_i18n
                                             : "home/pcs_detail"_i18n;
        for (HostCard* card : hostCards) {
            if (card->getState() == HostCardState::RUNNING &&
                !card->getRunningApp().name.empty()) {
                detail = brls::getStr("home/pcs_detail_running",
                                      card->getHost().hostname,
                                      card->getRunningApp().name);
                break;
            }
        }
        break;
    }
    case 1: {
        const FavoriteSummary fav = favoriteSummary();
        info = fav.games == 0   ? "home/fav_empty"_i18n
               : fav.games == 1 ? "home/fav_info_one"_i18n
                                : brls::getStr("home/fav_info", fav.games);
        detail = fav.games == 0
                     ? "home/fav_empty_detail"_i18n
                     : brls::getStr("home/fav_from", joinNames(fav.names));
        break;
    }
    case 2:
        info = "home/settings"_i18n;
        detail = "home/settings_detail"_i18n;
        break;
    default:
        info = brls::getStr("about/version", APP_VERSION);
        detail = "home/about_detail"_i18n;
        break;
    }
    home.info->setText(info);
    home.detail->setText(detail);
    home.dots->setState(home.carousel->count(), sel);

    // PC shelf.
    if (HostCard* card = selectedHost()) {
        const std::string text = card->infoText();
        pcs.info->setText(text.empty() || text == " " ? card->statusText()
                                                      : text);
        pcs.detail->setText(card->detailText());
#if defined(PLATFORM_SWITCH)
    } else if (ConsoleCard* console = selectedConsole()) {
        pcs.info->setText(console->statusText());
        pcs.detail->setText(console->detailText() + "  ·  " +
                            console->infoText());
    } else {
        pcs.info->setText("home/add_title"_i18n);
        pcs.detail->setText("home/add_body"_i18n);
#else
    } else {
        pcs.info->setText("hosts/add_title"_i18n);
        pcs.detail->setText("hosts/add_body"_i18n);
#endif
    }
    pcs.dots->setState(pcs.carousel->count(), pcs.carousel->selectedIndex());
}

void HomeView::updateBackAction() {
    const bool back = entered || level == Level::PCS;
    updateActionHint(BUTTON_B, back ? "home/back"_i18n : "home/exit"_i18n);
    Application::getGlobalHintsUpdateEvent()->fire();
}

void HomeView::confirmExit() {
    auto* dialog = new Dialog("home/exit_message"_i18n);
    dialog->addButton("common/cancel"_i18n, [] {});
    dialog->addButton("home/exit"_i18n, [] { Application::quit(); });
    dialog->open();
}

void HomeView::openPcs() {
    if (level == Level::PCS)
        return;
    level = Level::PCS;

    pcLayer->setVisibility(Visibility::VISIBLE);
    pcLayer->set(0.7f, 0, 0);
    pcLayer->animate(1, 1, 0, kZoomMs);
    // Zoom through the home shelf into the PCs one.
    homeLayer->animate(1.7f, 0, 0, kZoomMs, [this] {
        if (level == Level::PCS)
            homeLayer->setVisibility(Visibility::INVISIBLE);
    });

    const int index = std::clamp(pcs.carousel->selectedIndex(), 0,
                                 (int)pcs.carousel->count() - 1);
    Application::giveFocus(pcs.carousel->getChildren()[index]);
    updateShelfInfo();
    updateBackAction();
    checkConsoleUpdates();
}

void HomeView::closePcs() {
    if (level == Level::HOME)
        return;
    level = Level::HOME;

    homeLayer->setVisibility(Visibility::VISIBLE);
    homeLayer->animate(1, 1, 0, kZoomMs);
    pcLayer->animate(0.7f, 0, 0, kZoomMs, [this] {
        if (level == Level::HOME)
            pcLayer->setVisibility(Visibility::INVISIBLE);
    });

    Application::giveFocus(homeCards[0]);
    updateShelfInfo();
    updateBackAction();
}

void HomeView::enter() {
    if (entered) {
        leave();
        return;
    }
    entered = true;
    fillPanel();

    Shelf& shelf = level == Level::PCS ? pcs : home;
    shelf.carousel->setEntered(true, kEnterOffset);

    panel->setVisibility(Visibility::VISIBLE);
    panel->set(1, 0, 60);
    panel->animate(1, 1, 0, 420);

    chromeFade.reset(chromeFade.getValue());
    chromeFade.addStep(0.0f, 260, EasingFunction::quadraticOut);
    chromeFade.start();

    if (View* first = getDefaultFocus())
        Application::giveFocus(first);
    updateBackAction();
}

void HomeView::leave(bool focusCard) {
    if (!entered)
        return;
    entered = false;

    Shelf& shelf = level == Level::PCS ? pcs : home;
    shelf.carousel->setEntered(false);

    panel->animate(1, 0, 60, 240, [this] {
        if (!entered)
            panel->setVisibility(Visibility::GONE);
    });

    chromeFade.reset(chromeFade.getValue());
    chromeFade.addStep(1.0f, 320, EasingFunction::quadraticOut);
    chromeFade.start();

    if (focusCard) {
        const int index = shelf.carousel->selectedIndex();
        if (index >= 0 && index < (int)shelf.carousel->count())
            Application::giveFocus(shelf.carousel->getChildren()[index]);
    }
    updateBackAction();
}

void HomeView::fillPanel() {
    NVGcolor color = themeColor("equinox/muted");
    std::vector<HomeOption> options;
    int columns = 1;

    if (level == Level::PCS) {
        if (HostCard* card = selectedHost()) {
            panelName->setText(card->getHost().hostname);
            panelStatus->setText(card->statusText());
            color = card->statusColor();
            std::string detail = card->detailText();
            const std::string info = card->infoText();
            if (!info.empty() && info != " ")
                detail += "  ·  " + info;
            panelDetail->setText(detail);
            options = hostOptions(card);
#if defined(PLATFORM_SWITCH)
        } else if (ConsoleCard* card = selectedConsole()) {
            panelName->setText(card->console().name);
            panelStatus->setText(card->statusText());
            color = card->statusColor();
            panelDetail->setText(card->detailText() + "  ·  " +
                                 card->infoText());
            options = consoleOptions(card);
#endif
        } else {
            return;
        }
    } else {
        const int sel = home.carousel->selectedIndex();
        if (sel < 0 || sel >= (int)homeCards.size())
            return;
        switch (homeCards[sel]->getKind()) {
        case HomeCardKind::FAVORITES: {
            const FavoriteSummary fav = favoriteSummary();
            panelName->setText("home/favorites"_i18n);
            panelStatus->setText(
                fav.games == 0
                    ? "home/fav_empty"_i18n
                    : brls::getStr("home/fav_status", gamesText(fav.games),
                                   pcsText((int)fav.perHost.size())));
            color = themeColor("equinox/sun");
            panelDetail->setText(
                fav.games == 0
                    ? "home/fav_empty_detail"_i18n
                    : brls::getStr("home/fav_from", joinNames(fav.names)));
            options = favoritesOptions();
            break;
        }
        case HomeCardKind::SETTINGS:
            panelName->setText("home/settings"_i18n);
            panelStatus->setText(streamQuality());
            panelDetail->setText("home/settings_detail"_i18n);
            options = settingsOptions();
            columns = 2;
            break;
        case HomeCardKind::ABOUT:
            panelName->setText("home/about"_i18n);
            panelStatus->setText(brls::getStr("home/about_status", APP_VERSION));
            panelDetail->setText("home/about_detail"_i18n);
            options = aboutOptions();
            break;
        case HomeCardKind::PCS:
            return;
        }
    }

    panelDot->setBackgroundColor(color);
    panelStatus->setTextColor(color);
    setPanelOptions(options, columns);
}

void HomeView::setPanelOptions(const std::vector<HomeOption>& options,
                               int columns) {
    const float width =
        columns == 2 ? (kPanelWidth - kOptionGap) / 2 : kPanelWidth;
    size_t next = 0;
    auto& rows = panelOptions->getChildren();
    for (size_t r = 0; r < optionSlots.size(); r++) {
        bool rowVisible = false;
        for (int c = 0; c < 2; c++) {
            auto* button = static_cast<OptionButton*>(optionSlots[r][c]);
            if (c < columns && next < options.size()) {
                button->set(options[next], next == 0);
                button->setWidth(width);
                button->setMarginRight(c == 0 && columns == 2 ? kOptionGap : 0);
                button->setVisibility(Visibility::VISIBLE);
                rowVisible = true;
                next++;
            } else {
                button->setVisibility(Visibility::GONE);
            }
        }
        rows[r]->setVisibility(rowVisible ? Visibility::VISIBLE
                                          : Visibility::GONE);
    }

    // Focus on an option that just went away: move it to the first one.
    View* focus = Application::getCurrentFocus();
    if (entered && focus && isInside(focus, panelOptions) &&
        focus->getVisibility() != Visibility::VISIBLE) {
        if (View* first = getDefaultFocus())
            Application::giveFocus(first);
    }
}

View* HomeView::getDefaultFocus() {
    if (entered) {
        for (auto& row : optionSlots)
            for (PressableBox* button : row)
                if (button->getVisibility() == Visibility::VISIBLE)
                    return button;
    }
    Shelf& shelf = level == Level::PCS ? pcs : home;
    if (shelf.carousel->count() == 0)
        return nullptr;
    const int index = std::clamp(shelf.carousel->selectedIndex(), 0,
                                 (int)shelf.carousel->count() - 1);
    return shelf.carousel->getChildren()[index];
}

View* HomeView::getNextFocus(FocusDirection direction, View* currentView) {
    return nullptr;
}

std::vector<HomeOption> HomeView::favoritesOptions() {
    const FavoriteSummary fav = favoriteSummary();
    const auto hosts = Settings::instance().hosts();
    std::vector<HomeOption> options;
    options.push_back({"", "home/fav_all"_i18n, std::to_string(fav.games),
                       [this] { presentWithHeader(this, new FavoritesView(-1)); }});
    for (const auto& [index, count] : fav.perHost) {
        const int hostIndex = (int)index;
        options.push_back(
            {"", hosts[index].hostname, std::to_string(count),
             [this, hostIndex] {
                 presentWithHeader(this, new FavoritesView(hostIndex));
             }});
    }
    return options;
}

std::vector<HomeOption> HomeView::settingsOptions() {
    // Same ids and Material Icons as the settings menu.
    static const std::pair<const char*, const char*> groups[] = {
        {"video", ""},
#if defined(PLATFORM_SWITCH)
        {"handheld", ""},
#endif
        {"audio", ""},
        {"controls", ""},  {"shortcuts", ""},
        {"input", ""},     {"system", ""},
    };

    auto open = [this](const std::string& group) {
        auto* settings = new SettingsTab(group);
        settings->getAppletFrameItem()->title = "home/settings"_i18n;
        presentWithHeader(this, settings);
    };

    std::vector<HomeOption> options;
    for (const auto& [id, icon] : groups) {
        const std::string group = id;
        options.push_back(
            {icon, brls::getStr(std::string("settings/groups/") + id + "/title"),
             "", [open, group] { open(group); }});
    }
    return options;
}

std::vector<HomeOption> HomeView::aboutOptions() {
    std::vector<HomeOption> options;
    options.push_back({"", "about/link_github"_i18n, "", [] {
                           Application::getPlatform()->openBrowser(
                               "https://github.com/luizfbalves/Equinox");
                       }});
    options.push_back({"", "about/fork_title"_i18n, "", [] {
                           Application::getPlatform()->openBrowser(
                               "https://github.com/XITRIX/Moonlight-Switch");
                       }});
    options.push_back({"", "home/about_more"_i18n, "", [this] {
                           auto* about = new AboutTab();
                           about->getAppletFrameItem()->title = "home/about"_i18n;
                           presentWithHeader(this, about);
                       }});
    return options;
}

std::vector<HomeOption> HomeView::hostOptions(HostCard* card) {
    std::vector<HomeOption> options;
    const int hostIndex = pcs.carousel->selectedIndex();

    int favorites = 0;
    for (const App& app : card->getHost().favorites)
        if (!isHiddenApp(app.name))
            favorites++;
    auto favoritesOption = [this, hostIndex, favorites]() -> HomeOption {
        return {"", "home/host_favorites"_i18n, std::to_string(favorites),
                [this, hostIndex] {
                    presentWithHeader(this, new FavoritesView(hostIndex));
                }};
    };

    switch (card->getState()) {
    case HostCardState::ONLINE:
        options.push_back({"", "hosts/library"_i18n, "",
                           [card] { card->openLibrary(); }});
        if (favorites > 0)
            options.push_back(favoritesOption());
        options.push_back({"", "hosts/steam_import"_i18n, "",
                           [card] { card->importSteamGames(); }});
        options.push_back({"", "capacity/action"_i18n, "",
                           [card] { card->openCapacityTest(); }});
        break;
    case HostCardState::RUNNING:
        options.push_back(
            {"",
             brls::getStr("home/host_resume", card->getRunningApp().name.empty()
                                                  ? "hosts/running_unknown"_i18n
                                                  : card->getRunningApp().name),
             "", [card] { card->resumeRunningApp(); }});
        options.push_back({"", "hosts/library"_i18n, "",
                           [card] { card->openLibrary(); }});
        options.push_back({"", "home/host_quit"_i18n, "",
                           [card] { card->quitRunningApp(); }});
        options.push_back({"", "capacity/action"_i18n, "",
                           [card] { card->openCapacityTest(); }});
        break;
    case HostCardState::ASLEEP:
        options.push_back({"", "hosts/action_wake"_i18n, "",
                           [card] { card->wake(); }});
        break;
    case HostCardState::UNREACHABLE:
        options.push_back({"", "hosts/action_retry"_i18n, "",
                           [card] { card->refresh(); }});
        break;
    case HostCardState::FETCHING:
        break;
    }

    options.push_back({"", "host/rename"_i18n, "",
                       [card] { card->rename(); }});
    HomeOption remove{"", "home/host_remove"_i18n, "",
                      [card] { card->remove(); }};
    remove.danger = true;
    options.push_back(remove);
    return options;
}

// MARK: - Consoles

void HomeView::openConsole(const emu::Console& console,
                           const std::string& selectRom) {
    (void)selectRom;
    // Task 7: push ConsoleShelfView
    Application::notify(console.name);
}

void HomeView::showAddChoices() {
    if (!instance)
        return;
    auto openAddHost = [] {
        if (!instance)
            return;
        auto* screen = new AddHostTab();
        screen->getAppletFrameItem()->title = "hosts/add_screen"_i18n;
        presentWithHeader(instance, screen);
    };

#if defined(PLATFORM_SWITCH)
    Style style = Application::getStyle();
    auto* box = new Box(Axis::COLUMN);
    box->setPadding(style["brls/dialog/paddingTopBottom"],
                    style["brls/dialog/paddingLeftRight"], 18,
                    style["brls/dialog/paddingLeftRight"]);

    auto* title = new BoldLabel();
    title->setFontSize(24);
    title->setMarginBottom(18);
    title->setText("home/add_title"_i18n);
    box->addView(title);

    auto* dialog = new Dialog(box);
    auto addChoice = [box](const HomeOption& option, bool first) {
        auto* button = new OptionButton();
        button->set(option, first);
        box->addView(button);
        return button;
    };

    // A row acts once its press animation ends; B or Cancel may have closed
    // the dialog meanwhile, and closing it twice frees it twice.
    OptionButton* pcButton =
        addChoice({"", "emu/add_pc"_i18n, "emu/add_pc_hint"_i18n,
                   [dialog, openAddHost] {
                       if (isDialogClosing(dialog))
                           return;
                       dialog->close(openAddHost);
                   }},
                  true);

    for (const emu::Console& console : emu::consoles()) {
        const emu::Console* target = &console;
        std::string hint;
        if (!runnerInstalled(console)) {
            auto latest = latestManifests().find(console.slug);
            hint = brls::getStr("emu/add_console_hint",
                                latest != latestManifests().end()
                                    ? emuSizeText(latest->second.size)
                                    : std::string("11 MB"));
        }
        addChoice({"", console.name, hint, [dialog, target] {
                       if (isDialogClosing(dialog))
                           return;
                       dialog->close([target] {
                           if (!instance)
                               return;
                           if (runnerInstalled(*target))
                               instance->openConsole(*target);
                           else
                               instance->installConsole(*target);
                       });
                   }},
                  false);
    }

    addChoice({"", "emu/more_soon"_i18n, "", nullptr}, false)->disable();

    dialog->addButton("common/cancel"_i18n, [] {});
    // Open on the first choice, not on Cancel.
    dialog->setLastFocusedView(pcButton);
    dialog->open();
#else
    openAddHost();
#endif
}

#if defined(PLATFORM_SWITCH)

std::vector<HomeOption> HomeView::consoleOptions(ConsoleCard* card) {
    const emu::Console* console = &card->console();
    std::vector<HomeOption> options;
    options.push_back(
        {"", "emu/games"_i18n,
         card->gameCount() < 0 ? "" : std::to_string(card->gameCount()),
         [this, console] { openConsole(*console); }});
    if (card->action() == emu::RunnerAction::Update && card->latest())
        options.push_back({"", "emu/update"_i18n,
                           "v" + card->latest()->version,
                           [this, console] { installConsole(*console); }});
    options.push_back({"", "emu/rescan"_i18n, "",
                       [card] { card->refresh(); }});
    HomeOption remove{"", "emu/remove"_i18n, "",
                      [this, console] { removeConsole(*console); }};
    remove.danger = true;
    options.push_back(remove);
    return options;
}

void HomeView::installConsole(const emu::Console& target) {
    if (installing.exchange(true))
        return;
    const emu::Console* console = &target;
    const bool first = !runnerInstalled(target);
    const std::string title = brls::getStr(
        first ? "emu/installing" : "emu/updating", target.name);

    // Touched from the download thread (cancel, percent) and the UI thread.
    struct State {
        std::atomic<bool> cancel{false};
        std::atomic<bool> closed{false};
        std::atomic<int> percent{-1};
    };
    auto state = std::make_shared<State>();

    Label* text = nullptr;
    Dialog* loader = createLoadingDialog(
        title, [state] { state->cancel.store(true); }, &text);
    loader->open();

    // UI thread only. The dialog and its label are freed once it is
    // cancelled or closed, so this is checked before touching either.
    auto dialogAlive = [state, loader] {
        return !state->cancel.load() && !state->closed.load() &&
               !isDialogClosing(loader);
    };
    // Closes the dialog, then runs `then` (an error, a toast, the shelf).
    // A cancelled install ends silently.
    auto finish = [state, loader, dialogAlive](std::function<void()> then) {
        brls::sync([state, loader, dialogAlive, then] {
            installing.store(false);
            if (!dialogAlive())
                return;
            state->closed.store(true);
            loader->close(then ? then : std::function<void()>([] {}));
        });
    };

    brls::async([state, loader, text, console, title, first, dialogAlive,
                 finish] {
        const emu::ConsolePaths paths = emu::pathsFor(*console);
        std::string error;
        emu::RunnerError code = emu::RunnerError::None;
        std::optional<emu::Manifest> latest =
            emu::fetchLatest(*console, error, &code);
        if (state->cancel.load()) {
            finish(nullptr);
            return;
        }
        if (!latest) {
            finish([console, code, error] {
                showError(brls::getStr("emu/install_error", console->name,
                                       runnerErrorText(code, error)));
            });
            return;
        }

        const emu::Manifest manifest = *latest;
        brls::sync([console, manifest] {
            latestManifests()[console->slug] = manifest;
        });

        // A first install ignores a stale installed.json left without its
        // runner.bin.
        const std::optional<emu::Installed> installed =
            first ? std::nullopt : emu::readInstalled(paths.installed);
        switch (emu::decide(installed, manifest, console->protocol)) {
        case emu::RunnerAction::NeedsNewerEquinox:
            finish([] { showAlert("emu/needs_newer"_i18n); });
            return;
        case emu::RunnerAction::UpToDate:
            finish([console] {
                Application::notify("emu/up_to_date"_i18n);
                if (instance)
                    instance->refreshConsole(*console);
            });
            return;
        default:
            break;
        }

        // "Snes9x · v1.0.0 · 11,4 MB · 62%", redrawn once per whole percent.
        const std::string prefix = emuRunnerName(manifest) + "  ·  v" +
                                   manifest.version + "  ·  " +
                                   emuSizeText(manifest.size) + "  ·  ";
        auto progress = [state, text, title, prefix, dialogAlive](
                            uint64_t done, uint64_t total) {
            if (state->cancel.load())
                return false;
            const int percent =
                total > 0 ? (int)std::min<uint64_t>(100, done * 100 / total) : 0;
            if (state->percent.exchange(percent) != percent) {
                const std::string line =
                    title + "\n" + prefix + std::to_string(percent) + "%";
                brls::sync([text, line, dialogAlive] {
                    if (dialogAlive())
                        text->setText(line);
                });
            }
            return true;
        };
        progress(0, manifest.size);

        const emu::InstallResult result =
            emu::installRunner(*console, manifest, progress);
        if (!result.ok) {
            if (result.cancelled || state->cancel.load())
                finish(nullptr);
            else
                finish([console, code = result.code, error = result.error] {
                    showError(brls::getStr("emu/install_error", console->name,
                                           runnerErrorText(code, error)));
                });
            return;
        }
        // Installed, even if Cancel was pressed as the download ended: the
        // shelf follows what is on the SD card either way.
        brls::sync([console, first, dialogAlive, state, loader] {
            installing.store(false);
            if (!instance)
                return;
            if (!dialogAlive()) {
                if (first)
                    instance->rebuildHosts();
                else
                    instance->refreshConsole(*console);
                return;
            }
            state->closed.store(true);
            loader->close([console, first] {
                if (instance)
                    instance->consoleInstalled(*console, first);
            });
        });
    });
}

void HomeView::consoleInstalled(const emu::Console& console, bool first) {
    emu::ensureFolders(emu::pathsFor(console));
    if (!first) {
        refreshConsole(console);
        Application::notify("emu/updated"_i18n);
        return;
    }

    // Land on the new card, then open its shelf.
    int index = (int)hostCards.size();
    for (const emu::Console& other : emu::consoles()) {
        if (&other == &console)
            break;
        if (runnerInstalled(other))
            index++;
    }
    rebuildHosts(index);
    openConsole(console);
}

void HomeView::refreshConsole(const emu::Console& console) {
    auto latest = latestManifests().find(console.slug);
    for (ConsoleCard* card : consoleCards) {
        if (&card->console() != &console)
            continue;
        if (latest != latestManifests().end())
            card->setLatest(latest->second);
        card->refresh();
    }
}

void HomeView::removeConsole(const emu::Console& target) {
    const emu::Console* console = &target;
    auto* confirm =
        new Dialog(brls::getStr("emu/remove_message", target.name));
    confirm->addButton("common/cancel"_i18n, [] {});
    confirm->addButton("common/remove"_i18n, [console] {
        if (installing.load())
            return;
        emu::removeRunner(*console);
        latestManifests().erase(console->slug);
        if (instance)
            instance->rebuildHosts();
    });
    confirm->open();
}

void HomeView::checkConsoleUpdates() {
    if (consoleCards.empty() || checkingUpdates.exchange(true))
        return;

    // One request per launch: a console already checked in this run keeps
    // that answer.
    std::vector<const emu::Console*> targets;
    for (ConsoleCard* card : consoleCards)
        if (!latestManifests().count(card->console().slug))
            targets.push_back(&card->console());
    if (targets.empty()) {
        checkingUpdates.store(false);
        return;
    }

    brls::async([targets] {
        for (const emu::Console* console : targets) {
            const emu::ConsolePaths paths = emu::pathsFor(*console);
            const auto installed = emu::readInstalled(paths.installed);
            const int64_t now = (int64_t)std::time(nullptr);
            if (!installed || !emu::shouldCheck(installed->lastCheck, now))
                continue;

            std::string error;
            const auto latest = emu::fetchLatest(*console, error);
            if (!latest)
                continue; // offline: tried again next time

            // Only an up to date runner waits a day for the next check: a
            // pending update (or "Update Equinox") is asked again on every
            // launch, so the card keeps showing it. Read again first: an
            // install may have replaced the file meanwhile.
            if (!installing.load()) {
                auto fresh = emu::readInstalled(paths.installed);
                if (fresh && emu::decide(fresh, *latest, console->protocol) ==
                                 emu::RunnerAction::UpToDate) {
                    fresh->lastCheck = now;
                    emu::writeInstalled(paths.installed, *fresh);
                }
            }

            const emu::Manifest manifest = *latest;
            brls::sync([console, manifest] {
                latestManifests()[console->slug] = manifest;
                if (!instance)
                    return;
                for (ConsoleCard* card : instance->consoleCards)
                    if (&card->console() == console)
                        card->setLatest(manifest);
            });
        }
        checkingUpdates.store(false);
    });
}

#else

void HomeView::checkConsoleUpdates() {}

#endif // PLATFORM_SWITCH

void HomeView::willAppear(bool resetState) {
    Box::willAppear(resetState);
    presenting = false;
    if (auto* applet = getAppletFrame()) {
        frame = applet;
        frame->setHeaderVisibility(Visibility::GONE);
    }

    // The cards already query their host when they are created.
    if (appeared) {
        for (HostCard* card : hostCards)
            card->refresh();
#if defined(PLATFORM_SWITCH)
        // Back from a game or a screen: ROMs may have been added since.
        for (ConsoleCard* card : consoleCards)
            card->refresh();
#endif
    }
    appeared = true;

    updateHomeCards();
    updateShelfInfo();
    if (entered)
        fillPanel();
}

void HomeView::draw(NVGcontext* vg, float x, float y, float width,
                    float height, Style style, FrameContext* ctx) {
    const float chrome = chromeFade.getValue();
    header->setAlpha(chrome);
    for (Shelf* shelf : {&home, &pcs}) {
        shelf->crumb->getParent()->setAlpha(chrome);
        shelf->info->setAlpha(chrome);
        shelf->detail->setAlpha(chrome);
        shelf->dots->setAlpha(chrome);
    }

    // A dialog closing hands focus back to where it was opened; if the panel
    // was closed meanwhile, bring it back to the shelf.
    View* focus = Application::getCurrentFocus();
    if (!entered && focus && isInside(focus, panel))
        if (View* card = getDefaultFocus())
            Application::giveFocus(card);

    Box::draw(vg, x, y, width, height, style, ctx);
}

void HomeView::presentWithHeader(View* from, View* view) {
    AppletFrame* applet = from->getAppletFrame();
    if (!applet)
        return;
    if (instance && instance->frame == applet)
        instance->presenting = true;
    applet->setHeaderVisibility(Visibility::VISIBLE);
    applet->pushContentView(view);
}

void HomeView::hostsChanged() {
    brls::sync([] {
        if (instance)
            instance->rebuildHosts();
    });
}

void HomeView::hostAdded() {
    if (!instance)
        return;
    auto rebuild = [] {
        if (instance)
            instance->rebuildHosts(
                (int)Settings::instance().hosts().size() - 1);
    };
    if (instance->presenting && instance->frame) {
        instance->frame->popContentView(rebuild);
        return;
    }
    rebuild();
}

View* HomeView::create() { return new HomeView(); }
