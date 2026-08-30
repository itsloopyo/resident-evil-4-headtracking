// Characterization tests for the matrix/quaternion primitives this plugin
// consumes from cameraunlock-core's header-only re_math.h.
//
// The plugin no longer carries a local copy, nor a using-alias shim over the
// core header. These tests lock the observable behaviour of the two symbols the
// GUI compensation depends on - MatrixToQuat and ComputeCleanToHeadRotation -
// so a change to the shared math shows up here rather than as drifted markers
// in game.
//
// Hand-rolled runner in the same style as cameraunlock-core/cpp/tests - no
// extra dependencies.

#include <cameraunlock/reframework/re_math.h>

#include <cmath>
#include <iostream>

namespace ref = cameraunlock::reframework;

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

ref::Matrix4x4f Identity() {
    ref::Matrix4x4f m{};
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
    return m;
}

void TestMatrixToQuat() {
    std::cout << "MatrixToQuat:\n";

    ref::REQuat q = ref::MatrixToQuat(Identity());
    Check(ApproxEq(q.x, 0.f) && ApproxEq(q.y, 0.f) && ApproxEq(q.z, 0.f) && ApproxEq(q.w, 1.f),
          "identity matrix -> identity quaternion (0,0,0,1)");

    // 90 deg rotation about the Z axis (RE Engine stores basis axes in rows).
    ref::Matrix4x4f rz = Identity();
    rz.m[0][0] = 0.f; rz.m[0][1] = 1.f;
    rz.m[1][0] = -1.f; rz.m[1][1] = 0.f;
    ref::REQuat qz = ref::MatrixToQuat(rz);
    const float kHalfSqrt2 = 0.70710678f;
    Check(ApproxEq(qz.x, 0.f) && ApproxEq(qz.y, 0.f) &&
          ApproxEq(std::fabs(qz.z), kHalfSqrt2) && ApproxEq(std::fabs(qz.w), kHalfSqrt2),
          "90deg-about-Z -> (0,0,+-0.707,+-0.707)");
}

void TestComputeCleanToHeadRotation() {
    std::cout << "ComputeCleanToHeadRotation:\n";

    ref::Matrix4x4f I = Identity();
    float c[3][3] = {};
    ref::ComputeCleanToHeadRotation(I, I, c);
    bool isIdentity =
        ApproxEq(c[0][0], 1.f) && ApproxEq(c[1][1], 1.f) && ApproxEq(c[2][2], 1.f) &&
        ApproxEq(c[0][1], 0.f) && ApproxEq(c[0][2], 0.f) &&
        ApproxEq(c[1][0], 0.f) && ApproxEq(c[1][2], 0.f) &&
        ApproxEq(c[2][0], 0.f) && ApproxEq(c[2][1], 0.f);
    Check(isIdentity, "clean == head -> identity rotation");

    // head row i . clean row j. With head 90deg-about-Z relative to clean
    // (identity), the result equals the head basis itself.
    ref::Matrix4x4f head = Identity();
    head.m[0][0] = 0.f; head.m[0][1] = 1.f;
    head.m[1][0] = -1.f; head.m[1][1] = 0.f;
    float c2[3][3] = {};
    ref::ComputeCleanToHeadRotation(I, head, c2);
    Check(ApproxEq(c2[0][0], 0.f) && ApproxEq(c2[0][1], 1.f) &&
          ApproxEq(c2[1][0], -1.f) && ApproxEq(c2[1][1], 0.f) &&
          ApproxEq(c2[2][2], 1.f),
          "clean identity, head 90deg-about-Z -> head basis in rows");
}

}  // namespace

int main() {
    std::cout << "RE4HeadTracking camera math tests\n";
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
