#include "pch.h"
#include "camera_hook.h"
#include "math_types.h"
#include "game_state_detector.h"
#include "core/mod.h"
#include "core/logger.h"

#include <reframework/API.hpp>
#include <cameraunlock/math/smoothing_utils.h>
#include <cameraunlock/reframework/camera_chain.h>
#include <cameraunlock/reframework/camera_controller_hook.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/rendering/gui_marker_compensation.h>
#include <unordered_set>
#include <string>
#include <span>

namespace RE4HT {

namespace ref = cameraunlock::reframework;

// Distinct GUI element names logged for discovery, bounded so a busy HUD cannot
// flood the log.
constexpr size_t kMaxLoggedGuiNames = 100;

// A descendant PlayObject count above this marks RE4's full-screen HUD layer,
// which takes the view-tree walk rather than the small-element fast path.
constexpr uint32_t kLargeHudDescendantThreshold = 100;

// Upper bound on HUD child elements offset per frame, capping per-frame
// reflection cost on the large-HUD path.
constexpr uint32_t kMaxCompensatedHudChildren = 64;

// Half the 1920x1080 reference canvas the GUI compensation projects against.
// Focal lengths are expressed in pixels of that canvas.
constexpr float kHalfReferenceCanvasWidth  = 960.f;
constexpr float kHalfReferenceCanvasHeight = 540.f;

static CrosshairProjection g_crosshair;

// Clean-to-head rotation matrix (3x3).
static float g_C[3][3] = {};
static bool g_C_valid = false;

// Head-to-clean position delta in clean-camera-local space.
static float g_posClean[3] = {};

static struct {
    Matrix4x4f gameMatrix;
    bool hasGameMatrix = false;
} g_saved;

static struct {
    Matrix4x4f matrix;
    bool valid = false;
} g_cleanCameraMatrix;

static bool g_trackingAppliedThisFrame = false;

static ref::CameraTransformResolver g_cameraResolver;

// Per-frame camera/transform cache. Invalidated together at the
// camera-controller update pre-hook and at the end of OnPostBeginRendering,
// so within a single render frame they hold the live primary camera and its
// transform without re-walking the SceneManager chain.
static void* g_cachedTransform = nullptr;
static void* g_cachedCamera = nullptr;

static void* GetCameraTransformCached() {
    if (g_cachedTransform) return g_cachedTransform;
    g_cachedTransform = g_cameraResolver.ResolveTransform(&g_cachedCamera);
    return g_cachedTransform;
}

static struct {
    reframework::API::Method* guiFindObjectsByType = nullptr;
    reframework::API::Method* transformSetPosition = nullptr;
    reframework::API::Method* transformGetGlobalPosition = nullptr;
    reframework::API::Method* getProjectionMatrix = nullptr;  // via.Camera.get_ProjectionMatrix
    reframework::API::TypeDefinition* playObjectRuntimeType = nullptr;
    bool resolved = false;
    bool giveUp = false;
} g_guiCam;

// Method*-slot caches for the per-element, per-frame GUI invokes. Resolved once
// by ref::InvokeCached; getters/array accessors are inherited so the same
// Method* serves every instance.
static struct {
    reframework::API::Method* getGameObject = nullptr;
    reframework::API::Method* getName = nullptr;
    reframework::API::Method* arrGetLength = nullptr;
    reframework::API::Method* arrGetValue = nullptr;
    reframework::API::Method* getView = nullptr;
    reframework::API::Method* getChildren = nullptr;
} g_hotMethods;

static void ApplyHeadTracking(Matrix4x4f* worldMat) {
    float yaw, pitch, roll;
    if (!Mod::Instance().GetProcessedRotation(yaw, pitch, roll)) return;

    Matrix4x4f preRotationAxes = *worldMat;

    float yr = -yaw * DEG_TO_RAD;
    float pr = pitch * DEG_TO_RAD;
    float rr = roll * DEG_TO_RAD;

    if (Mod::Instance().IsWorldSpaceYaw()) {
        ApplyWorldSpaceHeadRotation(*worldMat, yr, pr, rr);
    } else {
        ApplyCameraLocalHeadRotation(*worldMat, yr, pr, rr);
    }

    // 6DOF position
    float px, py, pz;
    if (Mod::Instance().GetPositionOffset(px, py, pz)) {
        ApplyViewSpacePositionOffset(*worldMat, preRotationAxes, px, py, pz);
    }
}

// --- Camera controller hooks ---
static int CameraUpdatePreHook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    g_cachedTransform = nullptr;
    g_cachedCamera = nullptr;

