#include "emu/LibraryLog.hpp"

#include <borealis/core/thread.hpp>

#if defined(PLATFORM_SWITCH)
#include <switch.h>
#endif

#include <sys/stat.h>

#include <chrono>
#include <cstdio>
#include <mutex>

namespace emu::liblog {

namespace {
constexpr size_t kMaxPending = 256 * 1024;
constexpr long kMaxFileBytes = 4 * 1024 * 1024;
constexpr int kMaxBugs = 30;

std::mutex pendingLock;
std::string pending;
std::atomic<bool> flushing{false};
std::atomic<int> bugs{0};
bool checkedSize = false; // only the flush that holds `flushing` touches it

int64_t startUs() {
    static const int64_t start = nowUs();
    return start;
}
}  // namespace

CoverStats covers;

int64_t nowUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

void write(const std::string& line) {
    brls::Logger::info("LIB {}", line);
    const std::string stamped =
        fmt::format("[{:.3f}] {}\n", (nowUs() - startUs()) / 1e6, line);
    std::lock_guard<std::mutex> guard(pendingLock);
    if (pending.size() + stamped.size() <= kMaxPending)
        pending += stamped;
}

void flushSoon() {
    if (flushing.exchange(true))
        return;
    brls::async([] {
        std::string out;
        {
            std::lock_guard<std::mutex> guard(pendingLock);
            out.swap(pending);
        }
        if (!checkedSize) {
            checkedSize = true;
            struct stat info {};
            if (stat(kFile, &info) == 0 && info.st_size > kMaxFileBytes)
                std::remove(kFile);
        }
        if (!out.empty()) {
            if (std::FILE* file = std::fopen(kFile, "a")) {
                std::fwrite(out.data(), 1, out.size(), file);
                std::fclose(file);
            }
        }
        flushing = false;
    });
}

bool bugAllowed() {
    return bugs.fetch_add(1) < kMaxBugs;
}

int usedMemoryMb() {
#if defined(PLATFORM_SWITCH)
    u64 used = 0;
    if (R_SUCCEEDED(svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0)))
        return static_cast<int>(used / (1024 * 1024));
#endif
    return 0;
}

}  // namespace emu::liblog
