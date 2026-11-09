//
//  turntable.hpp
//  Equinox
//
//  A low-poly 3D object (made with the equinox-lowpoly-3d skill), drawn
//  floating with no card around it. While active the object spins and bobs,
//  and when it stops being active it turns back to the front.
//
//  Two sources:
//  - "<name>.png": 72 pre-rendered views of one full turn, 12 per row.
//  - "<name>.mesh": the model itself, drawn live by MeshRaster. It turns
//    smoothly and also rocks toward the viewer while active, which the
//    fixed views can't do.
//

#pragma once

#include <borealis.hpp>

#include <memory>
#include <string>
#include <vector>

class Turntable {
  public:
    // path: inside the resources, e.g. "img/3d/snes.png" or ".mesh".
    explicit Turntable(std::string path);
    ~Turntable();

    // The image is in the resources: use a Turntable instead of the card.
    static bool exists(const std::string& path);

    // Spin (selected) or turn back to the front; hops when it starts.
    void setActive(bool active);

    // Live models only: recolours the object's lights (a status LED) to
    // color, glowing emit (0 = off). See MeshRaster::setGlow.
    void setGlow(NVGcolor color, float emit);

    // Draws the object centred on (cx, cy), width wide, with its soft shadow
    // on the ground at groundY.
    void draw(NVGcontext* vg, float cx, float cy, float width, float groundY,
              float alpha);

  private:
    struct LiveRender;

    void drawLive(NVGcontext* vg, float cx, float cy, float w, float h,
                  float renderW, float tilt, float alpha);
    void collectRender(NVGcontext* vg);
    void startRender(int w, int h, float tilt, int samples);

    std::string file;
    std::shared_ptr<LiveRender> live; // live source; null for the image
    bool rendering = false; // a worker has, or just finished, a render
    int jobW = 0, jobH = 0; // size of that render
    brls::Time nextRenderAt = 0;
    int imageW = 0, imageH = 0;
    float drawnSpin = -1, drawnTilt = 0; // the pose last sent to a worker
    int drawnSamples = 0;
    bool stale = false; // the model changed: render it again
    bool glowPending = false; // waits for no worker to be reading the model
    NVGcolor glowColor{};
    float glowEmit = 0;
    int image = 0; // loaded on the first draw; -1 when it can't be read
    float spin = 0; // degrees
    float shownTilt = 0; // degrees
    bool active = false;
    brls::Time lastFrame = 0;
    brls::Animatable focus = 0.0f;
    brls::Animatable hop = 0.0f;
};
