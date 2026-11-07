#include "emu/PlayedAgo.hpp"

#include <ctime>

namespace emu {

namespace {

bool localDay(int64_t when, std::tm& out) {
    const std::time_t t = static_cast<std::time_t>(when);
#if defined(_WIN32)
    return localtime_s(&out, &t) == 0;
#else
    return localtime_r(&t, &out) != nullptr;
#endif
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

int64_t dayNumber(const std::tm& tm) {
    return daysFromCivil(tm.tm_year + 1900, static_cast<unsigned>(tm.tm_mon + 1),
                         static_cast<unsigned>(tm.tm_mday));
}

}  // namespace

int daysSincePlayed(int64_t played, int64_t now) {
    if (played <= 0) return -1;
    std::tm a{}, b{};
    if (!localDay(played, a) || !localDay(now, b)) return 0;
    const int64_t days = dayNumber(b) - dayNumber(a);
    return days < 0 ? 0 : static_cast<int>(days);
}

}  // namespace emu
