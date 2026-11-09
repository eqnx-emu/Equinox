#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace emu {

// What a resumable download remembers beside its `.part`, in
// `<partPath>.resume`: the source, the validator (ETag or Last-Modified) the
// server gave for those bytes, and the full size.
struct ResumeState {
    std::string url, validator;
    uint64_t total = 0;
};

bool saveResume(const std::string& partPath, const ResumeState& state);
std::optional<ResumeState> loadResume(const std::string& partPath);
void removeResume(const std::string& partPath);

// Bytes of `partPath` that can be kept for `url`; 0 means start over.
uint64_t resumeOffset(const std::string& partPath, const std::string& url);

}  // namespace emu
