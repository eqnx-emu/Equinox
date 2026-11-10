//
//  console_card.cpp
//  Equinox
//

#include "console_card.hpp"


#include "bold_label.hpp"
#include "emu/CoverStore.hpp"
#include "emu/RomScan.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fmt/core.h>
#include <map>
#include <thread>

using namespace brls;
using namespace brls::literals;

namespace {
// The opened console's scene fits this box, centred kSceneCentre below the
// card's top: the column left of the panel, down to where the name was.
constexpr float kSceneBoxW = 440;
constexpr float kSceneBoxH = 330;
constexpr float kSceneCentre = 30;
// The resting object grows toward this width as it fades into the scene.
constexpr float kRestingGrowW = 330;

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

// Last installed runner read per console, for the same reason: only the
// first card of a console reads it on the UI thread (it can't start empty,
// it would show "not installed" for a moment).
std::map<std::string, std::optional<emu::Installed>>& lastInstalled() {
    static std::map<std::string, std::optional<emu::Installed>> found;
    return found;
}

std::string gamesText(int count) {
    return count == 1 ? "hosts/games_one"_i18n
                      : brls::getStr("hosts/games_count", count);
}

// The runner each console installs, as people know it.
std::string runnerName(const std::string& id) {
    if (id == "snes9x")
        return "Snes9x";
    if (id == "mgba")
        return "mGBA";
    if (id == "gambatte")
        return "Gambatte";
    return id;
}

std::string runnerIdFor(const emu::Console& console) {
    if (console.slug == "snes")
        return "snes9x";
    if (console.slug == "gba")
        return "mgba";
    if (console.slug == "gbc")
        return "gambatte";
    return console.slug;
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

std::string emuScanNotice(size_t found) {
    if (found == 0)
        return "emu/scan_none"_i18n;
    return brls::getStr("emu/scan_done", found == 1
                                             ? "hosts/games_one"_i18n
                                             : brls::getStr("hosts/games_count", found));
}

ConsoleCard::ConsoleCard(const emu::Console& console) : consoleRef(console) {
    this->inflateFromXMLRes("xml/cells/host_card.xml");

    // Gradient drawn in draw().
    this->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
    name->setText(console.name);
    // The live model when it's there, else the pre-rendered views.
    const auto art3d = [](const std::string& base) {
        return Turntable::exists(base + ".mesh") ? base + ".mesh" : base + ".png";
    };
    const std::string base = "img/3d/" + console.slug;
    if (Turntable::exists(art3d(base))) {
        turntable = std::make_unique<Turntable>(art3d(base));
        if (Turntable::exists(base + "-scene.png"))
            scenePath = std::string(BRLS_RESOURCES) + base + "-scene.png";
    }

    if (frameless()) {
        // No card: the object floats over the name, both centred.
        name->setHorizontalAlign(HorizontalAlign::CENTER);
        status->setHorizontalAlign(HorizontalAlign::CENTER);
    } else {
        // The tag row on top: the console's controller and its short name.
        auto* header = new Box(Axis::ROW);
        header->setAlignItems(AlignItems::CENTER);
        header->setMarginBottom(4);

        icon = new Image();
        icon->setScalingType(ImageScalingType::FIT);
        icon->setImageFromRes("img/consoles/" + console.slug + ".png");
        icon->setWidth(46);
        icon->setHeight(20);
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
    }

    // Pushes the name and the count to the bottom of the card.
    auto* spacer = new Box();
    spacer->setGrow(1);
    addView(spacer, 1);

    // The Devices panel below the shelf shows the runner and the folder.
    statusDot->setVisibility(Visibility::GONE);
    detail->setVisibility(Visibility::GONE);
    info->setVisibility(Visibility::GONE);
    status->setFontSize(15);

    auto it = lastCounts().find(console.slug);
    if (it != lastCounts().end())
        games = it->second;
    auto known = lastInstalled().find(console.slug);
    if (known != lastInstalled().end())
        installed = known->second;
    else
        installed = lastInstalled()[console.slug] =
            emu::readInstalled(emu::pathsFor(console).installed);

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
        // The last count the shelf saved: walking the folder here would make
        // every start pay for a library nobody opened yet.
        const int count = emu::readRomCount(paths);
        brls::sync([ASYNC_TOKEN, found, count] {
            ASYNC_RELEASE
            installed = found;
            lastInstalled()[consoleRef.slug] = found;
            if (count >= 0) {
                games = count;
                lastCounts()[consoleRef.slug] = count;
            }
            update();
        });
    });
}

