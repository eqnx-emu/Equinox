#include "emu/ResumeState.hpp"

#include <jansson.h>

#include <filesystem>
#include <memory>
#include <system_error>

#include "emu/PathUtf8.hpp"

namespace emu {

namespace fs = std::filesystem;

namespace {

struct JsonDeleter {
    void operator()(json_t* j) const { json_decref(j); }
};
using JsonPtr = std::unique_ptr<json_t, JsonDeleter>;

fs::path controlPath(const std::string& partPath) { return fromUtf8(partPath + ".resume"); }

}  // namespace

bool saveResume(const std::string& partPath, const ResumeState& state) {
    JsonPtr root(json_pack("{s:s, s:s, s:I}", "url", state.url.c_str(), "validator",
                           state.validator.c_str(), "total",
                           static_cast<json_int_t>(state.total)));
    if (!root) return false;
    return json_dump_file(root.get(), toUtf8(controlPath(partPath)).c_str(), JSON_COMPACT) == 0;
}

std::optional<ResumeState> loadResume(const std::string& partPath) {
    json_error_t err;
    JsonPtr root(json_load_file(toUtf8(controlPath(partPath)).c_str(), 0, &err));
    if (!root || !json_is_object(root.get())) return std::nullopt;
    const json_t* url = json_object_get(root.get(), "url");
    const json_t* validator = json_object_get(root.get(), "validator");
    const json_t* total = json_object_get(root.get(), "total");
    if (!json_is_string(url) || !json_is_string(validator) || !json_is_integer(total))
        return std::nullopt;
    if (json_integer_value(total) < 0) return std::nullopt;
    ResumeState state;
    state.url = json_string_value(url);
    state.validator = json_string_value(validator);
    state.total = static_cast<uint64_t>(json_integer_value(total));
    return state;
}

void removeResume(const std::string& partPath) {
    std::error_code ec;
    fs::remove(controlPath(partPath), ec);
}

uint64_t resumeOffset(const std::string& partPath, const std::string& url) {
    auto state = loadResume(partPath);
    if (!state || state->url != url) return 0;
    std::error_code ec;
    auto size = fs::file_size(fromUtf8(partPath), ec);
    if (ec || size == 0 || size > state->total) return 0;
    return size;
}

}  // namespace emu
