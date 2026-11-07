#include "emu/NetGate.hpp"

#include <atomic>

namespace emu {

namespace {
std::atomic<bool> busy{false};
}

bool netTryAcquire() { return !busy.exchange(true); }
void netRelease() { busy.store(false); }

}  // namespace emu
