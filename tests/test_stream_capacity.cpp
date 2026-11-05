#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "StreamCapacity.hpp"

TEST_CASE("min bitrate table H264 60fps") {
    CHECK(min_recommended_bitrate_kbps(1280, 720, 60, H264) == 8000);
    CHECK(min_recommended_bitrate_kbps(1920, 1080, 60, H264) == 15000);
    CHECK(min_recommended_bitrate_kbps(2560, 1440, 60, H264) == 30000);
    CHECK(min_recommended_bitrate_kbps(854, 480, 30, H264) == 2000);
}

TEST_CASE("codec factors") {
    CHECK(min_recommended_bitrate_kbps(1920, 1080, 60, H265) == 10500);
    CHECK(min_recommended_bitrate_kbps(1920, 1080, 60, AV1) == 9000);
}

TEST_CASE("fps factor above 60") {
    CHECK(min_recommended_bitrate_kbps(1920, 1080, 120, H264) == 21500);
}

TEST_CASE("floor and ceiling") {
    CHECK(min_recommended_bitrate_kbps(640, 360, 30, H265) >= 1000);
    CHECK(min_recommended_bitrate_kbps(2560, 1440, 120, H264) <= 100000);
    CHECK(min_recommended_bitrate_kbps(7680, 4320, 120, H264) == 100000);
}

TEST_CASE("interpolation between table points") {
    int v = min_recommended_bitrate_kbps(1600, 900, 60, H264);
    CHECK(v > 8000);
    CHECK(v < 15000);
}

TEST_CASE("stream_size_for") {
    CHECK(stream_size_for(720, 0, 0, 100) == std::make_pair(1280, 720));
    CHECK(stream_size_for(1080, 0, 0, 100) == std::make_pair(1920, 1080));
    CHECK(stream_size_for(-1, 1280, 720, 75) == std::make_pair(960, 540));
    CHECK(stream_size_for(-1, 1280, 720, 100) == std::make_pair(1280, 720));
    CHECK(stream_size_for(-1, 1281, 721, 100) == std::make_pair(1282, 722));
    CHECK(stream_size_for(-1, 0, 0, 50) == std::make_pair(0, 0));
}
