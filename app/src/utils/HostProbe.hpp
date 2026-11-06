#pragma once

#include <algorithm>
#include <string>

// Borealis-free helpers for the host's /solstice/ping and /solstice/speedtest
// endpoints.

constexpr int SPEEDTEST_MIN_SECONDS = 1;
constexpr int SPEEDTEST_MAX_SECONDS = 10;

inline int clamp_speedtest_seconds(int seconds) {
    return std::clamp(seconds, SPEEDTEST_MIN_SECONDS, SPEEDTEST_MAX_SECONDS);
}

inline std::string build_probe_url(const std::string& address,
                                   unsigned short https_port,
                                   const std::string& path,
                                   const std::string& unique_id,
                                   const std::string& extra_query = "") {
    std::string url = "https://" + address + ":" + std::to_string(https_port) +
                      path + "?uniqueid=" + unique_id;
    if (!extra_query.empty()) url += "&" + extra_query;
    return url;
}

inline std::string build_speedtest_url(const std::string& address,
                                       unsigned short https_port,
                                       const std::string& unique_id,
                                       int seconds) {
    return build_probe_url(
        address, https_port, "/solstice/speedtest", unique_id,
        "seconds=" + std::to_string(clamp_speedtest_seconds(seconds)));
}