    if (!g_saved.hasGameMatrix || !Mod::Instance().IsEnabled()) return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    void* transform = GetCameraTransformCached();
    if (!transform) return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);
    // RE Engine transform pointers can go stale across scene transitions; guard
    // the raw write so a torn-down camera never crashes the game.
    __try {
        *worldMat = g_saved.gameMatrix;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

static void CameraUpdatePostHook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    void* transform = GetCameraTransformCached();
    if (!transform) return;
    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);
    __try {
        g_saved.gameMatrix = *worldMat;
        g_saved.hasGameMatrix = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    static bool s_loggedOnce = false;
    if (!s_loggedOnce) {
        REQuat q = MatrixToQuat(g_saved.gameMatrix);
        Logger::Instance().Info("Hook save/restore active: gameQ=%.3f %.3f %.3f %.3f", q.x, q.y, q.z, q.w);
        s_loggedOnce = true;
    }
}

// --- Camera controller discovery ---

// RE4 Remake's game code lives under the `chainsaw` root namespace.
// Namespace changes and unknown controllers fall back to the hooker's TDB
// short-name scan and parent-chain walk.
static const char* const kControllerTypeCandidates[] = {
    "chainsaw.PlayerCameraController",
    "chainsaw.CameraManager",
};

static ref::CameraControllerHooker g_controllerHooker{
    kControllerTypeCandidates,
    static_cast<int>(std::size(kControllerTypeCandidates)),
    CameraUpdatePreHook,
    CameraUpdatePostHook};

// Retry camera-controller discovery each gameplay frame until it succeeds.
// The candidate fast path normally hooks at plugin init (see
// InitCachedFunctions); this adds the parent-chain walk, which needs a live
// gameplay camera rig to inspect.
static void EnsureCameraControllerHooked() {
    if (g_controllerHooker.IsHooked()) return;
    if (g_controllerHooker.TryHook(GetCameraTransformCached())) return;

    int attempts = g_controllerHooker.AttemptCount();
    if (attempts == 1 || (attempts % 300) == 0) {
        Logger::Instance().Warning(
            "Camera controller hook not yet found (attempt %d) - head tracking "
            "still active via the BeginRendering restore path", attempts);
    }
}

// --- GUI compensation ---

// Pixel focal lengths from the camera's projection matrix (P00 = 1/tan(hFovX/2),
// P11 = 1/tan(hFovY/2) in NDC), scaled by the half-canvas. Exact - no horizontal-
// vs-vertical FOV-convention guessing, which is what the get_FOV path got wrong
// (it produced fx=fy=558 against a 1920x1080 canvas that wants ~994).
static bool GetFocalLengthsFromProjectionMatrix(float& fx, float& fy) {
    if (!g_guiCam.getProjectionMatrix) return false;
    void* cam = g_cachedCamera ? g_cachedCamera : g_cameraResolver.ResolveCamera();
    if (!cam) return false;

    auto ret = g_guiCam.getProjectionMatrix->invoke(
        reinterpret_cast<reframework::API::ManagedObject*>(cam), ref::EmptyArgs());
    if (ret.exception_thrown) return false;

    auto* m = reinterpret_cast<const float*>(ret.bytes.data());
    return cameraunlock::rendering::FocalLengthsFromProjection(
        m[0], m[5], kHalfReferenceCanvasWidth, kHalfReferenceCanvasHeight, fx, fy);
}

