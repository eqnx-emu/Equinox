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

// Why a download stopped, as a sentence ending in a full stop. Empty for
// None and Cancelled.
std::string libraryStopText(emu::DataStop stop);
