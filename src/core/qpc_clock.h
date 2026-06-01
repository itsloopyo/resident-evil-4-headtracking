#pragma once

#include <windows.h>
#include <cstdint>

#include "numeric_utils.h"

namespace RE4HT {

// Current QueryPerformanceCounter value in microseconds, using the
// overflow-safe split conversion (see numeric_utils.h).
inline uint64_t QpcNowMicros() {
    static LARGE_INTEGER freq = {};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return QpcTicksToMicros(now.QuadPart, freq.QuadPart);
}

} // namespace RE4HT