static bool GetMarkerProjectionFocalLengths(float& fx, float& fy) {
    if (GetFocalLengthsFromProjectionMatrix(fx, fy)) {
        // Square pixels: a divergent P00 (seen at half its true value on a sibling
        // build, fx ending up half of fy) would under-compensate yaw. fy (vertical)
        // is the trusted value; enforce fx = fy so a bad matrix can't slip through.
        fx = fy;
        return true;
    }

    float fov = g_crosshair.valid ? g_crosshair.fovDegrees
                                  : g_cameraResolver.ResolveFovDegrees(g_cachedCamera);
    return cameraunlock::rendering::FocalLengthsFromVerticalFov(
        fov, kHalfReferenceCanvasWidth, kHalfReferenceCanvasHeight, fx, fy);
}

// Gui_FloatIcon carries the world markers as a single icon stack offset from
// screen centre, not at it - so a uniform centre-assumed shift makes off-centre
// markers fly. via.gui get_GlobalPosition is screen-centre-relative (origin =
// centre), so we read the stack's real screen anchor and reproject it through
// the clean-to-head rotation: the layer shift glues the anchor's world direction
// to where it lands in the head-tracked view, correct at any screen offset, and
// collapses to zero when the head is centred. Translation parallax is left to
// the engine (the GUI draws with the head position already).
static void ApplyMarkerCompensation(reframework::API::ManagedObject* guiMo) {
    if (!guiMo || !g_guiCam.transformSetPosition || !g_guiCam.transformGetGlobalPosition
        || !g_guiCam.guiFindObjectsByType || !g_guiCam.playObjectRuntimeType) return;
    if (!g_C_valid || !g_crosshair.valid || !Mod::Instance().IsEnabled() || !IsInGameplay()) return;

    float fx = 0.f, fy = 0.f;
    if (!GetMarkerProjectionFocalLengths(fx, fy)) return;

    auto viewRet = ref::InvokeCached(guiMo, g_hotMethods.getView, "get_View", ref::EmptyArgs());
    if (viewRet.exception_thrown || !viewRet.ptr) return;
    auto view = reinterpret_cast<reframework::API::ManagedObject*>(viewRet.ptr);

    // Zero the layer so the children report their natural, un-shifted positions.
    float zero[3] = { 0.f, 0.f, 0.f };
    void* zeroArgs[1] = { (void*)&zero[0] };
    g_guiCam.transformSetPosition->invoke(view, std::span<void*>(zeroArgs));

    void* findArgs[1] = { (void*)g_guiCam.playObjectRuntimeType };
    auto arrRet = g_guiCam.guiFindObjectsByType->invoke(guiMo, std::span<void*>(findArgs));
    if (arrRet.exception_thrown || !arrRet.ptr) return;
    auto arr = reinterpret_cast<reframework::API::ManagedObject*>(arrRet.ptr);
    auto lenRet = ref::InvokeCached(arr, g_hotMethods.arrGetLength, "get_Length", ref::EmptyArgs());
    if (lenRet.exception_thrown) return;
    uint32_t n = lenRet.dword;

    // First active icon (index 0 is the root container at the origin).
    float anchorX = 0.f, anchorY = 0.f;
    bool found = false;
    for (uint32_t i = 1; i < n && i < 16; i++) {
        std::vector<void*> ia = { (void*)(uintptr_t)i };
        auto er = ref::InvokeCached(arr, g_hotMethods.arrGetValue, "GetValue", ia);
        if (er.exception_thrown || !er.ptr) continue;
        auto node = reinterpret_cast<reframework::API::ManagedObject*>(er.ptr);
        auto gp = g_guiCam.transformGetGlobalPosition->invoke(node, ref::EmptyArgs());
        if (gp.exception_thrown) continue;
        float gx = *reinterpret_cast<float*>(&gp.bytes[0]);
        float gy = *reinterpret_cast<float*>(&gp.bytes[4]);
        if (gx == gx && gy == gy && (gx != 0.f || gy != 0.f)
            && fabsf(gx) <= 4000.f && fabsf(gy) <= 4000.f) {
            anchorX = gx; anchorY = gy; found = true; break;
        }
    }
    if (!found) return;  // no active marker; layer left zeroed

    // get_GlobalPosition is top-left origin, so centre the anchor before
    // unprojecting and add the centre back after. g_C already carries head roll
    // (it is R_head * R_clean^T), so reproject with roll = 0 - passing roll again
    // would double-count it. Round-trips to the anchor when g_C = I (head centred).
    float tcr = -(anchorX - kHalfReferenceCanvasWidth)  / fx;
    float tcu =  (anchorY - kHalfReferenceCanvasHeight) / fy;
    float guiX = 0.f, guiY = 0.f;
    if (!ProjectCleanRayToHeadGui(g_C, 0.f, tcr, tcu, 1.f, fx, fy, guiX, guiY)) return;

    float deltaX = (kHalfReferenceCanvasWidth  + guiX) - anchorX;
    float deltaY = (kHalfReferenceCanvasHeight + guiY) - anchorY;

    static cameraunlock::math::SmoothedFloat s_dX, s_dY;
    constexpr float kSmoothing = static_cast<float>(cameraunlock::math::kBaselineSmoothing);
    float dt = Mod::Instance().GetLastDeltaTime();
    deltaX = s_dX.Update(deltaX, kSmoothing, dt);
    deltaY = s_dY.Update(deltaY, kSmoothing, dt);

    float pos[3] = { deltaX, deltaY, 0.f };
    void* setArgs[1] = { (void*)&pos[0] };
    g_guiCam.transformSetPosition->invoke(view, std::span<void*>(setArgs));
}

