// Timing and sanity notes for the game library, for testing on the console.
// Goes to nxlink and to sdmc:/equinox/library.log, which survives the restart
// after a game and keeps the last lines before a crash.
#pragma once

#include <borealis/core/logger.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>

namespace emu::liblog {

constexpr const char* kFile = "sdmc:/equinox/library.log";

int64_t nowUs();
// Stamped with the seconds since the app started.
void write(const std::string& line);
// Writes what is pending to the card, off the UI thread.
void flushSoon();
bool bugAllowed();
// RAM this process uses, in MB. 0 where unknown.
int usedMemoryMb();

template <typename... Args>
void note(fmt::format_string<Args...> format, Args&&... args) {
    write(fmt::format(format, std::forward<Args>(args)...));
}

// Something that should never happen. Capped, so one broken state repeated
// every frame cannot flood the log or slow the app down.
template <typename... Args>
void bug(fmt::format_string<Args...> format, Args&&... args) {
    if (!bugAllowed())
        return;
    write("BUG " + fmt::format(format, std::forward<Args>(args)...));
    flushSoon();
}

// Touched from the decode workers and the UI thread; read once per summary.
struct CoverStats {
    std::atomic<int> asked{0}, cacheHits{0}, decoded{0}, dropped{0};
    std::atomic<int64_t> decodeTotalUs{0}, decodeMaxUs{0}, uploadMaxUs{0};
};
extern CoverStats covers;

inline void raiseMax(std::atomic<int64_t>& slot, int64_t value) {
    int64_t seen = slot.load();
    while (value > seen && !slot.compare_exchange_weak(seen, value)) {
    }
}

}  // namespace emu::liblog
