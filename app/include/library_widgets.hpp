//
//  library_widgets.hpp
//  Equinox
//
//  Pieces shared by the two columns of the game library.
//

#pragma once

#include "emu/GameDataJob.hpp"
#include <borealis.hpp>

#include <string>

// A thin bar: `equinox/card` track, `brls/accent` fill.
class LibraryProgressBar : public brls::View {
  public:
    LibraryProgressBar();

    void setFraction(float value);

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

  private:
    float fraction = 0;
};

// setText lays the label out again even when the text is the same; moving
// through the list sets most texts to what they already are.
void setTextIfChanged(brls::Label* label, const std::string& text);

// Why a download stopped, as a sentence ending in a full stop. Empty for
// None and Cancelled.
std::string libraryStopText(emu::DataStop stop);