static void ApplyCrosshairOffset(reframework::API::ManagedObject* guiMo) {
    if (!guiMo || !g_guiCam.guiFindObjectsByType || !g_guiCam.playObjectRuntimeType
        || !g_guiCam.transformSetPosition) return;
    if (!g_crosshair.valid || !Mod::Instance().IsEnabled() || !IsInGameplay()) return;

    float fx = 0.f, fy = 0.f;
    if (!GetMarkerProjectionFocalLengths(fx, fy)) return;
    float deltaX = -g_crosshair.tanRight * fx;
    float deltaY =  g_crosshair.tanUp * fy;

    auto guiMoPtr = reinterpret_cast<reframework::API::ManagedObject*>(guiMo);

    // Enumerate the descendant PlayObjects once. The small-HUD branch below reuses
    // this array rather than re-running findObjects (a full descendant walk that
    // allocates a managed array) a second time in the same frame.
    reframework::API::ManagedObject* descendants = nullptr;
    uint32_t descendantCount = 0;
    {
        void* findArgs[1] = { (void*)g_guiCam.playObjectRuntimeType };
        auto arrRet = g_guiCam.guiFindObjectsByType->invoke(guiMoPtr, std::span<void*>(findArgs));
        if (!arrRet.exception_thrown && arrRet.ptr) {
            descendants = reinterpret_cast<reframework::API::ManagedObject*>(arrRet.ptr);
            auto lenRet = ref::InvokeCached(descendants, g_hotMethods.arrGetLength, "get_Length", ref::EmptyArgs());
            if (!lenRet.exception_thrown) descendantCount = lenRet.dword;
            else descendants = nullptr;
        }
    }

    float pos[3] = { deltaX, deltaY, 0.f };
    void* setArgs[1] = { (void*)&pos[0] };

    if (descendantCount > kLargeHudDescendantThreshold) {
        auto viewRet = ref::InvokeCached(guiMoPtr, g_hotMethods.getView, "get_View", ref::EmptyArgs());
        if (viewRet.exception_thrown || !viewRet.ptr) return;
        auto view = reinterpret_cast<reframework::API::ManagedObject*>(viewRet.ptr);
        auto childrenRet = ref::InvokeCached(view, g_hotMethods.getChildren, "getChildren", ref::EmptyArgs());
        if (childrenRet.exception_thrown || !childrenRet.ptr) return;
        auto childArr = reinterpret_cast<reframework::API::ManagedObject*>(childrenRet.ptr);
        auto lenRet = ref::InvokeCached(childArr, g_hotMethods.arrGetLength, "get_Length", ref::EmptyArgs());
        uint32_t count = lenRet.exception_thrown ? 0 : lenRet.dword;

        // GetValue routes through the string-keyed ManagedObject overload (vector
        // only); reuse one buffer across iterations instead of allocating per index.
        static std::vector<void*> idxArgs(1);

        float absRoll = fabsf(g_crosshair.rollDegrees);
        bool applyRoll = (absRoll > 0.1f) && g_guiCam.transformGetGlobalPosition;
        if (applyRoll) {
            float rollRad = g_crosshair.rollDegrees * DEG_TO_RAD;
            float cosR = cosf(rollRad), sinR = sinf(rollRad);
            float zeroP[3] = {0,0,0};
            void* zeroArgs[1] = { (void*)&zeroP[0] };
            uint32_t cap = count < kMaxCompensatedHudChildren ? count : kMaxCompensatedHudChildren;
            for (uint32_t i = 0; i < cap; i++) {
                idxArgs[0] = (void*)(uintptr_t)i;
                auto elemRet = ref::InvokeCached(childArr, g_hotMethods.arrGetValue, "GetValue", idxArgs);
                if (elemRet.exception_thrown || !elemRet.ptr) continue;
                auto elem = reinterpret_cast<reframework::API::ManagedObject*>(elemRet.ptr);
                g_guiCam.transformSetPosition->invoke(elem, std::span<void*>(zeroArgs));
                auto gpRet = g_guiCam.transformGetGlobalPosition->invoke(elem, ref::EmptyArgs());
                if (gpRet.exception_thrown) continue;
                float gx = *reinterpret_cast<float*>(&gpRet.bytes[0]);
                float gy = *reinterpret_cast<float*>(&gpRet.bytes[4]);
                float rotX = gx*cosR - gy*sinR, rotY = gx*sinR + gy*cosR;
                float fp[3] = { (rotX-gx)+deltaX, (rotY-gy)+deltaY, 0.f };
                void* fArgs[1] = { (void*)&fp[0] };
                g_guiCam.transformSetPosition->invoke(elem, std::span<void*>(fArgs));
            }
        } else {
            uint32_t cap = count < kMaxCompensatedHudChildren ? count : kMaxCompensatedHudChildren;
            for (uint32_t i = 0; i < cap; i++) {
                idxArgs[0] = (void*)(uintptr_t)i;
                auto elemRet = ref::InvokeCached(childArr, g_hotMethods.arrGetValue, "GetValue", idxArgs);
                if (elemRet.exception_thrown || !elemRet.ptr) continue;
                g_guiCam.transformSetPosition->invoke(
                    reinterpret_cast<reframework::API::ManagedObject*>(elemRet.ptr), std::span<void*>(setArgs));
            }
        }
    } else {
        if (!descendants || descendantCount < 2) return;
        static std::vector<void*> idxArgs = { (void*)(uintptr_t)1 };
        auto elemRet = ref::InvokeCached(descendants, g_hotMethods.arrGetValue, "GetValue", idxArgs);
        if (elemRet.exception_thrown || !elemRet.ptr) return;
        g_guiCam.transformSetPosition->invoke(
            reinterpret_cast<reframework::API::ManagedObject*>(elemRet.ptr), std::span<void*>(setArgs));
    }
}

