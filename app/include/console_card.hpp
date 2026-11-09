//
//  console_card.hpp
//  Equinox
//
//  A console whose emulator is installed, on the Devices shelf: a card in
//  the console's colors with its controller drawn large in the corner. Shows
//  how many games are on the SD card, or that an update is waiting. With
//  focus its tag turns gold and the controller
//  hops upright.
//
//  A console with a 3D turntable (img/3d/<slug>.png) has no card:
//  its object floats over the name and spins while it is selected.
//

#pragma once

#include "cover_carousel.hpp"
#include "emu/Console.hpp"
#include "emu/RunnerManifest.hpp"
#include "pressable_box.hpp"
#include "turntable.hpp"
#include <borealis.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>

class ConsoleCard : public PressableBox, public ShelfItem {
  public:
    explicit ConsoleCard(const emu::Console& console);

    bool frameless() const override { return turntable != nullptr; }

    // Re-reads the installed runner and counts the ROMs again, off the UI
    // thread.
    void refresh();
    // Walks the ROM folder, updates the count and tells the result in a
    // notification, off the UI thread.
    void rescan();
    // Result of the update check: shows "Update available" when it is newer.
    void setLatest(const emu::Manifest& latest);

    const emu::Console& console() const { return consoleRef; }
    std::string statusText() const { return statusLine; }
    std::string detailText() const { return detailLine; }
    std::string infoText() const { return infoLine; }
    NVGcolor statusColor() const;
    // -1 until the first scan finishes.
    int gameCount() const { return games; }
    emu::RunnerAction action() const { return runnerAction; }
    const std::optional<emu::Manifest>& latest() const { return latestManifest; }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;
    void onFocusGained() override;
    void onFocusLost() override;

    BRLS_BIND(brls::Label, name, "name");
    BRLS_BIND(brls::Box, statusDot, "status_dot");
    BRLS_BIND(brls::Label, status, "status");
    BRLS_BIND(brls::Label, detail, "detail");
    BRLS_BIND(brls::Label, info, "info");

    // A on the card (after the press animation).
    std::function<void()> onEnter;
    // Game count, version or update state changed.
    std::function<void()> onChanged;

  private:
    const emu::Console& consoleRef;
    std::optional<emu::Installed> installed;
    std::optional<emu::Manifest> latestManifest;
    emu::RunnerAction runnerAction = emu::RunnerAction::UpToDate;
    int games = -1;
    bool scanning = false;
    // 1 to 3, the dots after the text while the folder is searched.
    int scanDots = 1;
    brls::RepeatingTimer scanTimer;
    std::string statusLine, detailLine, infoLine;

    brls::Image* icon = nullptr;
    brls::Box* tag = nullptr;
    // 0 to 1 with focus: gold tag and full-strength controller.
    brls::Animatable focus = 0.0f;
    // 0 to 1 with focus, overshooting: the controller's hop.
    brls::Animatable hop = 0.0f;

    // The 3D object instead of the card, when the console has one.
    std::unique_ptr<Turntable> turntable;

    void update();
    void animateFocus(bool focused);
};

// "11,4 MB" / "11.4 MB", with the decimal separator of the language.
std::string emuSizeText(uint64_t bytes);
// Display name of the emulator a manifest ships ("snes9x" -> "Snes9x").
std::string emuRunnerName(const emu::Manifest& manifest);
// Notification text for a finished search of the ROM folder.
std::string emuScanNotice(size_t found);
