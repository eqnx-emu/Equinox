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