static void InitGUICompensationMethods() {
    if (g_guiCam.resolved || g_guiCam.giveUp) return;
    g_guiCam.resolved = true;

    const auto& api = reframework::API::get();
    auto tdb = api->tdb();
    auto guiType = tdb->find_type("via.gui.GUI");
    auto transformType = tdb->find_type("via.gui.TransformObject");
    auto playObjType = tdb->find_type("via.gui.PlayObject");
    if (!guiType || !transformType || !playObjType) { g_guiCam.giveUp = true; return; }

    g_guiCam.guiFindObjectsByType = guiType->find_method("findObjects(System.Type)");
    g_guiCam.transformSetPosition = transformType->find_method("set_Position");
    g_guiCam.transformGetGlobalPosition = transformType->find_method("get_GlobalPosition");
    auto runtimeType = playObjType->get_runtime_type();
    if (runtimeType) g_guiCam.playObjectRuntimeType = reinterpret_cast<reframework::API::TypeDefinition*>(runtimeType);

    auto camType = tdb->find_type("via.Camera");
    g_guiCam.getProjectionMatrix = camType ? camType->find_method("get_ProjectionMatrix") : nullptr;

    bool ready = g_guiCam.guiFindObjectsByType && g_guiCam.transformSetPosition && g_guiCam.playObjectRuntimeType;
    if (!ready) { g_guiCam.giveUp = true; return; }
    Logger::Instance().Info("GUI compensation methods resolved (projMat=%p)", (void*)g_guiCam.getProjectionMatrix);
}

