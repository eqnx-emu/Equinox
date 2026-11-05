/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "Data.hpp"
#include <cstdint>
#include <functional>
#include <string>

enum HTTPRequestTimeout : long {
    HTTPRequestTimeoutLow = 1,
    HTTPRequestTimeoutMedium = 5,
    HTTPRequestTimeoutLong = 120
};

int http_init(const std::string& key_directory);
int http_request(const std::string& url, Data* data, HTTPRequestTimeout timeout);


// GET `url` and measure the round trip (request sent -> response complete) in
// milliseconds. A libcurl handle is kept and reused between calls, so only the
// first call on a connection pays for the TCP/TLS handshake; callers should
// discard one warm-up ping before collecting samples. Not thread-safe across
// concurrent callers (calls are serialized internally).
// Returns GS_OK, GS_NOT_SUPPORTED (HTTP 404) or GS_FAILED / GS_IO_ERROR.
int http_ping(const std::string& url, float* rtt_ms,
              HTTPRequestTimeout timeout = HTTPRequestTimeoutLow);

// GET `url`, discarding the body (only bytes are counted). `on_progress` is
// invoked at most every 100 ms while data arrives, plus once at the end, with
// the seconds since the request started and the total bytes received.
// `timeout_s` is the whole-request timeout.
// Returns GS_OK, GS_NOT_SUPPORTED (HTTP 404) or GS_FAILED / GS_IO_ERROR.
int http_download_discard(
    const std::string& url, long timeout_s,
    const std::function<void(double elapsed_s, uint64_t bytes)>& on_progress);
