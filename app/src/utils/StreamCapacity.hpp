#pragma once

#include "VideoCodec.hpp"
#include <utility>

// Minimum recommended bitrate in kbps for the given stream parameters.
int min_recommended_bitrate_kbps(int width, int height, int fps,
                                 VideoCodec codec);

// Stream size for a resolution setting (height, or -1 for native window size
// scaled by native_scale percent). Width derived as height * 16 / 9 for fixed
// heights. Both dimensions are rounded up to even values.
std::pair<int, int> stream_size_for(int resolution_setting, int native_w,
                                    int native_h, int native_scale);
