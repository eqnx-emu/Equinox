//
//  image_decode.hpp
//  Equinox
//
//  Reading cover and screenshot files off the UI thread, at the size they
//  are drawn rather than the size they were saved.
//

#pragma once

#include <atomic>
#include <memory>
#include <string>

struct DecodedImage {
    std::shared_ptr<unsigned char> pixels; // RGBA; null when the file can't be read
    int width = 0, height = 0;
};

// Covers share one texture per file (TextureCache) between the library's
// cartridge and the favorites tile, so both ask for this size: enough for
// either at the largest UI scale (1.5, docked).
constexpr int kCoverMinW = 288;
constexpr int kCoverMinH = 336;

// Reads path as RGBA. An image larger than needed to cover minW x minH
// pixels is scaled down to that, keeping its shape; 0 keeps it whole.
DecodedImage decodeImageFile(const std::string& path, int minW = 0, int minH = 0);

// Bumped by the view when it stops wanting the image being decoded.
using ImageTicket = std::shared_ptr<std::atomic<unsigned>>;

inline ImageTicket makeImageTicket() {
    return std::make_shared<std::atomic<unsigned>>(0);
}

// decodeImageFile, unless the ticket moved past wanted while the job waited
// in the pool: then nothing is read and the result is empty.
DecodedImage decodeIfWanted(const ImageTicket& ticket, unsigned wanted, const std::string& path,
                            int minW = 0, int minH = 0);
