//
//  bold_label.hpp
//  Equinox
//
//  A label set in Onest SemiBold, for titles and PC names.
//

#pragma once

#include <borealis.hpp>
#include <string>

inline const std::string EQUINOX_BOLD_FONT = "equinox/bold";

class BoldLabel : public brls::Label {
  public:
    BoldLabel() {
        int bold = brls::Application::getFont(EQUINOX_BOLD_FONT);
        if (bold != brls::FONT_INVALID)
            this->font = bold;
    }

    static brls::View* create() { return new BoldLabel(); }
};

// A label drawn with the Material Icons font. Its code points overlap the
// Switch button glyphs, so icons cannot rely on the font fallback chain.
class IconLabel : public brls::Label {
  public:
    IconLabel() {
        int icons = brls::Application::getFont(brls::FONT_MATERIAL_ICONS);
        if (icons != brls::FONT_INVALID)
            this->font = icons;
    }
};
