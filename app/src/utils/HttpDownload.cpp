#include "HttpDownload.hpp"

#include "emu/PathUtf8.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "emu/ResumeState.hpp"

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

// Lowercased host of an http(s) URL, without userinfo or port.
std::string hostOf(const std::string& url) {
    size_t start = url.find("://");
    start = start == std::string::npos ? 0 : start + 3;
    size_t end = url.find_first_of("/?#", start);
    std::string authority = url.substr(start, end == std::string::npos ? end : end - start);
    size_t at = authority.rfind('@');
    if (at != std::string::npos) authority.erase(0, at + 1);
    size_t colon = authority.find(':');
    if (colon != std::string::npos) authority.erase(colon);
    std::transform(authority.begin(), authority.end(), authority.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return authority;
}

void applyCommon(CURL* curl, const std::string& url) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
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
}

// GET `url` over verified TLS, following redirects (release assets redirect
// to GitHub's object storage).
bool httpGet(const std::string& url, curl_write_callback write, void* user, long timeout,
             const Progress* progress, std::string& error, long* status = nullptr,
             bool* writeError = nullptr) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl_easy_init failed";
        return false;
    }

    // The GitHub media type is meant for the API; other hosts get the default.
    struct curl_slist* headers = nullptr;
    if (hostOf(url) == "api.github.com")
        headers = curl_slist_append(headers, "Accept: application/vnd.github+json");

    applyCommon(curl, url);
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
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
    if (writeError) *writeError = code == CURLE_WRITE_ERROR;
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

bool httpGetToString(const std::string& url, std::string& out, long timeoutSec,
                     const Progress& progress, std::string& error, long& httpStatus) {
    out.clear();
    httpStatus = 0;
    return httpGet(url, writeToString, &out, timeoutSec, &progress, error, &httpStatus);
}

bool httpGetToFile(const std::string& url, const std::string& path, long timeoutSec,
                   const Progress& progress, std::string& error) {
    long status = 0;
    return httpGetToFile(url, path, timeoutSec, progress, error, status);
}

bool httpGetToFile(const std::string& url, const std::string& path, long timeoutSec,
                   const Progress& progress, std::string& error, long& httpStatus) {
    bool writeFailed = false;
    return httpGetToFile(url, path, timeoutSec, progress, error, httpStatus, writeFailed);
}

bool httpGetToFile(const std::string& url, const std::string& path, long timeoutSec,
                   const Progress& progress, std::string& error, long& httpStatus,
                   bool& writeFailed) {
    httpStatus = 0;
    writeFailed = false;
    std::ofstream file(emu::fromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "could not write " + path;
        writeFailed = true;
        return false;
    }
    bool ok = httpGet(url, writeToFile, &file, timeoutSec, &progress, error, &httpStatus,
                      &writeFailed);
    file.close();
    if (ok && file.fail()) {
        error = "could not write " + path;
        writeFailed = true;
        ok = false;
    }
    return ok;
}

namespace {

namespace fs = std::filesystem;

struct ResumableCtx {
    CURL* curl = nullptr;
    std::string url, partPath;
    std::ofstream file;
    uint64_t offset = 0;
    std::string etag, lastModified;
    bool started = false;
    bool restarted = false;
    bool writeFailed = false;
    const Progress* progress = nullptr;
};

bool startsWithNoCase(const std::string& line, const char* prefix) {
    size_t n = std::char_traits<char>::length(prefix);
    if (line.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(line[i])) != prefix[i]) return false;
    return true;
}

std::string headerValue(const std::string& line, size_t nameLen) {
    size_t begin = line.find_first_not_of(" \t", nameLen);
    size_t end = line.find_last_not_of("\r\n \t");
    if (begin == std::string::npos || end == std::string::npos || end < begin) return {};
    return line.substr(begin, end - begin + 1);
}

size_t onHeader(char* data, size_t size, size_t count, void* user) {
    auto* ctx = static_cast<ResumableCtx*>(user);
    std::string line(data, size * count);
    if (startsWithNoCase(line, "http/")) {
        // A new response (redirect hop): earlier validators do not apply.
        ctx->etag.clear();
        ctx->lastModified.clear();
    } else if (startsWithNoCase(line, "etag:")) {
        ctx->etag = headerValue(line, 5);
    } else if (startsWithNoCase(line, "last-modified:")) {
        ctx->lastModified = headerValue(line, 14);
    }
    return size * count;
}

// A weak ETag cannot go into If-Range, so the date stands in for it.
std::string validatorOf(const ResumableCtx& ctx) {
    if (!ctx.etag.empty() && ctx.etag.rfind("W/", 0) != 0) return ctx.etag;
    return ctx.lastModified;
}

