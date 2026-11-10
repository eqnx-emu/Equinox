#pragma once

namespace emu {

// Where Equinox keeps ROMs, covers, emulators and its queue. The desktop
// build, used only for testing, keeps them next to where it runs.
#if defined(PLATFORM_SWITCH)
constexpr const char* kDataRoot = "sdmc:/equinox";
#else
constexpr const char* kDataRoot = "equinox-data";
#endif

}  // namespace emu
