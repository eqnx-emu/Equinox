//
//  mesh_raster.hpp
//  Equinox
//
//  A small CPU renderer for the low-poly objects made with the
//  equinox-lowpoly-3d skill (<slug>.mesh). It frames them with the same
//  camera as the Blender render and lights them with an approximation of
//  the same three lights, so a live object matches its pre-rendered stills.
//  No GPU API: it fills an RGBA buffer that any backend can upload.
//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

class MeshRaster {
  public:
    // Reads a .mesh file (written by eqx3d.export_mesh). False if it can't.
    bool load(const std::string& file);

    // Aspect of the render the camera was set up for (width / height).
    float aspect() const { return resW / resH; }

    // Renders the object turned spin degrees (around the vertical axis, or
    // around its own axle for wheels and gears: see eqx3d.render) and
    // tilted tilt degrees toward the camera, into rgba (w*h*4, straight
    // alpha). margin > 1 zooms out, leaving room for poses that reach past
    // the still's framing. samples: supersampling per axis (2 = 4 per pixel).
    void render(int w, int h, float spin, float tilt, float margin, int samples,
                std::vector<uint8_t>& rgba);

    // Recolours the model's lights (its materials made with emit > 1, such
    // as a status LED): display-encoded r g b in 0..1 and how much they glow
    // (0 = off).
    void setGlow(float r, float g, float b, float emit);

  private:
    struct Material {
        float r, g, b, rough, emit;
    };
    struct Tri {
        float v[9];
        uint32_t mat;
    };

    float rot[3] = {0, 0, 0}, cam[3] = {0, -6, 5}, lens = 50;
    float resW = 1200, resH = 800;
    bool axle = false;
    std::vector<Material> mats;
    std::vector<Tri> tris;
    std::vector<size_t> glows; // the materials setGlow recolours

    std::vector<float> depth;   // supersampled
    std::vector<float> colour;  // supersampled, rgb display-encoded
};