// First body bytes: the response is known, so decide between append and
// restart, check the free space, and write the control file.
bool beginBody(ResumableCtx& ctx) {
    long status = 0;
    curl_easy_getinfo(ctx.curl, CURLINFO_RESPONSE_CODE, &status);
    curl_off_t length = -1;
    curl_easy_getinfo(ctx.curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);

    if (ctx.offset > 0 && status != 206) {
        // The server sent the whole body (or the validator no longer matches).
        ctx.file.close();
        ctx.file.open(emu::fromUtf8(ctx.partPath), std::ios::binary | std::ios::trunc);
        ctx.offset = 0;
        ctx.restarted = true;
        if (!ctx.file) return false;
    }

    uint64_t total = 0;
    if (length >= 0) {
        total = static_cast<uint64_t>(length) + ctx.offset;
        fs::path dir = emu::fromUtf8(ctx.partPath).parent_path();
        std::error_code ec;
        fs::space_info info = fs::space(dir.empty() ? fs::path(".") : dir, ec);
        if (!ec && info.available < static_cast<uint64_t>(length)) return false;
    }

    std::string validator = validatorOf(ctx);
    if (total > 0 && !validator.empty())
        emu::saveResume(ctx.partPath, {ctx.url, validator, total});
    else
        emu::removeResume(ctx.partPath);
    return true;
}

size_t writeResumable(char* data, size_t size, size_t count, void* user) {
    auto& ctx = *static_cast<ResumableCtx*>(user);
    if (!ctx.started) {
        ctx.started = true;
        if (!beginBody(ctx)) {
            ctx.writeFailed = true;
            return 0;
        }
    }
    ctx.file.write(data, static_cast<std::streamsize>(size * count));
    if (!ctx.file.good()) {
        ctx.writeFailed = true;
        return 0;
    }
    return size * count;
}

int onResumableProgress(void* user, curl_off_t total, curl_off_t done, curl_off_t, curl_off_t) {
    auto* ctx = static_cast<ResumableCtx*>(user);
    uint64_t base = ctx->offset;
    uint64_t d = base + (done > 0 ? static_cast<uint64_t>(done) : 0);
    uint64_t t = total > 0 ? base + static_cast<uint64_t>(total) : 0;
    return (*ctx->progress)(d, t) ? 0 : 1;
}

}  // namespace

ResumableResult httpGetResumable(const std::string& url, const std::string& partPath,
                                 const Progress& progress) {
    ResumableResult result{false, false, false, 0, {}};

    ResumableCtx ctx;
    ctx.url = url;
    ctx.partPath = partPath;
    ctx.progress = &progress;

    std::string validator;
    ctx.offset = emu::resumeOffset(partPath, url);
    if (ctx.offset > 0) {
        if (auto saved = emu::loadResume(partPath)) validator = saved->validator;
        if (validator.empty()) ctx.offset = 0;
    }
    if (ctx.offset == 0) emu::removeResume(partPath);

    ctx.file.open(emu::fromUtf8(partPath),
                  std::ios::binary | (ctx.offset > 0 ? std::ios::app : std::ios::trunc));
    if (!ctx.file) {
        result.error = "could not write " + partPath;
        result.writeFailed = true;
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "curl_easy_init failed";
        return result;
    }
    ctx.curl = curl;

    struct curl_slist* headers = nullptr;
    if (ctx.offset > 0) {
        headers = curl_slist_append(headers, ("If-Range: " + validator).c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(ctx.offset));
    }

    applyCommon(curl, url);
    // No total timeout: a large file on a slow link is fine while it moves.
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, onHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeResumable);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    if (progress) {
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onResumableProgress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    }

    CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
    ctx.file.close();

    result.restarted = ctx.restarted;
    result.writeFailed = ctx.writeFailed || code == CURLE_WRITE_ERROR;
    if (code == CURLE_OK && ctx.file.fail()) {
        result.writeFailed = true;
        code = CURLE_WRITE_ERROR;
    }
    if (code == CURLE_ABORTED_BY_CALLBACK)
        result.error = "cancelled";
    else if (result.writeFailed)
        result.error = "could not write " + partPath;
    else if (code != CURLE_OK)
        result.error = curl_easy_strerror(code);

    if (code == CURLE_OK) {
        emu::removeResume(partPath);
    } else if (result.status == 416) {
        // The saved range no longer fits the file on the server.
        std::error_code ec;
        fs::remove(emu::fromUtf8(partPath), ec);
        emu::removeResume(partPath);
    }

    result.ok = code == CURLE_OK;
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}
