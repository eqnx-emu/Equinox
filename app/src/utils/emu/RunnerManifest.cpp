#include "emu/RunnerManifest.hpp"

#include <jansson.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <system_error>
#include <vector>

namespace emu {

namespace {

struct JsonDeleter {
    void operator()(json_t* j) const { json_decref(j); }
};
using JsonPtr = std::unique_ptr<json_t, JsonDeleter>;

bool getString(json_t* obj, const char* key, std::string& out) {
    json_t* v = json_object_get(obj, key);
    if (!json_is_string(v)) return false;
    out = json_string_value(v);
    return true;
}

bool getInt(json_t* obj, const char* key, json_int_t& out) {
    json_t* v = json_object_get(obj, key);
    if (!json_is_integer(v)) return false;
    out = json_integer_value(v);
    return true;
}

std::vector<long> versionParts(const std::string& v) {
    std::vector<long> parts;
    long cur = 0;
    bool any = false;
    for (char c : v) {
        if (c >= '0' && c <= '9') {
            cur = cur * 10 + (c - '0');
            any = true;
        } else if (c == '.') {
            parts.push_back(cur);
            cur = 0;
            any = false;
        }
    }
    if (any || parts.empty()) parts.push_back(cur);
    return parts;
}

int sha256Start(mbedtls_sha256_context* c) {
#if defined(MBEDTLS_VERSION_MAJOR) && MBEDTLS_VERSION_MAJOR >= 3
    return mbedtls_sha256_starts(c, 0);
#else
    return mbedtls_sha256_starts_ret(c, 0);
#endif
}

int sha256Update(mbedtls_sha256_context* c, const unsigned char* data, size_t len) {
#if defined(MBEDTLS_VERSION_MAJOR) && MBEDTLS_VERSION_MAJOR >= 3
    return mbedtls_sha256_update(c, data, len);
#else
    return mbedtls_sha256_update_ret(c, data, len);
#endif
}

int sha256Finish(mbedtls_sha256_context* c, unsigned char out[32]) {
#if defined(MBEDTLS_VERSION_MAJOR) && MBEDTLS_VERSION_MAJOR >= 3
    return mbedtls_sha256_finish(c, out);
#else
    return mbedtls_sha256_finish_ret(c, out);
#endif
}

}  // namespace

std::optional<Manifest> parseManifest(const std::string& json) {
    json_error_t err;
    JsonPtr root(json_loadb(json.data(), json.size(), 0, &err));
    if (!root || !json_is_object(root.get())) return std::nullopt;

    Manifest m;
    json_int_t protocol = 0, size = 0;
    if (!getString(root.get(), "id", m.id) || !getString(root.get(), "console", m.console) ||
        !getString(root.get(), "version", m.version) || !getString(root.get(), "asset", m.asset) ||
        !getString(root.get(), "sha256", m.sha256) || !getInt(root.get(), "protocol", protocol) ||
        !getInt(root.get(), "size", size))
        return std::nullopt;
    if (size < 0 || protocol < 0 || protocol > INT_MAX) return std::nullopt;
    m.protocol = static_cast<int>(protocol);
    m.size = static_cast<uint64_t>(size);
    return m;
}

int compareVersions(const std::string& a, const std::string& b) {
    std::vector<long> pa = versionParts(a), pb = versionParts(b);
    size_t n = pa.size() > pb.size() ? pa.size() : pb.size();
    for (size_t i = 0; i < n; i++) {
        long x = i < pa.size() ? pa[i] : 0;
        long y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

std::optional<Installed> readInstalled(const std::string& path) {
    json_error_t err;
    JsonPtr root(json_load_file(path.c_str(), 0, &err));
    if (!root || !json_is_object(root.get())) return std::nullopt;

    Installed i;
    json_int_t protocol = 0, lastCheck = 0;
    if (!getString(root.get(), "version", i.version) || !getString(root.get(), "sha256", i.sha256) ||
        !getInt(root.get(), "protocol", protocol) || !getInt(root.get(), "lastCheck", lastCheck))
        return std::nullopt;
    i.protocol = static_cast<int>(protocol);
    i.lastCheck = lastCheck;
    return i;
}

bool writeInstalled(const std::string& path, const Installed& installed) {
    JsonPtr root(json_pack("{s:s, s:s, s:i, s:I}", "version", installed.version.c_str(), "sha256",
                           installed.sha256.c_str(), "protocol", installed.protocol, "lastCheck",
                           static_cast<json_int_t>(installed.lastCheck)));
    if (!root) return false;

    std::string tmp = path + ".tmp";
    std::error_code ec;
    if (json_dump_file(root.get(), tmp.c_str(), JSON_INDENT(2)) != 0) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    std::filesystem::rename(tmp, path, ec);
    std::error_code existsEc;
    if (ec && std::filesystem::exists(path, existsEc)) {
        // FAT (libnx) cannot rename over an existing file.
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
    }
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

RunnerAction decide(const std::optional<Installed>& installed, const Manifest& latest,
                    int supportedProtocol) {
    if (latest.protocol != supportedProtocol) return RunnerAction::NeedsNewerEquinox;
    if (!installed) return RunnerAction::Install;
    return compareVersions(installed->version, latest.version) < 0 ? RunnerAction::Update
                                                                   : RunnerAction::UpToDate;
}

bool shouldCheck(int64_t lastCheck, int64_t now) {
    return now < lastCheck || now - lastCheck >= 86400;
}

std::string sha256Hex(const std::string& filePath) {
    std::FILE* f = std::fopen(filePath.c_str(), "rb");
    if (!f) return "";

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    bool ok = sha256Start(&ctx) == 0;
    std::vector<unsigned char> buf(64 * 1024);
    while (ok) {
        size_t n = std::fread(buf.data(), 1, buf.size(), f);
        if (n > 0) ok = sha256Update(&ctx, buf.data(), n) == 0;
        if (n < buf.size()) {
            if (std::ferror(f)) ok = false;
            break;
        }
    }
    std::fclose(f);

    unsigned char digest[32];
    if (ok) ok = sha256Finish(&ctx, digest) == 0;
    mbedtls_sha256_free(&ctx);
    if (!ok) return "";

    static const char* hex = "0123456789abcdef";
    std::string out;
    for (unsigned char b : digest) {
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
}

bool verifyFile(const std::string& filePath, const Manifest& manifest) {
    std::error_code ec;
    auto size = std::filesystem::file_size(filePath, ec);
    if (ec || size != manifest.size) return false;
    std::string actual = sha256Hex(filePath);
    std::string expected = manifest.sha256;
    std::transform(expected.begin(), expected.end(), expected.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return !actual.empty() && actual == expected;
}

std::string manifestUrl(const Console& console) {
    return "https://github.com/" + console.repo + "/releases/latest/download/manifest.json";
}

std::string assetUrl(const Console& console, const Manifest& manifest) {
    return "https://github.com/" + console.repo + "/releases/download/v" + manifest.version + "/" +
           manifest.asset;
}

}  // namespace emu
