//
//  mesh_raster.cpp
//  Equinox
//

#include "mesh_raster.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

struct V3 {
    float x, y, z;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V3 norm(V3 a) {
    float l = std::sqrt(dot(a, a));
    return l > 0 ? a * (1 / l) : a;
}

constexpr float kDeg = 3.14159265f / 180;
constexpr float kPi = 3.14159265f;

// Blender's XYZ Euler: R = Rz * Ry * Rx.
struct M3 {
    float m[9];
    M3 operator*(const M3& o) const {
        M3 r;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                r.m[i * 3 + j] = m[i * 3] * o.m[j] + m[i * 3 + 1] * o.m[3 + j] +
                                 m[i * 3 + 2] * o.m[6 + j];
        return r;
    }
    V3 operator*(V3 v) const {
        return {m[0] * v.x + m[1] * v.y + m[2] * v.z,
                m[3] * v.x + m[4] * v.y + m[5] * v.z,
                m[6] * v.x + m[7] * v.y + m[8] * v.z};
    }
};
M3 euler(float rx, float ry, float rz) {
    float cx = std::cos(rx * kDeg), sx = std::sin(rx * kDeg);
    float cy = std::cos(ry * kDeg), sy = std::sin(ry * kDeg);
    float cz = std::cos(rz * kDeg), sz = std::sin(rz * kDeg);
    return {{cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx,
             sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx,
             -sy, cy * sx, cy * cx}};
}

// AgX tone mapping (polynomial fit of Blender's view transform), with a
// little extra contrast for the "Medium High Contrast" look. Linear in,
// display-encoded out.
V3 agx(V3 c) {
    V3 v = {0.842479f * c.x + 0.078434f * c.y + 0.079224f * c.z,
            0.042328f * c.x + 0.878469f * c.y + 0.079166f * c.z,
            0.042376f * c.x + 0.078434f * c.y + 0.879143f * c.z};
    const float lo = -12.47393f, hi = 4.026069f;
    auto curve = [&](float x) {
        x = std::max(x, 1e-10f);
        x = (std::clamp(std::log2(x), lo, hi) - lo) / (hi - lo);
        float x2 = x * x, x4 = x2 * x2;
        return 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x +
               0.4298f * x2 + 0.1191f * x - 0.00232f;
    };
    v = {curve(v.x), curve(v.y), curve(v.z)};
    // Look: a touch more contrast and saturation.
    const float power = 1.12f, sat = 1.12f;
    v = {std::pow(std::max(v.x, 0.f), power), std::pow(std::max(v.y, 0.f), power),
         std::pow(std::max(v.z, 0.f), power)};
    float luma = 0.2126f * v.x + 0.7152f * v.y + 0.0722f * v.z;
    v = {luma + sat * (v.x - luma), luma + sat * (v.y - luma), luma + sat * (v.z - luma)};
    V3 o = {1.196879f * v.x - 0.098021f * v.y - 0.099030f * v.z,
            -0.052897f * v.x + 1.151903f * v.y - 0.098961f * v.z,
            -0.052972f * v.x - 0.098043f * v.y + 1.151074f * v.z};
    return {std::clamp(o.x, 0.f, 1.f), std::clamp(o.y, 0.f, 1.f), std::clamp(o.z, 0.f, 1.f)};
}

template <typename T>
bool readv(FILE* f, T* out, size_t n = 1) {
    return std::fread(out, sizeof(T), n, f) == n;
}

} // namespace

