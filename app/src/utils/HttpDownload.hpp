#pragma once

#include <cstdint>
#include <functional>
#include <string>

// HTTPS GET over curl: verified TLS (romfs CA bundle on Switch, the system
// store elsewhere), redirects followed, HTTP errors treated as failures.

// Called while a download runs; total is 0 until the server sends a length.
// Returning false cancels the download.
using Progress = std::function<bool(uint64_t done, uint64_t total)>;

bool httpGetToString(const std::string& url, std::string& out, long timeoutSec,
                     std::string& error);

// Writes the body to `path`. On failure the partial file is left for the
// caller to remove.
bool httpGetToFile(const std::string& url, const std::string& path, long timeoutSec,
                   const Progress& progress, std::string& error);