static bool ReadGuiElementName(void* guiMo, char* out, size_t outSize) {
    out[0] = 0;
    auto mo = reinterpret_cast<reframework::API::ManagedObject*>(guiMo);
    auto goRet = ref::InvokeCached(mo, g_hotMethods.getGameObject, "get_GameObject", ref::EmptyArgs());
    if (goRet.exception_thrown || !goRet.ptr) return false;
    auto goMo = reinterpret_cast<reframework::API::ManagedObject*>(goRet.ptr);
    auto nameRet = ref::InvokeCached(goMo, g_hotMethods.getName, "get_Name", ref::EmptyArgs());
    if (nameRet.exception_thrown || !nameRet.ptr) return false;
    ref::ReadManagedString(nameRet.ptr, out, outSize);
    return out[0] != 0;
}

bool OnPreGuiDrawElement(void* element, void* context) {
    if (!Mod::Instance().IsEnabled()) return true;
    if (!element) return true;

    char goName[128] = {};
    if (!ReadGuiElementName(element, goName, sizeof(goName))) return true;

    static std::unordered_set<std::string> s_loggedNames;
    if (s_loggedNames.size() < kMaxLoggedGuiNames && s_loggedNames.insert(std::string(goName)).second) {
        Logger::Instance().Info("GUI element: \"%s\"", goName);
    }

    if (!g_guiCam.resolved && !g_guiCam.giveUp) InitGUICompensationMethods();

    auto mo = reinterpret_cast<reframework::API::ManagedObject*>(element);

    // Gui_FloatIcon is RE4's world-anchored floating-marker layer.
    if (strcmp(goName, "Gui_FloatIcon") == 0) ApplyMarkerCompensation(mo);

    bool isCrosshairCandidate = strncmp(goName, "Gui_ui20", 8) == 0;
    if (isCrosshairCandidate && g_crosshair.valid) ApplyCrosshairOffset(mo);

    return true;
}

// --- Initialization ---
static bool InitCachedFunctions() {
    static bool s_attempted = false;
    if (s_attempted) return !g_cameraResolver.HasFailed();
    s_attempted = true;

    if (!g_cameraResolver.Initialize()) return false;

    // Hook the camera controller from the candidate / TDB short-name fast
    // paths right away (the chainsaw.* types exist in the TDB before gameplay
    // starts). The parent-chain walk needs a live camera rig, so it runs from
    // EnsureCameraControllerHooked during gameplay if this misses.
    if (!g_controllerHooker.TryHook(nullptr)) {
        Logger::Instance().Warning("Camera controller hook not installed at init - retrying during gameplay");
    }

    Logger::Instance().Info("Methods cached");
    return true;
}

