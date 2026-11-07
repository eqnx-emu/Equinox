#include "emu/GameDataHttp.hpp"

#include "HttpDownload.hpp"

namespace emu {

namespace {

constexpr long TIMEOUT_SEC = 20;

}  // namespace

FetchResult HttpDataTransport::toFile(const std::string& url, const std::string& path) {
    const Progress progress = [this](uint64_t, uint64_t) { return !stop_->load(); };
    std::string error;
    long status = 0;
    bool writeFailed = false;
    if (httpGetToFile(url, path, TIMEOUT_SEC, progress, error, status, writeFailed))
        return FetchResult::Ok;
    if (stop_->load()) return FetchResult::Cancelled;
    if (writeFailed) return FetchResult::Disk;
    return status == 404 || status == 403 ? FetchResult::NotFound : FetchResult::Transient;
}

FetchResult HttpDataTransport::toString(const std::string& url, std::string& body, long& httpStatus) {
    const Progress progress = [this](uint64_t, uint64_t) { return !stop_->load(); };
    std::string error;
    const bool ok = httpGetToString(url, body, TIMEOUT_SEC, progress, error, httpStatus);
    if (stop_->load()) return FetchResult::Cancelled;
    // An HTTP error is still an answer: the caller reads what it means.
    return ok || httpStatus >= 400 ? FetchResult::Ok : FetchResult::Transient;
}

}  // namespace emu
