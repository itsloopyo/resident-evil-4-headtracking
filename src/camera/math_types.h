#pragma once

#include <cameraunlock/reframework/re_math.h>

// The matrix/quaternion primitives this plugin needs already live in
// cameraunlock-core (header-only). Re-export the ones we use under the local
// names rather than maintaining a second copy.
namespace RE4HT {

using cameraunlock::reframework::Matrix4x4f;
using cameraunlock::reframework::REQuat;
using cameraunlock::reframework::MatrixToQuat;
using cameraunlock::reframework::ComputeCleanToHeadRotation;

constexpr float DEG_TO_RAD = cameraunlock::reframework::kDegToRad;

} // namespace RE4HT
