#include "pch.h"
#include "gui_compensation.h"
#include "game_state_detector.h"

#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/reframework/camera_pipeline.h>
#include <cameraunlock/reframework/gui_elements.h>
#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/reframework/plugin_mod.h>
#include <cameraunlock/reframework/re_math.h>

#include <reframework/API.hpp>

#include <cmath>
#include <cstring>

namespace RE4HT {

namespace ref = cameraunlock::reframework;

// A descendant PlayObject count above this marks RE4's full-screen HUD layer,
// which takes the view-tree walk rather than the small-element fast path.
constexpr uint32_t kLargeHudDescendantThreshold = 100;

// Upper bound on HUD child elements offset per frame, capping per-frame
// reflection cost on the large-HUD path.
constexpr uint32_t kMaxCompensatedHudChildren = 64;

// Method*-slot caches for the per-element, per-frame GUI invokes. Resolved once
// by ref::InvokeCached; getters and array accessors are inherited, so the same
// Method* serves every instance.
static struct {
    reframework::API::Method* arrGetLength = nullptr;
    reframework::API::Method* arrGetValue = nullptr;
    reframework::API::Method* getChildren = nullptr;
} g_hotMethods;

// Gui_FloatIcon carries the world markers as a single icon stack offset from
// screen centre, not at it - so a uniform centre-assumed shift makes off-centre
// markers fly. via.gui get_GlobalPosition is screen-centre-relative (origin =
// centre), so we read the stack's real screen anchor and reproject it through
// the clean-to-head rotation: the layer shift glues the anchor's world direction
// to where it lands in the head-tracked view, correct at any screen offset, and
// collapses to zero when the head is centred.
//
// Translation parallax is not corrected. The post-render callback restores the
// clean camera in full, position row included, so the GUI draws its anchors from
// the un-leaned eye while the frame was drawn from the leaned one, and the
// leftover is lean/depth. Fixing it needs each icon's depth; this reads a canvas
// position, and the write moves the whole stack at once, so no single value
// would be right for more than one icon in it.
static void ApplyMarkerCompensation(reframework::API::ManagedObject* guiMo) {
    const auto& gui = ref::GetGuiMethods();
    if (!gui.ready || !gui.getGlobalPosition) return;

    const auto& projection = ref::GetFrameProjection();
    if (!projection.cleanToHeadValid || !projection.aimValid) return;
    if (!ref::PluginMod::Instance().IsEnabled() || !IsInGameplay()) return;

    float fx = 0.f, fy = 0.f;
    if (!ref::GetMarkerFocalLengths(fx, fy)) return;

    auto view = ref::GetElementView(guiMo);
    if (!view) return;

    // Zero the layer so the children report their natural, un-shifted positions.
    ref::SetTransformPosition(view, 0.f, 0.f);

    uint32_t n = 0;
    auto arr = ref::FindPlayObjects(guiMo, n);
    if (!arr) return;

    // First active icon (index 0 is the root container at the origin).
    float anchorX = 0.f, anchorY = 0.f;
    bool found = false;
    for (uint32_t i = 1; i < n && i < 16; i++) {
        std::vector<void*> ia = { (void*)(uintptr_t)i };
        auto er = ref::InvokeCached(arr, g_hotMethods.arrGetValue, "GetValue", ia);
        if (er.exception_thrown || !er.ptr) continue;
        auto node = reinterpret_cast<reframework::API::ManagedObject*>(er.ptr);
        float gx = 0.f, gy = 0.f;
        if (!ref::GetTransformGlobalPosition(node, gx, gy)) continue;
        if (gx == gx && gy == gy && (gx != 0.f || gy != 0.f)
            && fabsf(gx) <= 4000.f && fabsf(gy) <= 4000.f) {
            anchorX = gx; anchorY = gy; found = true; break;
        }
    }
    if (!found) return;  // no active marker; layer left zeroed

    // get_GlobalPosition is top-left origin, so centre the anchor before
    // unprojecting and add the centre back after. cleanToHead already carries
    // head roll (it is R_head * R_clean^T), so reproject with roll = 0 - passing
    // roll again would double-count it. Round-trips to the anchor when the head
    // is centred.
    float tcr = -(anchorX - ref::kHalfReferenceCanvasWidth)  / fx;
    float tcu =  (anchorY - ref::kHalfReferenceCanvasHeight) / fy;
    float guiX = 0.f, guiY = 0.f;
    if (!ref::ProjectCleanRayToHeadGui(projection.cleanToHead, 0.f, tcr, tcu, 1.f, fx, fy, guiX, guiY)) {
        return;
    }

    float deltaX = (ref::kHalfReferenceCanvasWidth  + guiX) - anchorX;
    float deltaY = (ref::kHalfReferenceCanvasHeight + guiY) - anchorY;

    static cameraunlock::math::SmoothedFloat s_dX, s_dY;
    float dt = ref::PluginMod::Instance().GetLastDeltaTime();
    deltaX = s_dX.Update(deltaX, ref::kProjectionSmoothing, dt);
    deltaY = s_dY.Update(deltaY, ref::kProjectionSmoothing, dt);

    ref::SetTransformPosition(view, deltaX, deltaY);
}

static void ApplyCrosshairOffset(reframework::API::ManagedObject* guiMo) {
    const auto& gui = ref::GetGuiMethods();
    if (!gui.ready) return;

    const auto& projection = ref::GetFrameProjection();
    if (!projection.aimValid || !ref::PluginMod::Instance().IsEnabled() || !IsInGameplay()) return;

    float fx = 0.f, fy = 0.f;
    if (!ref::GetMarkerFocalLengths(fx, fy)) return;
    float deltaX = -projection.aimTanRight * fx;
    float deltaY =  projection.aimTanUp * fy;

    // Enumerate the descendant PlayObjects once. The small-HUD branch below
    // reuses this array rather than re-running findObjects (a full descendant
    // walk that allocates a managed array) a second time in the same frame.
    uint32_t descendantCount = 0;
    auto descendants = ref::FindPlayObjects(guiMo, descendantCount);

    if (descendantCount > kLargeHudDescendantThreshold) {
        auto view = ref::GetElementView(guiMo);
        if (!view) return;
        auto childrenRet = ref::InvokeCached(view, g_hotMethods.getChildren, "getChildren", ref::EmptyArgs());
        if (childrenRet.exception_thrown || !childrenRet.ptr) return;
        auto childArr = reinterpret_cast<reframework::API::ManagedObject*>(childrenRet.ptr);
        auto lenRet = ref::InvokeCached(childArr, g_hotMethods.arrGetLength, "get_Length", ref::EmptyArgs());
        uint32_t count = lenRet.exception_thrown ? 0 : lenRet.dword;
        uint32_t cap = count < kMaxCompensatedHudChildren ? count : kMaxCompensatedHudChildren;

        // GetValue routes through the string-keyed ManagedObject overload
        // (vector only); reuse one buffer across iterations instead of
        // allocating per index.
        static std::vector<void*> idxArgs(1);

        float absRoll = fabsf(projection.rollDegrees);
        bool applyRoll = (absRoll > 0.1f) && gui.getGlobalPosition;
        float cosR = 1.f, sinR = 0.f;
        if (applyRoll) {
            float rollRad = projection.rollDegrees * ref::kDegToRad;
            cosR = cosf(rollRad);
            sinR = sinf(rollRad);
        }

        for (uint32_t i = 0; i < cap; i++) {
            idxArgs[0] = (void*)(uintptr_t)i;
            auto elemRet = ref::InvokeCached(childArr, g_hotMethods.arrGetValue, "GetValue", idxArgs);
            if (elemRet.exception_thrown || !elemRet.ptr) continue;
            auto elem = reinterpret_cast<reframework::API::ManagedObject*>(elemRet.ptr);

            if (!applyRoll) {
                ref::SetTransformPosition(elem, deltaX, deltaY);
                continue;
            }

            // Rotate each child about the canvas origin by head roll, then add
            // the shared yaw/pitch shift. Zeroing first makes the global read
            // report the child's natural position rather than the previous
            // frame's correction.
            ref::SetTransformPosition(elem, 0.f, 0.f);
            float gx = 0.f, gy = 0.f;
            if (!ref::GetTransformGlobalPosition(elem, gx, gy)) continue;
            float rotX = gx * cosR - gy * sinR;
            float rotY = gx * sinR + gy * cosR;
            ref::SetTransformPosition(elem, (rotX - gx) + deltaX, (rotY - gy) + deltaY);
        }
        return;
    }

    if (!descendants || descendantCount < 2) return;
    static std::vector<void*> idxArgs = { (void*)(uintptr_t)1 };
    auto elemRet = ref::InvokeCached(descendants, g_hotMethods.arrGetValue, "GetValue", idxArgs);
    if (elemRet.exception_thrown || !elemRet.ptr) return;
    ref::SetTransformPosition(
        reinterpret_cast<reframework::API::ManagedObject*>(elemRet.ptr), deltaX, deltaY);
}

bool OnPreGuiDrawElement(void* element, void* context) {
    (void)context;
    if (!ref::PluginMod::Instance().IsEnabled()) return true;
    if (!element) return true;

    auto mo = reinterpret_cast<reframework::API::ManagedObject*>(element);

    char goName[128] = {};
    if (!ref::ReadGuiElementName(mo, goName, sizeof(goName))) return true;
    ref::LogGuiElementNameOnce(goName);

    // Gui_FloatIcon is RE4's world-anchored floating-marker layer.
    if (strcmp(goName, "Gui_FloatIcon") == 0) ApplyMarkerCompensation(mo);

    if (strncmp(goName, "Gui_ui20", 8) == 0) ApplyCrosshairOffset(mo);

    return true;
}

} // namespace RE4HT
