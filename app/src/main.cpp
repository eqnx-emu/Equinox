//
//  main.cpp
//  Moonlight
//
//  Created by XITRIX on 26.05.2021.
//

// Switch include only necessary for demo videos recording
#ifdef __SWITCH__
#include <switch.h>
#endif


#include <cstdlib>

#include <borealis.hpp>
#include <algorithm>
#include <string>
#include <vector>

#include "add_host_tab.hpp"
#include "download_service.hpp"
#include "bold_label.hpp"
#include "host_card.hpp"
#include "link_cell.hpp"
#include "main_activity.hpp"
#include "backdrop_frame.hpp"
#include "home_view.hpp"
#include "settings_tab.hpp"
#include "app_theme.hpp"
#include "splash_view.hpp"
#include "Updater.hpp"
#include "metrics_hud.hpp"
#include "views/boolean_slider_cell.hpp"

#if defined(PLATFORM_SWITCH)
#include "emu/EmuLauncher.hpp"
#include "emu/LaunchLine.hpp"
#endif

#include "MoonlightSession.hpp"
#include "SwitchMoonlightSessionDecoderAndRenderProvider.hpp"


#if defined(_WIN32) && defined(__SDL2__)
#include <SDL.h>
#define SDL_MAIN
#endif

#if defined(__SDL3__)
#include <SDL3/SDL_main.h>
#elif defined(__SDL2__)
#include <SDL_main.h>
#endif
#include <main_args.hpp>

using namespace brls;
using namespace brls::literals; // for _i18n

namespace {

bool loadBundledFont(const std::string& name, const std::string& file) {
    return brls::Application::loadFontFromFile(name,
                                               std::string(BRLS_RESOURCES) + file);
}

// Equinox uses Onest for its interface. The platform fonts loaded by Borealis
// stay as fallbacks, so CJK text and button glyphs keep rendering.
void loadEquinoxFonts() {
    NVGcontext* vg = brls::Application::getNVGContext();

    std::vector<int> fallbacks;
    for (const std::string& name :
         {brls::FONT_REGULAR, brls::FONT_CHINESE_SIMPLIFIED,
          brls::FONT_CHINESE_SIMPLIFIED_EXT, brls::FONT_CHINESE_TRADITIONAL,
          brls::FONT_KOREAN_REGULAR, brls::FONT_SWITCH_ICONS,
          brls::FONT_MATERIAL_ICONS, brls::FONT_EMOJI}) {
        int id = brls::Application::getFont(name);
        if (id != brls::FONT_INVALID &&
            std::find(fallbacks.begin(), fallbacks.end(), id) == fallbacks.end())
            fallbacks.push_back(id);
    }

    auto addFallbacks = [&](const std::string& name) {
        int id = brls::Application::getFont(name);
        for (int fallback : fallbacks)
            nvgAddFallbackFontId(vg, id, fallback);
    };

    if (loadBundledFont(EQUINOX_BOLD_FONT, "font/onest_semibold.ttf"))
        addFallbacks(EQUINOX_BOLD_FONT);
    else
        brls::Logger::warning("Could not load the Onest semibold font");

    // Replaces the "regular" handle before any label reads the default font.
    if (loadBundledFont(brls::FONT_REGULAR, "font/onest_regular.ttf"))
        addFallbacks(brls::FONT_REGULAR);
    else
        brls::Logger::warning("Could not load the Onest regular font");
}

} // namespace

#ifdef __SWITCH__
namespace {

s32 selectAllowedSwitchCore(u64 affinityMask, int ordinal) {
    s32 lastAllowedCore = -1;

    for (s32 core = 0; core < 4; core++) {
        if ((affinityMask & (1ULL << core)) == 0) {
            continue;
        }

        lastAllowedCore = core;
        if (ordinal == 0) {
            return core;
        }

        ordinal--;
    }

    return lastAllowedCore;
}

void preferSwitchCore(int ordinal) {
    s32 preferredCore = -1;
    u64 affinityMask = 0;
    if (R_FAILED(svcGetThreadCoreMask(&preferredCore, &affinityMask, CUR_THREAD_HANDLE))) {
        return;
    }

    s32 targetCore = selectAllowedSwitchCore(affinityMask, ordinal);
    if (targetCore >= 0 && targetCore != preferredCore) {
        svcSetThreadCoreMask(CUR_THREAD_HANDLE, targetCore, static_cast<u32>(affinityMask));
    }
}

} // namespace
#endif

