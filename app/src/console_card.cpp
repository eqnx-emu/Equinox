//
//  console_card.cpp
//  Equinox
//

#include "console_card.hpp"

#if defined(PLATFORM_SWITCH)

#include "bold_label.hpp"
#include "emu/RomScan.hpp"

#include <cmath>
#include <fmt/core.h>
#include <map>

using namespace brls;
using namespace brls::literals;

namespace {
NVGcolor themeColor(const std::string& name) {
    return Application::getTheme().getColor(name);
}

NVGcolor hex(unsigned rgb) {
    return nvgRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// Last game count per console, so a rebuilt shelf does not blink while the
// SD card is scanned again.
std::map<std::string, int>& lastCounts() {
    static std::map<std::string, int> counts;
    return counts;
}

std::string gamesText(int count) {
    return count == 1 ? "hosts/games_one"_i18n
                      : brls::getStr("hosts/games_count", count);
}

// The runner each console installs, as people know it.
std::string runnerName(const std::string& id) {
    if (id == "snes9x")
        return "Snes9x";
    return id;
}

std::string runnerIdFor(const emu::Console& console) {
    return console.slug == "snes" ? "snes9x" : console.slug;
}
} // namespace

std::string emuSizeText(uint64_t bytes) {
    std::string text = fmt::format("{:.1f}", bytes / 1000000.0);
    const std::string separator = "emu/decimal_separator"_i18n;
    const size_t dot = text.find('.');
    if (dot != std::string::npos)
        text.replace(dot, 1, separator);
    return text + " MB";
}

std::string emuRunnerName(const emu::Manifest& manifest) {
    return runnerName(manifest.id);
}

ConsoleCard::ConsoleCard(const emu::Console& console) : consoleRef(console) {
    this->inflateFromXMLRes("xml/cells/host_card.xml");

    // Gradient drawn in draw().
    this->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
    name->setText(console.name);

    // The tag row on top: the console's controller and its short name.
    auto* header = new Box(Axis::ROW);
    header->setAlignItems(AlignItems::CENTER);
    header->setMarginBottom(4);

    icon = new Image();
    icon->setScalingType(ImageScalingType::FIT);
    icon->setImageFromRes("img/consoles/" + console.slug + ".png");
    icon->setWidth(30);
    icon->setHeight(30);
    icon->setMarginRight(8);
    header->addView(icon);

    tag = new Box(Axis::ROW);
    tag->setPadding(3, 9, 3, 9);
    tag->setCornerRadius(10);
    tag->setBackgroundColor(hex(console.cardAccent));
    auto* tagText = new BoldLabel();
    tagText->setFontSize(13);
    tagText->setTextColor(themeColor("equinox/pill_text"));
    tagText->setText(console.shortName);
    tag->addView(tagText);
    header->addView(tag);
    addView(header, 0);

    // Pushes the name and the count to the bottom of the card.
    auto* spacer = new Box();
    spacer->setGrow(1);
    addView(spacer, 1);

    // The Devices panel below the shelf shows the runner and the folder.
    statusDot->setVisibility(Visibility::GONE);
    detail->setVisibility(Visibility::GONE);
    info->setVisibility(Visibility::GONE);
    status->setFontSize(15);

    // The card draws its own gold border.
    setHideHighlight(true);

    auto it = lastCounts().find(console.slug);
    if (it != lastCounts().end())
        games = it->second;
    installed = emu::readInstalled(emu::pathsFor(console).installed);

    this->addGestureRecognizer(new TapGestureRecognizer(this));
    this->registerClickAction([this](View* view) {
        press([this] {
            if (onEnter)
                onEnter();
        });
        return true;
    });
    updateActionHint(ControllerButton::BUTTON_A, "hosts/options"_i18n);

    update();
    refresh();
}

void ConsoleCard::refresh() {
    const emu::Console* console = &consoleRef;
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, console] {
        const emu::ConsolePaths paths = emu::pathsFor(*console);
        auto found = emu::readInstalled(paths.installed);
        const int count = (int)emu::scanRoms(paths.roms, *console).size();
        brls::sync([ASYNC_TOKEN, found, count] {
            ASYNC_RELEASE
            installed = found;
            games = count;
            lastCounts()[consoleRef.slug] = count;
            update();
        });
    });
}

void ConsoleCard::setLatest(const emu::Manifest& latest) {
    latestManifest = latest;
    update();
}

NVGcolor ConsoleCard::statusColor() const {
    switch (runnerAction) {
    case emu::RunnerAction::Update:
        return themeColor("equinox/sun");
    case emu::RunnerAction::NeedsNewerEquinox:
        return themeColor("equinox/muted");
    default:
        return themeColor("equinox/online");
    }
}

