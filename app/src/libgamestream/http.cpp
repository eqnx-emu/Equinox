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

#include "http.h"
#include "CryptoManager.hpp"
#include "client.h"
#include "errors.h"
#include <borealis/core/logger.hpp>

#include <curl/curl.h>
#include <chrono>
#include <cstring>
#include <mutex>

static bool curlGlobalInit = false;
static std::string certificateFilePath;
static std::string keyFilePath;

CURL* makeCurl();
void freeCurl(CURL* curl);

struct HTTP_DATA {
    char* memory;
    size_t size;
};

static size_t _write_curl(void* contents, size_t size, size_t nmemb,
                          void* userp) {
    size_t realsize = size * nmemb;
    auto* mem = (HTTP_DATA*)userp;

    mem->memory = (char*)realloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory == NULL)
        return 0;

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

int http_init(const std::string& key_directory) {
    if (!curlGlobalInit) {
#if LIBCURL_VERSION_NUM >= 0x075600
#ifdef USE_OPENSSL_CRYPTO
        curl_global_sslset(CURLSSLBACKEND_OPENSSL, NULL, NULL);
#elif USE_MBEDTLS_CRYPTO
        curl_global_sslset(CURLSSLBACKEND_MBEDTLS, NULL, NULL);
#endif
#endif
        curl_global_init(CURL_GLOBAL_ALL);
        brls::Logger::info("Curl: {}", curl_version());
    } else {
        return GS_OK;
    }

    certificateFilePath = key_directory + "/" + CERTIFICATE_FILE_NAME;
    keyFilePath = key_directory + "/" + KEY_FILE_NAME;

    curlGlobalInit = true;
    return GS_OK;
}

CURL* makeCurl() {
    auto curl = curl_easy_init();

    if (!curl)
        return nullptr;

    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSLENGINE_DEFAULT, 1L);
    curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLCERT, certificateFilePath.c_str());
    curl_easy_setopt(curl, CURLOPT_SSLKEYTYPE, "PEM");
    curl_easy_setopt(curl, CURLOPT_SSLKEY, keyFilePath.c_str());
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _write_curl);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 0L);

    return curl;
}

void freeCurl(CURL* curl) {
    curl_easy_cleanup(curl);
}

int http_request(const std::string& url, Data* data,
                 HTTPRequestTimeout timeout) {
    brls::Logger::info("Curl: Request:\n{}", url.c_str());

    auto* http_data = (HTTP_DATA*)malloc(sizeof(HTTP_DATA));
    http_data->memory = (char*)malloc(1);
    http_data->size = 0;

    auto curl = makeCurl();
    if (!curl) return GS_FAILED;

    curl_easy_setopt(curl, CURLOPT_WRITEDATA, http_data);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        gs_set_error(curl_easy_strerror(res));
        brls::Logger::error("Curl: error: {}", gs_error().c_str());
        free(http_data->memory);
        free(http_data);
        return GS_FAILED;
    } else if (http_data->memory == nullptr) {
        brls::Logger::error("Curl: memory = NULL");
        free(http_data->memory);
        free(http_data);
        return GS_OUT_OF_MEMORY;
    }

    *data = Data(http_data->memory, http_data->size);

    if (http_data->size > 3000) {
        brls::Logger::info("Curl: Response: Ok");
    } else {
        brls::Logger::info("Curl: Response:\n{}", http_data->memory);
    }

    free(http_data->memory);
    free(http_data);
    freeCurl(curl);

    return GS_OK;
}


static size_t _write_discard(void* contents, size_t size, size_t nmemb,
                             void* userp) {
    (void)contents;
    (void)userp;
    return size * nmemb;
}

static int status_to_result(CURL* curl) {
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if (code == 404) {
        gs_set_error("Host does not support this feature");
        return GS_NOT_SUPPORTED;
    }
    if (code < 200 || code >= 300) {
        gs_set_error("HTTP error " + std::to_string(code));
        return GS_FAILED;
    }
    return GS_OK;
}

int http_ping(const std::string& url, float* rtt_ms,
              HTTPRequestTimeout timeout) {
    static std::mutex pingMutex;
    static CURL* pingCurl = nullptr;
    std::lock_guard<std::mutex> lock(pingMutex);

    if (!pingCurl) {
        pingCurl = makeCurl();
        if (!pingCurl) return GS_FAILED;
        curl_easy_setopt(pingCurl, CURLOPT_VERBOSE, 0L);
        curl_easy_setopt(pingCurl, CURLOPT_FAILONERROR, 0L);
        curl_easy_setopt(pingCurl, CURLOPT_WRITEFUNCTION, _write_discard);
        curl_easy_setopt(pingCurl, CURLOPT_FORBID_REUSE, 0L);
    }
    curl_easy_setopt(pingCurl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(pingCurl, CURLOPT_TIMEOUT, (long)timeout);

    auto start = std::chrono::steady_clock::now();
    CURLcode res = curl_easy_perform(pingCurl);
    auto end = std::chrono::steady_clock::now();

    if (res != CURLE_OK) {
        gs_set_error(curl_easy_strerror(res));
        return GS_IO_ERROR;
    }
    int status = status_to_result(pingCurl);
    if (status != GS_OK) return status;

    *rtt_ms =
        std::chrono::duration<float, std::milli>(end - start).count();
    return GS_OK;
}

namespace {
struct SpeedState {
    const std::function<void(double, uint64_t)>* on_progress;
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point lastReport;
    uint64_t bytes = 0;
};
} // namespace

static size_t _write_count(void* contents, size_t size, size_t nmemb,
                           void* userp) {
    (void)contents;
    auto* state = (SpeedState*)userp;
    size_t n = size * nmemb;
    state->bytes += n;

    auto now = std::chrono::steady_clock::now();
    if (now - state->lastReport >= std::chrono::milliseconds(100)) {
        state->lastReport = now;
        if (*state->on_progress)
            (*state->on_progress)(
                std::chrono::duration<double>(now - state->start).count(),
                state->bytes);
    }
    return n;
}

int http_download_discard(
    const std::string& url, long timeout_s,
    const std::function<void(double elapsed_s, uint64_t bytes)>& on_progress) {
    brls::Logger::info("Curl: Speedtest request:\n{}", url.c_str());

    auto curl = makeCurl();
    if (!curl) return GS_FAILED;

    SpeedState state;
    state.on_progress = &on_progress;
    state.start = state.lastReport = std::chrono::steady_clock::now();

    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _write_count);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_s);

    CURLcode res = curl_easy_perform(curl);
    auto end = std::chrono::steady_clock::now();

    int ret = GS_OK;
    if (res != CURLE_OK) {
        gs_set_error(curl_easy_strerror(res));
        brls::Logger::error("Curl: error: {}", gs_error().c_str());
        ret = GS_IO_ERROR;
    } else {
        ret = status_to_result(curl);
    }
    freeCurl(curl);

    if (ret == GS_OK && on_progress)
        on_progress(std::chrono::duration<double>(end - state.start).count(),
                    state.bytes);
    return ret;
}

void http_cleanup() {
    curl_global_cleanup();
}
