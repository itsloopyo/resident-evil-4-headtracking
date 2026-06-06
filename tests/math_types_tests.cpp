// Characterization tests for the matrix/quaternion primitives this plugin
// consumes through src/camera/math_types.h.
//
// math_types.h was de-duplicated to re-export cameraunlock-core's header-only
// re_math.h instead of carrying a second copy. These tests lock the observable
// behaviour of the symbols the plugin actually uses (RE4HT::MatrixToQuat and
// RE4HT::ComputeCleanToHeadRotation) so the consolidation can never silently
// rewire to the wrong implementation.
//
// Hand-rolled runner in the same style as cameraunlock-core/cpp/tests - no
// extra dependencies.

#include "camera/math_types.h"

#include <cmath>
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

bool ApproxEq(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

RE4HT::Matrix4x4f Identity() {
    RE4HT::Matrix4x4f m{};
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
    return m;
}

void TestMatrixToQuat() {
    std::cout << "MatrixToQuat:\n";

    RE4HT::REQuat q = RE4HT::MatrixToQuat(Identity());
    Check(ApproxEq(q.x, 0.f) && ApproxEq(q.y, 0.f) && ApproxEq(q.z, 0.f) && ApproxEq(q.w, 1.f),
          "identity matrix -> identity quaternion (0,0,0,1)");

    // 90 deg rotation about the Z axis (RE Engine stores basis axes in rows).
    RE4HT::Matrix4x4f rz = Identity();
    rz.m[0][0] = 0.f; rz.m[0][1] = 1.f;
    rz.m[1][0] = -1.f; rz.m[1][1] = 0.f;
    RE4HT::REQuat qz = RE4HT::MatrixToQuat(rz);
    const float kHalfSqrt2 = 0.70710678f;
    Check(ApproxEq(qz.x, 0.f) && ApproxEq(qz.y, 0.f) &&
          ApproxEq(std::fabs(qz.z), kHalfSqrt2) && ApproxEq(std::fabs(qz.w), kHalfSqrt2),
          "90deg-about-Z -> (0,0,+-0.707,+-0.707)");
}

void TestComputeCleanToHeadRotation() {
    std::cout << "ComputeCleanToHeadRotation:\n";

    RE4HT::Matrix4x4f I = Identity();
    float c[3][3] = {};
    RE4HT::ComputeCleanToHeadRotation(I, I, c);
    bool isIdentity =
        ApproxEq(c[0][0], 1.f) && ApproxEq(c[1][1], 1.f) && ApproxEq(c[2][2], 1.f) &&
        ApproxEq(c[0][1], 0.f) && ApproxEq(c[0][2], 0.f) &&
        ApproxEq(c[1][0], 0.f) && ApproxEq(c[1][2], 0.f) &&
        ApproxEq(c[2][0], 0.f) && ApproxEq(c[2][1], 0.f);
    Check(isIdentity, "clean == head -> identity rotation");

    // head row i . clean row j. With head 90deg-about-Z relative to clean
    // (identity), the result equals the head basis itself.
    RE4HT::Matrix4x4f head = Identity();
    head.m[0][0] = 0.f; head.m[0][1] = 1.f;
    head.m[1][0] = -1.f; head.m[1][1] = 0.f;
    float c2[3][3] = {};
    RE4HT::ComputeCleanToHeadRotation(I, head, c2);
    Check(ApproxEq(c2[0][0], 0.f) && ApproxEq(c2[0][1], 1.f) &&
          ApproxEq(c2[1][0], -1.f) && ApproxEq(c2[1][1], 0.f) &&
          ApproxEq(c2[2][2], 1.f),
          "clean identity, head 90deg-about-Z -> head basis in rows");
}

}  // namespace

int main() {
    std::cout << "RE4HeadTracking math_types tests\n";
    std::cout << "================================\n";

    TestMatrixToQuat();
    TestComputeCleanToHeadRotation();

    if (g_failures == 0) {
        std::cout << "All tests passed!\n";
        return 0;
    }
    std::cout << g_failures << " test(s) FAILED\n";
    return 1;
}
