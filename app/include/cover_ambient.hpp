//
//  cover_ambient.hpp
//  Equinox
//
//  Glow behind a cover shelf, in the colours of the focused game's cover.
//  Fades from one game to the next; the colours are read off the UI thread
//  and kept per cover.
//

#pragma once

#include "backdrop_frame.hpp"
#include <Settings.hpp>
#include <borealis.hpp>

#include <map>
#include <memory>
#include <string>

class CoverAmbient {
  public:
    CoverAmbient();
    ~CoverAmbient();

    // Glows in the colours of this game's cover, once it is on disk.
    void select(const Host& host, int appId);
    // Glows in the colours of the cover at this path. A missing or
    // unreadable file fades the glow out; selecting it again once the file
    // is there lights it.
    void select(const std::string& coverPath);
    // Fades the glow out.
    void clear();

    // Paints over the whole frame (under header and footer) when the owner
    // sits in a BackdropFrame. Call from willAppear / willDisappear.
    void attach(brls::View* owner);
    void detach();
    // Paints inside the owner when no BackdropFrame took it; call first
    // thing in the owner's draw().
    void drawFallback(NVGcontext* vg, float x, float y, float width,
                      float height);

  private:
    struct Colors {
        NVGcolor top;
        NVGcolor bottom;
        bool valid = false;
    };

    std::map<std::string, Colors> cache;
    Colors shown, previous;
    std::string selected;
    brls::Animatable fade = 1.0f;
    BackdropFrame* frame = nullptr;
    // Cleared on destruction, so colours read in the background are dropped.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

    void select(const std::string& path, bool onDisk);
    void show(const Colors& colors);
    void paint(NVGcontext* vg, float x, float y, float width, float height);
    void paintOne(NVGcontext* vg, const Colors& colors, float alpha, float x,
                  float y, float width, float height);
};
