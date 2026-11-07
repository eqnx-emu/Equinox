#pragma once

#include <atomic>
#include <memory>

#include "emu/GameDataJob.hpp"

namespace emu {

// DataTransport over HttpDownload. A download in progress is cancelled when
// *stop becomes true.
class HttpDataTransport : public DataTransport {
  public:
    explicit HttpDataTransport(std::shared_ptr<std::atomic<bool>> stop) : stop_(std::move(stop)) {}

    FetchResult toFile(const std::string& url, const std::string& path) override;
    FetchResult toString(const std::string& url, std::string& body, long& httpStatus) override;

  private:
    std::shared_ptr<std::atomic<bool>> stop_;
};

}  // namespace emu
