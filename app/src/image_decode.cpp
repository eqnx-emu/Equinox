//
//  image_decode.cpp
//  Equinox
//

#include "image_decode.hpp"

#include <borealis/extern/nanovg/stb_image.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

std::shared_ptr<unsigned char> stbPixels(unsigned char* px) {
    return std::shared_ptr<unsigned char>(px, [](unsigned char* p) {
        if (p)
            stbi_image_free(p);
    });
}

// Each target pixel is the average of the source pixels under it. Enough for
// the factors used here (above 1:1, rarely past 1:6).
std::shared_ptr<unsigned char> shrink(const unsigned char* src, int sw, int sh, int dw, int dh) {
    auto* dst = static_cast<unsigned char*>(std::malloc(static_cast<size_t>(dw) * dh * 4));
    if (!dst)
        return nullptr;
    std::vector<int> xs(dw + 1);
    for (int x = 0; x <= dw; x++)
        xs[x] = static_cast<int>(static_cast<int64_t>(x) * sw / dw);
    for (int y = 0; y < dh; y++) {
        const int y0 = static_cast<int>(static_cast<int64_t>(y) * sh / dh);
        const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<int64_t>(y + 1) * sh / dh));
        unsigned char* out = dst + static_cast<size_t>(y) * dw * 4;
        for (int x = 0; x < dw; x++) {
            const int x0 = xs[x];
            const int x1 = std::max(x0 + 1, xs[x + 1]);
            uint32_t sum[4] = {0, 0, 0, 0};
            for (int sy = y0; sy < y1; sy++) {
                const unsigned char* in = src + (static_cast<size_t>(sy) * sw + x0) * 4;
                for (int sx = x0; sx < x1; sx++, in += 4) {
                    sum[0] += in[0];
                    sum[1] += in[1];
                    sum[2] += in[2];
                    sum[3] += in[3];
                }
            }
            const uint32_t n = static_cast<uint32_t>((y1 - y0) * (x1 - x0));
            for (int c = 0; c < 4; c++)
                out[x * 4 + c] = static_cast<unsigned char>((sum[c] + n / 2) / n);
        }
    }
    return std::shared_ptr<unsigned char>(dst, [](unsigned char* p) { std::free(p); });
}

} // namespace

DecodedImage decodeImageFile(const std::string& path, int minW, int minH) {
    DecodedImage image;
    int channels = 0;
    auto pixels = stbPixels(stbi_load(path.c_str(), &image.width, &image.height, &channels, 4));
    if (!pixels || image.width <= 0 || image.height <= 0)
        return {};
    image.pixels = pixels;
    if (minW <= 0 || minH <= 0)
        return image;

    // The smallest size that still covers minW x minH.
    const float scale = std::max(static_cast<float>(minW) / image.width,
                                 static_cast<float>(minH) / image.height);
    if (scale >= 1.0f)
        return image;
    const int w = std::max(1, static_cast<int>(image.width * scale + 0.5f));
    const int h = std::max(1, static_cast<int>(image.height * scale + 0.5f));
    if (auto small = shrink(pixels.get(), image.width, image.height, w, h)) {
        image.pixels = small;
        image.width = w;
        image.height = h;
    }
    return image;
}

DecodedImage decodeIfWanted(const ImageTicket& ticket, unsigned wanted, const std::string& path,
                            int minW, int minH) {
    if (ticket->load() != wanted)
        return {};
    return decodeImageFile(path, minW, minH);
}
