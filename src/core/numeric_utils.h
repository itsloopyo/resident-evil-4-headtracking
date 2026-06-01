#pragma once

#include <cstdint>

namespace RE4HT {

// Convert QueryPerformanceCounter ticks to microseconds without the signed
// int64 overflow that `ticks * 1000000 / freq` suffers once the counter grows
// large. A 10 MHz QPC overflows a 64-bit multiply after ~10 days of uptime
// (and QPC keeps counting across sleep/hibernate), at which point the product
// wraps negative and the derived timestamp becomes garbage. Splitting into a
// whole-second part plus a sub-second remainder keeps every intermediate
// within int64 range for any realistic QPC frequency.
inline uint64_t QpcTicksToMicros(int64_t ticks, int64_t freq) {
    if (freq <= 0) return 0;
    const int64_t secs = ticks / freq;
    const int64_t rem = ticks % freq;
    return static_cast<uint64_t>(secs) * 1000000ULL +
           static_cast<uint64_t>((rem * 1000000) / freq);
}

// Clamp a raw INI-parsed UDP port to a usable value. OpenTrack uses the
// 1024-65535 range; values outside it (including the negative and >65535
// values an atoi-style parse yields, which silently truncate to a wrong
// 16-bit port when cast straight to uint16_t - e.g. 70000 -> 4464) fall back
// to the default. `valid` reports whether the raw value was in range.
inline uint16_t NormalizeUdpPort(int raw, uint16_t fallback, bool& valid) {
    valid = (raw >= 1024 && raw <= 65535);
    return valid ? static_cast<uint16_t>(raw) : fallback;
}

} // namespace RE4HT