// Project the clean aim point into the head-tracked view to derive the smoothed
// reticle/marker offset (tangents + live FOV). Roll is sourced already-smoothed
// from the processor pipeline.
static void UpdateCrosshairProjection(const Matrix4x4f& clean, const Matrix4x4f& head) {
    constexpr float kAimDist = 50.0f;
    float rawTanRight = 0.f, rawTanUp = 0.f;
    if (!ProjectAimToViewTangents(clean, head, kAimDist, rawTanRight, rawTanUp)) {
        g_crosshair.valid = false;
        return;
    }

    float rawFov = g_cameraResolver.ResolveFovDegrees(g_cachedCamera);
    if (rawFov <= 10.f) rawFov = g_crosshair.fovDegrees;

    // Frame-rate-independent baseline smoothing. Raw perspective division and
    // per-frame FOV reads jitter; the reticle/marker offsets consume these
    // directly, so unsmoothed they visibly shake.
    float dt = Mod::Instance().GetLastDeltaTime();
    constexpr float kCrosshairSmoothing = static_cast<float>(cameraunlock::math::kBaselineSmoothing);

    static cameraunlock::math::SmoothedFloat s_tanRight;
    static cameraunlock::math::SmoothedFloat s_tanUp;
    static cameraunlock::math::SmoothedFloat s_fov;

    g_crosshair.tanRight = s_tanRight.Update(rawTanRight, kCrosshairSmoothing, dt);
    g_crosshair.tanUp = s_tanUp.Update(rawTanUp, kCrosshairSmoothing, dt);
    g_crosshair.fovDegrees = s_fov.Update(rawFov, kCrosshairSmoothing, dt);
    g_crosshair.valid = g_crosshair.fovDegrees > 10.f;

    float yaw=0, pitch=0, roll=0;
    Mod::Instance().GetProcessedRotation(yaw, pitch, roll);
    g_crosshair.rollDegrees = roll;
}

void OnPreBeginRendering() {
    // Drain hotkey requests on the render thread so recenter / position-toggle
    // never mutate session state concurrently with the pipeline tick below.
    Mod::Instance().ProcessDeferredActions();

    if (!InitCachedFunctions()) return;
    if (!Mod::Instance().IsEnabled()) return;
    if (!IsInGameplay()) return;
    EnsureCameraControllerHooked();
    if (ShouldRecenter()) Mod::Instance().Recenter();

    // Advance interpolation + smoothing once per render frame. Every
    // downstream consumer (ApplyHeadTracking, crosshair projection, GUI
    // compensation) reads cached values from this tick.
    Mod::Instance().TickFrame();

    void* transform = GetCameraTransformCached();
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);

    g_cleanCameraMatrix.matrix = *worldMat;
    g_cleanCameraMatrix.valid = true;

    ApplyHeadTracking(worldMat);
    g_trackingAppliedThisFrame = true;

    ComputeCleanToHeadRotation(g_cleanCameraMatrix.matrix, *worldMat, g_C);
    g_C_valid = true;

    ComputeCleanLocalPositionDelta(g_cleanCameraMatrix.matrix, *worldMat, g_posClean);
    UpdateCrosshairProjection(g_cleanCameraMatrix.matrix, *worldMat);
}

void OnPostBeginRendering() {
    if (!g_trackingAppliedThisFrame) return;
    g_trackingAppliedThisFrame = false;
    if (!g_cleanCameraMatrix.valid) return;

    // OnPreBeginRendering populated the per-frame transform cache this frame
    // (g_trackingAppliedThisFrame is only set after that succeeded), so reuse
    // it rather than re-walking the SceneManager chain.
    void* transform = GetCameraTransformCached();
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + ref::kTransformWorldMatrixOffset);
    __try {
        *worldMat = g_cleanCameraMatrix.matrix;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    g_cachedTransform = nullptr;
    g_cachedCamera = nullptr;
}

} // namespace RE4HT