void ConsoleCard::update() {
    runnerAction = latestManifest
                       ? emu::decide(installed, *latestManifest, consoleRef.protocol)
                       : emu::RunnerAction::UpToDate;

    switch (runnerAction) {
    case emu::RunnerAction::Update:
        statusLine = "emu/update_available"_i18n;
        break;
    case emu::RunnerAction::NeedsNewerEquinox:
        statusLine = "emu/needs_newer_short"_i18n;
        break;
    default:
        statusLine = brls::getStr("emu/on_switch",
                                  games < 0 ? std::string("…") : gamesText(games));
        break;
    }

    detailLine = runnerName(runnerIdFor(consoleRef));
    if (installed)
        detailLine += "  ·  v" + installed->version;

    std::string folder = emu::pathsFor(consoleRef).roms;
    if (folder.rfind("sdmc:", 0) == 0)
        folder = folder.substr(5);
    infoLine = brls::getStr("emu/roms_folder", folder);

    // The card says only what changes: the count, or the update.
    const NVGcolor color = statusColor();
    statusDot->setBackgroundColor(color);
    if (runnerAction == emu::RunnerAction::UpToDate) {
        status->setTextColor(themeColor("equinox/muted"));
        status->setText(games < 0 ? std::string("…") : gamesText(games));
    } else {
        status->setTextColor(color);
        status->setText(statusLine);
    }
    detail->setText(detailLine);
    info->setText(infoLine);

    if (onChanged)
        onChanged();
}

void ConsoleCard::onFocusGained() {
    PressableBox::onFocusGained();
    animateFocus(true);
}

void ConsoleCard::onFocusLost() {
    PressableBox::onFocusLost();
    animateFocus(false);
}

void ConsoleCard::animateFocus(bool focused) {
    const float to = focused ? 1.0f : 0.0f;
    focus.stop();
    focus.reset();
    focus.addStep(to, 220, EasingFunction::quadraticOut);
    focus.start();

    lift.stop();
    lift.reset();
    lift.addStep(focused ? 1.04f : 1.0f, 220, EasingFunction::quadraticOut);
    lift.start();

    hop.stop();
    hop.reset();
    if (focused)
        hop.addStep(1.0f, 650, EasingFunction::backOut);
    else
        hop.addStep(0.0f, 260, EasingFunction::quadraticOut);
    hop.start();
}

void ConsoleCard::draw(NVGcontext* vg, float x, float y, float width,
                       float height, Style style, FrameContext* ctx) {
    const float f = focus;
    const float radius = 16;
    const NVGcolor gold = themeColor("equinox/sun");

    // Soft gold glow behind the card.
    if (f > 0.0f) {
        NVGcolor inner = gold;
        inner.a = 0.35f * f * getAlpha();
        NVGcolor outer = gold;
        outer.a = 0;
        nvgBeginPath(vg);
        nvgRect(vg, x - 30, y - 30, width + 60, height + 60);
        nvgFillPaint(vg, nvgBoxGradient(vg, x, y, width, height, radius, 30,
                                        inner, outer));
        nvgFill(vg);
    }

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, width, height, radius);
    nvgFillPaint(vg, nvgLinearGradient(vg, x, y + height, x + width, y,
                                       a(hex(consoleRef.cardColorBottom)),
                                       a(hex(consoleRef.cardColorTop))));
    nvgFill(vg);

    // The controller, large and tilted in the corner, cut by the card's
    // rounded edge: the card's shape filled with the icon as a pattern
    // (outside the picture its transparent edge repeats).
    const int texture = icon ? icon->getTexture() : 0;
    if (texture > 0) {
        const float h = hop;
        const float size = 215 * (1 + 0.1f * h);
        const float angle = nvgDegToRad(-14 + 8 * h);
        const float cx = x + width - 87.5f;
        const float cy = y + height - 72.5f;
        const float c = std::cos(angle), s = std::sin(angle);
        const float half = size / 2;
        const float ox = cx + (-half * c + half * s);
        const float oy = cy + (-half * s - half * c);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, radius);
        nvgFillPaint(vg, nvgImagePattern(vg, ox, oy, size, size, angle, texture,
                                         (0.8f + 0.2f * f) * getAlpha()));
        nvgFill(vg);
    }

    tag->setBackgroundColor(
        nvgLerpRGBA(hex(consoleRef.cardAccent), gold, f));

    PressableBox::draw(vg, x, y, width, height, style, ctx);

    if (f > 0.0f) {
        NVGcolor border = gold;
        border.a = f * getAlpha();
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x - 1.5f, y - 1.5f, width + 3, height + 3, radius + 1.5f);
        nvgStrokeColor(vg, border);
        nvgStrokeWidth(vg, 3);
        nvgStroke(vg);
    }
}

#endif // PLATFORM_SWITCH
