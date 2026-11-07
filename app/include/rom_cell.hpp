//
//  rom_cell.hpp
//  Equinox
//
//  One game on a console's shelf: its cover, or until there is one the
//  console's gradient with a controller and the game's name.
//

#pragma once

#include "emu/Console.hpp"
#include "emu/RomScan.hpp"
#include <borealis.hpp>

#include <functional>
#include <memory>
#include <string>

class RomCell : public brls::Box {
  public:
    RomCell(const emu::Console& console, const emu::RomEntry& rom);

    void setFavorite(bool favorite);
    // Dims the cover with a "not on the SD card" layer (the carousel owns
    // the cell's alpha, so it cannot be used for this).
    void setMissing(bool missing);
    // A spinner and "Fetching cover..." over the generic cover while its
    // cover downloads. Hidden once a cover is shown.
    void setFetching(bool fetching);

    // Shows the cover at this path; decoded off the UI thread.
    void loadCover(const std::string& path);
    bool hasCover() const { return coverShown; }

    const emu::RomEntry& rom() const { return entry; }

    // Asked before playing; return false to only select.
    std::function<bool()> beforeLaunch;
    std::function<void()> onPlay;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

  private:
    const emu::Console& consoleRef;
    emu::RomEntry entry;
    bool coverShown = false;

    brls::Image* image = nullptr;
    brls::Box* generic = nullptr;
    brls::Box* fetchingLayer = nullptr;
    brls::Box* missingLayer = nullptr;
    brls::Image* star = nullptr;

    void applyCover(const std::string& path,
                    std::shared_ptr<unsigned char> pixels, int width,
                    int height);
    bool useCachedCover(const std::string& path);
    void setCoverTexture(int texture);
};
