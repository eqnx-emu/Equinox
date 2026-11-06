#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "HostProbe.hpp"

TEST_CASE("speedtest seconds are clamped to [1,10]") {
    CHECK(clamp_speedtest_seconds(0) == 1);
    CHECK(clamp_speedtest_seconds(-5) == 1);
    CHECK(clamp_speedtest_seconds(5) == 5);
    CHECK(clamp_speedtest_seconds(10) == 10);
    CHECK(clamp_speedtest_seconds(99) == 10);
}

TEST_CASE("probe urls") {
    CHECK(build_probe_url("192.168.1.2", 47984, "/solstice/ping", "ABC") ==
          "https://192.168.1.2:47984/solstice/ping?uniqueid=ABC");
    CHECK(build_speedtest_url("h", 47984, "ABC", 50) ==
          "https://h:47984/solstice/speedtest?uniqueid=ABC&seconds=10");
}
