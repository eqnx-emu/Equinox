//
//  library_widgets.cpp
//  Equinox
//

#include "library_widgets.hpp"

#if defined(PLATFORM_SWITCH)

#include <algorithm>

using namespace brls;
using namespace brls::literals;

LibraryProgressBar::LibraryProgressBar() {
    setHeight(6);
    setShrink(0);
}

void LibraryProgressBar::setFraction(float value) {
    fraction = std::clamp(value, 0.0f, 1.0f);
}

void LibraryProgressBar::draw(NVGcontext* vg, float x, float y, float width, float height, Style,
                              FrameContext*) {
    Theme theme = Application::getTheme();
    const float r = height / 2;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, width, height, r);
    nvgFillColor(vg, a(theme.getColor("equinox/card")));
    nvgFill(vg);
    if (fraction <= 0)
        return;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, std::max(height, width * fraction), height, r);
    nvgFillColor(vg, a(theme.getColor("brls/accent")));
    nvgFill(vg);
}

std::string libraryStopText(emu::DataStop stop) {
    switch (stop) {
    case emu::DataStop::Offline:
        return "library/stop/offline"_i18n;
    case emu::DataStop::BadLogin:
        return "library/stop/bad_login"_i18n;
    case emu::DataStop::Quota:
        return "library/stop/quota"_i18n;
    case emu::DataStop::Disk:
        return "library/stop/disk"_i18n;
    case emu::DataStop::None:
    case emu::DataStop::Cancelled:
        break;
    }
    return "";
}

#endif // PLATFORM_SWITCH
