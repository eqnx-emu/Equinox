//
//  app_theme.hpp
//  Equinox
//
//  The light or dark interface, picked in Settings: follow the system
//  (the default), always light or always dark.
//

#pragma once

#include <string>

namespace app_theme {

// Call once after Application::init(), before applying the setting: keeps
// what the system asked for, for the "follow the system" choice.
void rememberSystem();

// Switches the interface to the setting (Settings::theme()). Views made
// before keep their colours: call reload() to rebuild them.
void apply(int setting);

// Rebuilds the main screen in the current theme, then opens the settings
// group again (its id, e.g. "system") so the user stays where they were.
void reload(const std::string& settingsGroup);

} // namespace app_theme
