#pragma once

#include <borealis.hpp>
#include <string>

#include "../Settings.hpp"
#include "ScreenScraper.hpp"

#include "ss_dev.h"

namespace emu {

inline bool ssDevConfigured() { return EQUINOX_SS_DEVID[0] != '\0'; }

inline SsConfig appSsConfig() {
    SsConfig cfg;
    cfg.devId = EQUINOX_SS_DEVID;
    cfg.devPassword = EQUINOX_SS_DEVPASSWORD;
    cfg.softName = std::string("Equinox ") + APP_VERSION;
    cfg.user = Settings::instance().screenscraper_user();
    cfg.password = Settings::instance().screenscraper_password();
    cfg.lang = brls::Application::getLocale() == brls::LOCALE_PT_BR ? "pt" : "en";
    return cfg;
}

}  // namespace emu