bool MeshRaster::load(const std::string& file) {
    FILE* f = std::fopen(file.c_str(), "rb");
    if (!f)
        return false;
    char magic[4];
    uint32_t version = 0, count = 0, flags = 0;
    float head[9];
    bool ok = readv(f, magic, 4) && std::memcmp(magic, "EQXM", 4) == 0 &&
              readv(f, &version) && (version == 1 || version == 2) &&
              readv(f, head, 9) && (version < 2 || readv(f, &flags)) &&
              readv(f, &count);
    if (ok) {
        std::copy(head, head + 3, rot);
        std::copy(head + 3, head + 6, cam);
        lens = head[6];
        resW = head[7];
        resH = head[8];
        axle = flags & 1;
        mats.resize(count);
        ok = readv(f, (float*)mats.data(), count * 5) && readv(f, &count);
    }
    if (ok) {
        // In one read: the file holds them exactly as Tri lays them out.
        static_assert(sizeof(Tri) == 10 * 4, "Tri must match the file's 9 floats and index");
        tris.resize(count);
        ok = readv(f, tris.data(), count);
        for (const auto& t : tris)
            ok = ok && t.mat < mats.size();
    }
    std::fclose(f);
    if (!ok) {
        mats.clear();
        tris.clear();
    }
    glows.clear();
    for (size_t i = 0; i < mats.size(); i++)
        if (mats[i].emit > 1)
            glows.push_back(i);
    return ok;
}

void MeshRaster::setGlow(float r, float g, float b, float emit) {
    // r g b come display-encoded, like a theme colour: the materials are linear.
    for (size_t i : glows) {
        mats[i].r = std::pow(r, 2.2f);
        mats[i].g = std::pow(g, 2.2f);
        mats[i].b = std::pow(b, 2.2f);
        mats[i].emit = emit;
    }
}

