//
//  cartridge_art.hpp
//  Equinox
//
//  A SNES cartridge drawn in nanovg, with a game's cover as its label.
//

#pragma once

#include <borealis.hpp>

// Draws a 240x140 cartridge scaled to w x h: rounded body, top reflection and
// notch, grip ridges on both sides, the cover as the label (cropped to fill)
// with a gloss on its top half, and the bottom strip with its arrow.
// coverTexture <= 0 leaves the label dark. The focus ring is a 3 px gap in
// focusGap plus a 3 px ring in focusRing, both faded by focusAlpha.
void drawCartridge(NVGcontext* vg, float x, float y, float w, float h,
                   int coverTexture, int coverW, int coverH, NVGcolor body,
                   NVGcolor focusRing, NVGcolor focusGap, float focusAlpha);
