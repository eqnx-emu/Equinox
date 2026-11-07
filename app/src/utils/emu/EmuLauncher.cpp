#include "emu/EmuLauncher.hpp"

#include <utility>

#if defined(__SWITCH__)
#include <switch.h>

#include <borealis.hpp>
#include <ctime>
#include <filesystem>
#include <system_error>

#include "Settings.hpp"
#include "emu/CoverName.hpp"
#include "emu/PathUtf8.hpp"
#include "emu/VideoDefaults.hpp"
#include "helper.hpp"

using namespace brls::literals;
#endif

namespace emu {

namespace {
std::optional<ResumeToken>& pendingResume() {
    static std::optional<ResumeToken> token;
    return token;
}
}  // namespace

void setPendingResume(const ResumeToken& token) { pendingResume() = token; }

std::optional<ResumeToken> takePendingResume() {
    std::optional<ResumeToken> token = std::move(pendingResume());
    pendingResume().reset();
    return token;
}

#if defined(__SWITCH__)

bool launchRom(const Console& console, const RomEntry& rom, Origin origin, int filter) {
    const ConsolePaths paths = pathsFor(console);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(fromUtf8(paths.runner), ec)) {
        showError("emu/not_installed"_i18n);
        return false;
    }
    if (!std::filesystem::is_regular_file(fromUtf8(rom.path), ec)) {
        showError("emu/rom_missing"_i18n);
        return false;
    }
    // Without the homebrew loader (e.g. started from a forwarder in
    // application mode) there is nothing to chain the runner to.
    if (!envHasNextLoad()) {
        showError("emu/cannot_launch"_i18n);
        return false;
    }

    // The runner opens this file when the player leaves the game; without it
    // the Switch would fall back to the Homebrew Menu.
    const auto isFile = [](const std::string& path) {
        std::error_code err;
        return std::filesystem::is_regular_file(fromUtf8(path), err);
    };
    const std::optional<std::string> returnTo =
        pickReturnPath(Settings::instance().launch_path(), isFile);
    if (!returnTo) {
        showError("emu/no_return"_i18n);
        return false;
    }
    if (!ensureFolders(paths)) {
        showError("emu/error_folders"_i18n);
        return false;
    }

    Settings::instance().set_last_played(console.slug, rom.fileName, (int64_t)std::time(nullptr));
    Settings::instance().save();

    ResumeToken token;
    token.console = console.slug;
    token.origin = origin;
    token.filter = filter;
    token.romFile = rom.fileName;

    const std::string cover = coverPath(paths, rom.title);
    const std::string video = videoDefaultsPath(paths);
    recoverVideoDefaults(video);
    // argv[0] must be the runner's own path.
    const std::optional<std::string> line =
        launchLine(paths.runner, console, paths, rom, *returnTo, encodeToken(token),
                   isFile(cover) ? cover : "", isFile(video) ? video : "");
    if (!line) {
        brls::Logger::error("EmuLauncher: launch line too long for {}", rom.fileName);
        showError("emu/cannot_launch"_i18n);
        return false;
    }
    brls::Logger::info("EmuLauncher: {}", *line);
    if (R_FAILED(envSetNextLoad(paths.runner.c_str(), line->c_str()))) {
        showError("emu/cannot_launch"_i18n);
        return false;
    }

    romfsExit();
    brls::Application::quit();
    return true;
}

#else

bool launchRom(const Console&, const RomEntry&, Origin, int) { return false; }

#endif

}  // namespace emu