void ConsoleCard::rescan() {
    if (scanning)
        return;
    scanning = true;
    scanDots = 1;
    scanTimer.setCallback([this] {
        scanDots = scanDots % 3 + 1;
        update();
    });
    scanTimer.start(400);
    update();

    const emu::Console* console = &consoleRef;
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, console] {
        // A small folder is read at once: the search still shows one full
        // round of the dots.
        const auto shownUntil =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
        const emu::ConsolePaths paths = emu::pathsFor(*console);
        emu::ensureFolders(paths);
        // Covers libretro lacked are tried again too.
        emu::clearCoverMisses(paths);
        const int count = static_cast<int>(emu::scanRoms(paths.roms, *console).size());
        emu::writeRomCount(paths, count);
        std::this_thread::sleep_until(shownUntil);
        brls::sync([ASYNC_TOKEN, count] {
            ASYNC_RELEASE
            scanTimer.stop();
            scanning = false;
            games = count;
            lastCounts()[consoleRef.slug] = count;
            update();
            Application::notify(emuScanNotice(count));
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
        return themeColor("equinox/status_on");
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

    if (scanning)
        statusLine = "emu/searching_new"_i18n + std::string(scanDots, '.');

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
    if (scanning) {
        status->setTextColor(themeColor("equinox/muted"));
        status->setText(statusLine);
    } else if (runnerAction == emu::RunnerAction::UpToDate) {
        status->setTextColor(themeColor("equinox/muted"));
        status->setText(games < 0 ? std::string() : gamesText(games));
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
    if (turntable)
        turntable->setActive(focused);
    const float to = focused ? 1.0f : 0.0f;
    focus.stop();
    focus.reset();
    focus.addStep(to, 220, EasingFunction::quadraticOut);
    focus.start();

    hop.stop();
    hop.reset();
    if (focused)
        hop.addStep(1.0f, 650, EasingFunction::backOut);
    else
        hop.addStep(0.0f, 260, EasingFunction::quadraticOut);
    hop.start();
}

ConsoleCard::~ConsoleCard() {
    if (sceneImage > 0)
        nvgDeleteImage(Application::getNVGContext(), sceneImage);
}

void ConsoleCard::draw(NVGcontext* vg, float x, float y, float width,
                       float height, Style style, FrameContext* ctx) {
    if (frameless()) {
        // Floats over the name, its shadow just above it. Opened, it grows
        // into the scene over the free space and the name fades out.
        float e = scenePath.empty() ? 0.0f : std::clamp(enteredAmount, 0.0f, 1.0f);
        if (e > 0.01f && sceneImage == 0) {
            sceneImage = nvgCreateImage(vg, scenePath.c_str(), 0);
            if (sceneImage <= 0)
                sceneImage = -1;
        }
        if (sceneImage < 0)
            e = 0;
        const float alpha = getAlpha();
        const float cx = x + width / 2;
        if (e < 0.99f)
            turntable->draw(vg, cx, y + 38, 215 + (kRestingGrowW - 215) * e,
                            y + 100, alpha * (1 - e));
        if (e > 0.01f) {
            // The still, fitted to the box, growing in as it fades in.
            int iw = 0, ih = 0;
            nvgImageSize(vg, sceneImage, &iw, &ih);
            if (iw > 0 && ih > 0) {
                const float fit = std::min(kSceneBoxW / iw, kSceneBoxH / ih) * (0.7f + 0.3f * e);
                const float w = iw * fit, h = ih * fit;
                const float left = cx - w / 2, top = y + kSceneCentre - h / 2;
                nvgBeginPath(vg);
                nvgRect(vg, left, top, w, h);
                nvgFillPaint(vg, nvgImagePattern(vg, left, top, w, h, 0, sceneImage, alpha * e));
                nvgFill(vg);
            }
        }
        for (View* child : getChildren())
            child->setAlpha(1 - e);
        PressableBox::draw(vg, x, y, width, height, style, ctx);
        return;
    }

    const float f = focus;
    const float radius = 16;
    const NVGcolor gold = themeColor("equinox/sun");

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
        int imageW = 0, imageH = 0;
        nvgImageSize(vg, texture, &imageW, &imageH);
        const float h = hop;
        const float w = 205 * (1 + 0.1f * h);
        const float ht = imageW > 0 ? w * imageH / imageW : w;
        const float angle = nvgDegToRad(-14 + 8 * h);
        const float cx = x + width - 62;
        const float cy = y + height - 56;
        const float c = std::cos(angle), s = std::sin(angle);
        const float hw = w / 2, hh = ht / 2;
        const float ox = cx + (-hw * c + hh * s);
        const float oy = cy + (-hw * s - hh * c);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, radius);
        nvgFillPaint(vg, nvgImagePattern(vg, ox, oy, w, ht, angle, texture,
                                         (0.7f + 0.3f * f) * getAlpha()));
        nvgFill(vg);

        // Shade the left side so the name stays readable over it.
        NVGcolor shade = hex(consoleRef.cardColorBottom);
        shade.a = 0.75f * getAlpha();
        NVGcolor clear = shade;
        clear.a = 0;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, radius);
        nvgFillPaint(vg, nvgLinearGradient(vg, x + width * 0.35f, y,
                                           x + width * 0.75f, y, shade, clear));
        nvgFill(vg);
    }

    if (tag)
        tag->setBackgroundColor(
            nvgLerpRGBA(hex(consoleRef.cardAccent), gold, f));

    PressableBox::draw(vg, x, y, width, height, style, ctx);
}


