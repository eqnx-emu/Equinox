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

    // A controller where a PC card would show its monitor.
    auto* icon = new IconLabel();
    icon->setText(""); // videogame_asset
    icon->setFontSize(30);
    icon->setTextColor(themeColor("equinox/sun"));
    icon->setPositionType(PositionType::ABSOLUTE);
    icon->setPositionTop(18);
    icon->setPositionRight(22);
    addView(icon);

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

    const NVGcolor color = statusColor();
    statusDot->setBackgroundColor(color);
    status->setTextColor(color);
    status->setText(statusLine);
    detail->setText(detailLine);
    info->setText(infoLine);

    if (onChanged)
        onChanged();
}

void ConsoleCard::draw(NVGcontext* vg, float x, float y, float width,
                       float height, Style style, FrameContext* ctx) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, width, height, 16);
    nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x, y + height,
                                       a(hex(consoleRef.cardColorTop)),
                                       a(hex(consoleRef.cardColorBottom))));
    nvgFill(vg);

    PressableBox::draw(vg, x, y, width, height, style, ctx);
}

#endif // PLATFORM_SWITCH
