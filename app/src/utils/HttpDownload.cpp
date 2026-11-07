#include "HttpDownload.hpp"

#include "emu/PathUtf8.hpp"

#include <curl/curl.h>

#include <fstream>

namespace {

#ifdef __SWITCH__
const char* const CA_BUNDLE = "romfs:/certs/cacert.pem";
#endif

size_t writeToString(char* data, size_t size, size_t count, void* user) {
    static_cast<std::string*>(user)->append(data, size * count);
    return size * count;
}

size_t writeToFile(char* data, size_t size, size_t count, void* user) {
    auto* file = static_cast<std::ofstream*>(user);
    file->write(data, static_cast<std::streamsize>(size * count));
    return file->good() ? size * count : 0;
}

int onProgress(void* user, curl_off_t total, curl_off_t done, curl_off_t, curl_off_t) {
    const auto* progress = static_cast<const Progress*>(user);
    bool keepGoing = (*progress)(done > 0 ? static_cast<uint64_t>(done) : 0,
                                 total > 0 ? static_cast<uint64_t>(total) : 0);
    return keepGoing ? 0 : 1;
}

// GET `url` over verified TLS, following redirects (release assets redirect
// to GitHub's object storage).
bool httpGet(const std::string& url, curl_write_callback write, void* user, long timeout,
             const Progress* progress, std::string& error, long* status = nullptr) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init failed";
        return false;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Equinox/" APP_VERSION);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
#ifdef __SWITCH__
    curl_easy_setopt(curl, CURLOPT_CAINFO, CA_BUNDLE);
#endif
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, user);
    if (progress && *progress) {
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, progress);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    }

    CURLcode code = curl_easy_perform(curl);
    if (status) {
        long response = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);
        *status = response;
    }
    if (code == CURLE_ABORTED_BY_CALLBACK)
        error = "cancelled";
    else if (code != CURLE_OK)
        error = curl_easy_strerror(code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return code == CURLE_OK;
}

}  // namespace

bool httpGetToString(const std::string& url, std::string& out, long timeoutSec,
                     std::string& error) {
    out.clear();
    return httpGet(url, writeToString, &out, timeoutSec, nullptr, error);
}

bool httpGetToFile(const std::string& url, const std::string& path, long timeoutSec,
                   const Progress& progress, std::string& error) {
    long status = 0;
    return httpGetToFile(url, path, timeoutSec, progress, error, status);
}

bool httpGetToFile(const std::string& url, const std::string& path, long timeoutSec,
                   const Progress& progress, std::string& error, long& httpStatus) {
    httpStatus = 0;
    std::ofstream file(emu::fromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "could not write " + path;
        return false;
    }
    bool ok = httpGet(url, writeToFile, &file, timeoutSec, &progress, error, &httpStatus);
    file.close();
    if (ok && file.fail()) {
        error = "could not write " + path;
        ok = false;
    }
    return ok;
}
