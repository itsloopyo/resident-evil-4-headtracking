// Tests for the pure numeric helpers in src/core/numeric_utils.h.
//
// These cover two real defects fixed during the security/reliability audit:
//   1. QpcTicksToMicros - the previous `ticks * 1000000 / freq` overflowed a
//      signed 64-bit multiply once the QueryPerformanceCounter value grew
//      large (~10 days of uptime at 10 MHz), wrapping the derived timestamp
//      negative and corrupting the camera-transform cache window.
//   2. NormalizeUdpPort - a raw INI port was cast straight to uint16_t, so an
//      out-of-range value like 70000 silently truncated to 4464 (a different,
//      live port) instead of falling back to the default.
//
// Hand-rolled runner in the same style as cameraunlock-core/cpp/tests so it
// builds with no extra dependencies.

#include "core/numeric_utils.h"

#include <cstdint>
#include <iostream>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

void TestQpcConversion() {
    std::cout << "QpcTicksToMicros:\n";

    // 10 MHz QPC, one second of ticks -> 1,000,000 us.
    Check(RE4HT::QpcTicksToMicros(10'000'000, 10'000'000) == 1'000'000,
          "one second at 10MHz == 1e6 us");

    // Sub-second remainder is preserved (0.5 s -> 500,000 us).
    Check(RE4HT::QpcTicksToMicros(5'000'000, 10'000'000) == 500'000,
          "half second at 10MHz == 5e5 us");

    // Zero ticks -> zero.
    Check(RE4HT::QpcTicksToMicros(0, 10'000'000) == 0, "zero ticks == 0 us");

    // Division-by-zero / invalid frequency is contained, not UB.
    Check(RE4HT::QpcTicksToMicros(123, 0) == 0, "freq 0 returns 0 (no UB)");

    // The regression case: a counter large enough that ticks*1000000 overflows
    // int64. 14 days at 10 MHz = 1.2096e13 ticks; *1e6 = 1.2096e19 > INT64_MAX.
    // Correct microseconds = 14 days = 1,209,600,000,000 us.
    const int64_t freq = 10'000'000;
    const int64_t fourteenDaysTicks = freq * 60ll * 60ll * 24ll * 14ll;
    const uint64_t expectedUs = 14ull * 24 * 60 * 60 * 1'000'000ull;
    Check(RE4HT::QpcTicksToMicros(fourteenDaysTicks, freq) == expectedUs,
          "14-day counter does not overflow (matches exact us)");

    // Monotonic across the old overflow boundary: a later tick must yield a
    // larger microsecond value (the buggy version went non-monotonic here).
    const uint64_t a = RE4HT::QpcTicksToMicros(fourteenDaysTicks, freq);
    const uint64_t b = RE4HT::QpcTicksToMicros(fourteenDaysTicks + freq, freq);
    Check(b > a && (b - a) == 1'000'000, "monotonic +1s past overflow boundary");
}

void TestNormalizeUdpPort() {
    std::cout << "NormalizeUdpPort:\n";

    bool valid = false;

    Check(RE4HT::NormalizeUdpPort(4242, 4242, valid) == 4242 && valid,
          "in-range default port accepted");

    Check(RE4HT::NormalizeUdpPort(1024, 4242, valid) == 1024 && valid,
          "lower bound 1024 accepted");
    Check(RE4HT::NormalizeUdpPort(65535, 4242, valid) == 65535 && valid,
          "upper bound 65535 accepted");

    // The truncation regression: 70000 must NOT become (uint16_t)70000 == 4464.
    Check(RE4HT::NormalizeUdpPort(70000, 4242, valid) == 4242 && !valid,
          "70000 falls back to default (not truncated to 4464)");

    Check(RE4HT::NormalizeUdpPort(1023, 4242, valid) == 4242 && !valid,
          "privileged/reserved 1023 falls back");
    Check(RE4HT::NormalizeUdpPort(0, 4242, valid) == 4242 && !valid,
          "port 0 falls back");
    Check(RE4HT::NormalizeUdpPort(-1, 4242, valid) == 4242 && !valid,
          "negative port falls back (not wrapped to 65535)");
}

}  // namespace

int main() {
    std::cout << "RE4HeadTracking numeric_utils tests\n";
    std::cout << "===================================\n";

    TestQpcConversion();
    TestNormalizeUdpPort();

    if (g_failures == 0) {
        std::cout << "All tests passed!\n";
        return 0;
    }
    std::cout << g_failures << " test(s) FAILED\n";
    return 1;
}
