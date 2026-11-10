//
//  cartridge_art.hpp
//  Equinox
//
//  A Super Nintendo cartridge drawn in nanovg, with a game's cover as its label.
//

#pragma once

#include <borealis.hpp>

// Width over height of the cartridge's front.
constexpr float kCartridgeAspect = 240.0f / 176.0f;

// Draws the front of a cartridge in the w x h box (w / h = kCartridgeAspect):
// the grey shell with its rounded top, the ridge under the top edge, the
// grips on both sides of the label, the waist where the lower part narrows,
// the recessed label holding the cover (cropped to fill), the moulded arrow
// under it and the shell's thickness along the bottom. No text, no logo.
// coverTexture <= 0 leaves the label dark. The focus ring is a 3 px gap in
// focusGap plus a 3 px ring in focusRing, both faded by focusAlpha; shadow
// fades the drop shadow (0 for none).
void drawCartridge(NVGcontext* vg, float x, float y, float w, float h,
                   int coverTexture, int coverW, int coverH, NVGcolor body,
                   NVGcolor focusRing, NVGcolor focusGap, float focusAlpha,
                   float shadow = 1.0f);