int main(int argc, char* argv[]) {
    // Enable recording for Twitter memes
#ifdef __SWITCH__
    appletInitializeGamePlayRecording();
    appletSetWirelessPriorityMode(AppletWirelessPriorityMode_OptimizedForWlan);

    // Keep the UI loop away from the hottest streaming worker core.
    preferSwitchCore(0);

    // Keep the main thread above others so that the program stays responsive
    // when doing software decoding
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x20);

    // auto at = appletGetAppletType();
    // g_application_mode = at == AppletType_Application || at == AppletType_SystemApplication;

    // // To get access to /dev/nvhost-nvjpg, we need nvdrv:{a,s,t}
    // // However, nvdrv:{a,s} have limited address space for gpu mappings
    // extern u32 __nx_nv_service_type, __nx_nv_transfermem_size;
    // __nx_nv_service_type     = NvServiceType_Factory;
    // __nx_nv_transfermem_size = (g_application_mode ? 16 : 3) * 0x100000;
#endif

    // Debug logging costs whole milliseconds per line over nxlink.
    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);

    // Init the app and i18n
    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    registerDeepLinkHandler();
    app_theme::rememberSystem();


    MoonlightSession::set_provider(
            new SwitchMoonlightSessionDecoderAndRenderProvider());

    brls::Application::createWindow("title"_i18n);
    loadEquinoxFonts();

    auto home = Application::getPlatform()->getHomeDirectory("Moonlight-Switch");
    Settings::instance().set_working_dir(home);
    Settings::instance().set_launch_path(argc > 0 ? argv[0] : "");
    Updater::recoverInstall();
    app_theme::apply(Settings::instance().theme());
    brls::Logger::info("Working dir, {}", home);

    // Have the application register an action on every activity that will quit
    // when you press BUTTON_START
    brls::Application::setGlobalQuit(false);
    brls::Application::setFPSStatus(false);

    // Register custom views (including tabs, which are views)
    brls::Application::registerXMLView("BooleanSliderCell", BooleanSliderCell::create);
    brls::Application::registerXMLView("LinkCell", LinkCell::create);
    brls::Application::registerXMLView("MetricsPreview", MetricsPreview::create);

    brls::Application::registerXMLView("HomeView", HomeView::create);
    brls::Application::registerXMLView("BackdropFrame", BackdropFrame::create);
    brls::Application::registerXMLView("MonitorView", MonitorView::create);
    brls::Application::registerXMLView("BoldLabel", BoldLabel::create);
    brls::Application::registerXMLView("AddHostTab", AddHostTab::create);
    brls::Application::registerXMLView("SettingsTab", SettingsTab::create);

    // Add custom values to the theme
    brls::Theme::getLightTheme().addColor("captioned_image/caption",
                                   nvgRGB(2, 176, 183));
    brls::Theme::getDarkTheme().addColor("captioned_image/caption",
                                  nvgRGB(51, 186, 227));

    // Home screen host cards. The sky keeps the same night palette in both
    // themes; the card and text colors follow the theme.
    for (auto* theme : {&brls::Theme::getLightTheme(), &brls::Theme::getDarkTheme()}) {
        theme->addColor("equinox/sky_day", nvgRGB(46, 52, 102));
        theme->addColor("equinox/sky_night", nvgRGB(26, 30, 61));
        theme->addColor("equinox/sun", nvgRGB(244, 184, 96));
        theme->addColor("equinox/moon", nvgRGB(157, 184, 255));
        theme->addColor("equinox/horizon_on", nvgRGB(107, 116, 184));
        theme->addColor("equinox/horizon_off", nvgRGB(90, 97, 152));
        theme->addColor("equinox/pill_text", nvgRGB(22, 26, 51));
    }
    brls::Theme::getLightTheme().addColor("equinox/card", nvgRGB(255, 255, 255));
    brls::Theme::getLightTheme().addColor("equinox/card_inner", nvgRGB(239, 241, 247));
    brls::Theme::getLightTheme().addColor("equinox/muted", nvgRGB(96, 100, 120));
    brls::Theme::getLightTheme().addColor("equinox/status_on", nvgRGB(158, 98, 0));
    brls::Theme::getLightTheme().addColor("equinox/online", nvgRGB(34, 139, 72));
    brls::Theme::getLightTheme().addColor("equinox/status_moon", nvgRGB(52, 88, 186));
    brls::Theme::getDarkTheme().addColor("equinox/card", nvgRGB(45, 52, 98));
    brls::Theme::getDarkTheme().addColor("equinox/card_inner", nvgRGB(34, 39, 78));
    brls::Theme::getDarkTheme().addColor("equinox/muted", nvgRGB(174, 179, 214));
    brls::Theme::getDarkTheme().addColor("equinox/status_on", nvgRGB(244, 184, 96));
    brls::Theme::getLightTheme().addColor("equinox/danger", nvgRGB(184, 52, 40));
    brls::Theme::getDarkTheme().addColor("equinox/danger", nvgRGB(255, 167, 155));
    brls::Theme::getDarkTheme().addColor("equinox/online", nvgRGB(124, 214, 138));
    brls::Theme::getDarkTheme().addColor("equinox/status_moon", nvgRGB(157, 184, 255));

    // Equinox palette for the dark theme: dusk blue ground, sun amber accent
    // (focus, active tab, primary buttons) and moon blue for secondary info.
    // addColor overrides Borealis' defaults.
    {
        auto& dark = brls::Theme::getDarkTheme();
        const NVGcolor amber = nvgRGB(244, 184, 96);
        dark.addColor("brls/background", nvgRGB(22, 26, 51));
        dark.addColor("brls/text", nvgRGB(238, 240, 250));
        dark.addColor("brls/text_disabled", nvgRGB(110, 116, 160));
        dark.addColor("brls/accent", amber);
        dark.addColor("brls/click_pulse", nvgRGBA(244, 184, 96, 38));
        dark.addColor("brls/highlight/background", nvgRGB(31, 35, 71));
        dark.addColor("brls/highlight/color1", amber);
        dark.addColor("brls/highlight/color2", nvgRGB(255, 215, 154));
        dark.addColor("brls/applet_frame/separator", nvgRGB(47, 53, 99));
        dark.addColor("brls/sidebar/background", nvgRGB(28, 32, 66));
        dark.addColor("brls/sidebar/active_item", amber);
        dark.addColor("brls/sidebar/separator", nvgRGB(47, 53, 99));
        dark.addColor("brls/header/border", nvgRGB(60, 66, 110));
        dark.addColor("brls/header/rectangle", nvgRGB(157, 184, 255));
        dark.addColor("brls/header/subtitle", nvgRGB(174, 179, 214));
        dark.addColor("brls/button/primary_enabled_background", amber);
        dark.addColor("brls/button/primary_enabled_text", nvgRGB(22, 26, 51));
        dark.addColor("brls/button/default_enabled_background", nvgRGB(45, 51, 96));
        dark.addColor("brls/button/default_disabled_background", nvgRGB(45, 51, 96));
        dark.addColor("brls/button/highlight_enabled_text", amber);
        dark.addColor("brls/button/highlight_disabled_text", amber);
        dark.addColor("brls/list/listItem_value_color", nvgRGB(157, 184, 255));
        dark.addColor("brls/slider/pointer_color", nvgRGB(45, 51, 96));
        dark.addColor("brls/slider/pointer_border_color", nvgRGB(90, 97, 152));
        dark.addColor("brls/slider/line_filled", amber);
        dark.addColor("brls/slider/line_empty", nvgRGB(60, 66, 110));
    }

    // The light theme is the same identity by day: a warm paper white
    // ground, night navy for the text, and the sun amber deepened so it
    // still reads on white (text and lines), bright only in the focus glow.
    {
        auto& light = brls::Theme::getLightTheme();
        const NVGcolor ink = nvgRGB(26, 30, 61);
        const NVGcolor amber = nvgRGB(168, 100, 0);       // text and lines on white
        const NVGcolor glow = nvgRGB(214, 136, 20);       // focus ring
        const NVGcolor line = nvgRGB(221, 218, 208);
        light.addColor("brls/background", nvgRGB(246, 244, 238));
        light.addColor("brls/text", ink);
        light.addColor("brls/text_disabled", nvgRGB(100, 104, 138));
        light.addColor("brls/accent", amber);
        light.addColor("brls/click_pulse", nvgRGBA(214, 136, 20, 38));
        light.addColor("brls/highlight/background", nvgRGB(255, 255, 255));
        light.addColor("brls/highlight/color1", glow);
        light.addColor("brls/highlight/color2", nvgRGB(244, 184, 96));
        light.addColor("brls/applet_frame/separator", line);
        light.addColor("brls/sidebar/background", nvgRGB(239, 236, 228));
        light.addColor("brls/sidebar/active_item", amber);
        light.addColor("brls/sidebar/separator", line);
        light.addColor("brls/header/border", line);
        light.addColor("brls/header/rectangle", nvgRGB(82, 108, 196));
        light.addColor("brls/header/subtitle", nvgRGB(96, 100, 120));
        light.addColor("brls/button/primary_enabled_background", nvgRGB(244, 184, 96));
        light.addColor("brls/button/primary_enabled_text", ink);
        light.addColor("brls/button/default_enabled_background", nvgRGB(232, 229, 220));
        light.addColor("brls/button/default_disabled_background", nvgRGB(232, 229, 220));
        light.addColor("brls/button/highlight_enabled_text", amber);
        light.addColor("brls/button/highlight_disabled_text", amber);
        light.addColor("brls/list/listItem_value_color", nvgRGB(52, 88, 186));
        light.addColor("brls/slider/pointer_color", nvgRGB(255, 255, 255));
        light.addColor("brls/slider/pointer_border_color", nvgRGB(180, 176, 165));
        light.addColor("brls/slider/line_filled", glow);
        light.addColor("brls/slider/line_empty", line);
    }

    // Add custom values to the style
    // Narrower sidebar, so the content gets the room.
    brls::getStyle().addMetric("brls/tab_frame/sidebar_width", 290);
    brls::getStyle().addMetric("brls/sidebar/padding_left", 44);
    brls::getStyle().addMetric("brls/sidebar/padding_right", 28);

    brls::getStyle().addMetric("about/padding_top_bottom", 50);
    brls::getStyle().addMetric("about/padding_sides", 75);
    brls::getStyle().addMetric("about/description_margin", 50);

    // Back from a console game: the runner passes a resume token. The home
    // screen opens straight on the console's shelf, without the splash.
    bool resuming = false;
#if defined(PLATFORM_SWITCH)
    if (auto arg = emu::resumeArg(argc, argv)) {
        if (auto token = emu::decodeToken(*arg)) {
            emu::setPendingResume(*token);
            resuming = true;
        } else {
            brls::Logger::warning("Ignoring an invalid resume token");
        }
    }
#endif

    // Create and push the main activity to the stack if cannot run game from arguments
    if (resuming) {
        if (canStartApp(argc, argv))
            brls::Application::pushActivity(new MainActivity());
    } else if (!startFromArgs(argc, argv)) {
        brls::Application::pushActivity(new MainActivity());
        SplashView::show();
    }

    brls::Application::enableDebuggingView(Settings::instance().write_log());

    DownloadService::instance().start();

    // Run the app.
    while (brls::Application::mainLoop()) {
    }

    // Exit
    DownloadService::instance().shutdown();

    return EXIT_SUCCESS;
}
