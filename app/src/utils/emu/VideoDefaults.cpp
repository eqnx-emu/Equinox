#include "emu/VideoDefaults.hpp"

#include "emu/PathUtf8.hpp"
#include "emu/ReplaceFile.hpp"

#include <jansson.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <system_error>

namespace emu {

const std::vector<std::string> kSizeIds = {"original", "8:7", "4:3", "full"};
const std::vector<std::string> kSmoothIds = {"pixel", "sharp", "soft"};
const std::vector<std::string> kFilterIds = {"none", "scanlines", "ntsc"};
const std::vector<std::string> kBorderIds = {"equinox", "cover", "tv", "black"};

namespace {

struct JsonDeleter {
    void operator()(json_t* j) const { json_decref(j); }
};
using JsonPtr = std::unique_ptr<json_t, JsonDeleter>;

void pick(json_t* obj, const char* key, const std::vector<std::string>& allowed, std::string& out) {
    json_t* v = json_object_get(obj, key);
    if (!json_is_string(v)) return;
    const std::string value = json_string_value(v);
    if (std::find(allowed.begin(), allowed.end(), value) != allowed.end()) out = value;
}

bool readFile(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    const bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
}

}  // namespace

std::string videoDefaultsPath(const ConsolePaths& paths) { return paths.emulator + "/video.json"; }

void recoverVideoDefaults(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path target = fromUtf8(path);
    if (fs::exists(target, ec) || ec) return;
    for (const char* suffix : {".bak", ".tmp"}) {
        const fs::path side = fromUtf8(path + suffix);
        if (!fs::is_regular_file(side, ec)) continue;
        fs::rename(side, target, ec);
        return;
    }
}

VideoDefaults readVideoDefaults(const std::string& path) {
    recoverVideoDefaults(path);
    VideoDefaults out;
    std::string text;
    if (!readFile(path, text)) return out;
    // Jansson rejects a leading BOM, which some editors add.
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    json_error_t err;
    JsonPtr root(json_loadb(text.data(), text.size(), 0, &err));
    if (!root || !json_is_object(root.get())) return out;
    pick(root.get(), "size", kSizeIds, out.size);
    pick(root.get(), "smooth", kSmoothIds, out.smooth);
    pick(root.get(), "filter", kFilterIds, out.filter);
    pick(root.get(), "border", kBorderIds, out.border);
    return out;
}

bool writeVideoDefaults(const std::string& path, const VideoDefaults& defaults) {
    JsonPtr root(json_pack("{s:s, s:s, s:s, s:s}", "size", defaults.size.c_str(), "smooth",
                           defaults.smooth.c_str(), "filter", defaults.filter.c_str(), "border",
                           defaults.border.c_str()));
    if (!root) return false;

    const std::string tmp = path + ".tmp";
    std::error_code ec;
    if (json_dump_file(root.get(), tmp.c_str(), JSON_INDENT(2)) != 0) {
        std::filesystem::remove(fromUtf8(tmp), ec);
        return false;
    }
    // The old file stays as .bak until the new one is in place.
    if (!replaceKeepingBackup(fromUtf8(tmp), fromUtf8(path), fromUtf8(path + ".bak"))) {
        std::filesystem::remove(fromUtf8(tmp), ec);
        return false;
    }
    return true;
}

}  // namespace emu