void MeshRaster::render(int w, int h, float spin, float tilt, float margin,
                        int samples, std::vector<uint8_t>& rgba) {
    rgba.assign((size_t)w * h * 4, 0);
    if (tris.empty() || w <= 0 || h <= 0)
        return;
    const int s = std::max(1, samples);
    const int W = w * s, H = h * s;
    depth.assign((size_t)W * H, 0.0f); // 1/z, 0 = empty
    colour.resize((size_t)W * H * 3);

    // Camera: looks at (0, 0, 0.05) with its up toward +Z, like eqx3d.render().
    const V3 eye = {cam[0], cam[1], cam[2]};
    const V3 fwd = norm(V3{0, 0, 0.05f} - eye);
    const V3 right = norm(cross(fwd, {0, 0, 1}));
    const V3 up = cross(right, fwd);
    // Blender's 36 mm sensor fits the larger side of the render.
    const float focal = lens / 36.0f * (resW >= resH ? (float)W : W * resH / resW) /
                        std::max(margin, 1.0f);

    const M3 model = axle ? euler(rot[0] + tilt, rot[1], rot[2]) * euler(0, 0, spin)
                          : euler(rot[0] + tilt, rot[1], rot[2] + spin);

    // Lights from eqx3d.render(): a sun and two area lights, plus the dim
    // night-sky world. Area lights are treated as points (P / (pi r^2)).
    const V3 sun = euler(45, -28, 5) * V3{0, 0, 1};
    struct Lamp {
        V3 pos;
        float power;
        V3 tint;
    };
    const Lamp lamps[2] = {{{-4, -3, 2.5f}, 250, {1, 1, 1}},
                           {{3, 4, 3}, 300, {0.85f, 0.8f, 1.0f}}};
    const V3 sky = {0.05f * 0.6f, 0.055f * 0.6f, 0.1f * 0.6f};
    const float exposure = std::pow(2.0f, -0.35f);

    for (const Tri& t : tris) {
        V3 p[3];
        for (int i = 0; i < 3; i++)
            p[i] = model * V3{t.v[i * 3], t.v[i * 3 + 1], t.v[i * 3 + 2]};

        // Flat shading: one normal per face, toward the camera. The light is
        // worked out at each corner and blended, so the near/far falloff of
        // the area lights and highlights reads across a face with no seams
        // between its triangles.
        // Faces wind counter-clockwise seen from outside (Blender's
        // convention), so a face turned away is hidden behind the body.
        V3 n = norm(cross(p[1] - p[0], p[2] - p[0]));
        if (dot(n, eye - p[0]) <= 0)
            continue;

        const Material& m = mats[t.mat];
        const V3 albedo = {m.r, m.g, m.b};
        const float rough = std::max(m.rough, 0.08f);
        const float shine = 2 / (rough * rough * rough * rough) - 2;
        V3 out[3];
        for (int i = 0; i < 3; i++) {
            const V3 view = norm(eye - p[i]);
            auto light = [&](V3 dir, V3 irr) {
                float nl = dot(n, dir);
                if (nl <= 0)
                    return V3{0, 0, 0};
                V3 hv = norm(dir + view);
                float spec = 0.04f * (shine + 8) / (8 * kPi) *
                             std::pow(std::max(dot(n, hv), 0.f), shine);
                V3 e = irr * nl;
                return V3{albedo.x / kPi * e.x, albedo.y / kPi * e.y,
                          albedo.z / kPi * e.z} +
                       e * spec;
            };
            V3 lin = light(sun, {2.4f, 2.4f, 2.4f});
            for (const Lamp& l : lamps) {
                V3 d = l.pos - p[i];
                float e = l.power / (kPi * dot(d, d));
                lin = lin + light(norm(d), l.tint * e);
            }
            float skyUp = 0.5f + 0.5f * n.z;
            lin = lin + V3{albedo.x * sky.x, albedo.y * sky.y, albedo.z * sky.z} * skyUp;
            lin = lin + albedo * m.emit;
            out[i] = agx(lin * exposure);
        }

        // Project to the supersampled screen.
        float sx[3], sy[3], iz[3];
        bool behind = false;
        for (int i = 0; i < 3; i++) {
            V3 r = p[i] - eye;
            float z = dot(r, fwd);
            if (z <= 0.01f)
                behind = true;
            iz[i] = 1 / z;
            sx[i] = W * 0.5f + dot(r, right) * focal * iz[i];
            sy[i] = H * 0.5f - dot(r, up) * focal * iz[i];
        }
        if (behind)
            continue;
        float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (std::fabs(area) < 1e-6f)
            continue;
        const int x0 = std::max(0, (int)std::floor(std::min({sx[0], sx[1], sx[2]})));
        const int x1 = std::min(W - 1, (int)std::ceil(std::max({sx[0], sx[1], sx[2]})));
        const int y0 = std::max(0, (int)std::floor(std::min({sy[0], sy[1], sy[2]})));
        const int y1 = std::min(H - 1, (int)std::ceil(std::max({sy[0], sy[1], sy[2]})));
        const float inv = 1 / area;
        // Edge functions step by a constant per pixel.
        const float a0 = (sy[1] - sy[2]) * inv;
        const float a1 = (sy[2] - sy[0]) * inv;
        for (int y = y0; y <= y1; y++) {
            const float py = y + 0.5f, px = x0 + 0.5f;
            float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) * inv;
            float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) * inv;
            float* dz = &depth[(size_t)y * W];
            float* col = &colour[(size_t)y * W * 3];
            for (int x = x0; x <= x1; x++, w0 += a0, w1 += a1) {
                const float w2 = 1 - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0)
                    continue;
                const float z = w0 * iz[0] + w1 * iz[1] + w2 * iz[2];
                if (z <= dz[x])
                    continue;
                dz[x] = z;
                float* o = col + x * 3;
                o[0] = w0 * out[0].x + w1 * out[1].x + w2 * out[2].x;
                o[1] = w0 * out[0].y + w1 * out[1].y + w2 * out[2].y;
                o[2] = w0 * out[0].z + w1 * out[1].z + w2 * out[2].z;
            }
        }
    }

    // Box-filter down to w*h; alpha is the covered share of each pixel.
    const float n = (float)(s * s);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float r = 0, g = 0, b = 0;
            int hit = 0;
            for (int j = 0; j < s; j++)
                for (int i = 0; i < s; i++) {
                    size_t k = (size_t)(y * s + j) * W + (x * s + i);
                    if (depth[k] <= 0)
                        continue;
                    r += colour[k * 3];
                    g += colour[k * 3 + 1];
                    b += colour[k * 3 + 2];
                    hit++;
                }
            if (!hit)
                continue;
            uint8_t* o = &rgba[((size_t)y * w + x) * 4];
            o[0] = (uint8_t)std::lround(r / hit * 255);
            o[1] = (uint8_t)std::lround(g / hit * 255);
            o[2] = (uint8_t)std::lround(b / hit * 255);
            o[3] = (uint8_t)std::lround(hit / n * 255);
        }
}
