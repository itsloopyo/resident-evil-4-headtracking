#include "pch.h"
#include "camera_hook.h"
#include "math_types.h"
#include "game_state_detector.h"
#include "core/mod.h"
#include "core/logger.h"
#include "core/qpc_clock.h"

#include <reframework/API.hpp>
#include <cameraunlock/math/smoothing_utils.h>
#include <unordered_set>
#include <string>
#include <span>

namespace RE4HT {

constexpr int TX_WORLDMATRIX_OFFSET = 0x80;

// Reuse the resolved camera transform within this window instead of re-reflecting
// the SceneManager -> MainView -> PrimaryCamera -> GameObject -> Transform chain
// on every call; the camera keeps its identity within a single frame.
constexpr uint64_t kTransformCacheWindowUs = 2000;

// Distinct GUI element names logged for discovery, bounded so a busy HUD cannot
// flood the log.
constexpr size_t kMaxLoggedGuiNames = 100;

// A descendant PlayObject count above this marks RE4's full-screen HUD layer,
// which takes the view-tree walk rather than the small-element fast path.
constexpr uint32_t kLargeHudDescendantThreshold = 100;

// Upper bound on HUD child elements offset per frame, capping per-frame
// reflection cost on the large-HUD path.
constexpr uint32_t kMaxCompensatedHudChildren = 64;

static CrosshairProjection g_crosshair;

// Clean-to-head rotation matrix (3x3).
static float g_C[3][3] = {};
static bool g_C_valid = false;

// Head-to-clean position delta in clean-camera-local space.
static float g_posCleanX = 0.f;
static float g_posCleanY = 0.f;
static float g_posCleanZ = 0.f;

static struct {
    Matrix4x4f gameMatrix;
    bool hasGameMatrix = false;
} g_saved;

static struct {
    Matrix4x4f matrix;
    bool valid = false;
} g_cleanCameraMatrix;

static bool g_trackingAppliedThisFrame = false;

static struct {
    reframework::API::Method* getMainView = nullptr;
    reframework::API::Method* getPrimaryCamera = nullptr;
    reframework::API::Method* getGameObject = nullptr;
    reframework::API::Method* getTransform = nullptr;
    reframework::API::Method* getCameraFov = nullptr;
    void* sceneManager = nullptr;
    bool initialized = false;
    bool failed = false;
    bool smCached = false;
} g_fn;

static struct {
    reframework::API::Method* guiFindObjectsByType = nullptr;
    reframework::API::Method* transformSetPosition = nullptr;
    reframework::API::Method* transformGetGlobalPosition = nullptr;
    reframework::API::TypeDefinition* playObjectRuntimeType = nullptr;
    bool resolved = false;
    bool giveUp = false;
} g_guiCam;

static const std::vector<void*> g_emptyArgs{};

static struct {
    void* ptr = nullptr;
    uint64_t timestamp = 0;
} g_transformCache;

// String-keyed ManagedObject::invoke resolves get_type_definition()+find_method()
// on every call. These getters/array accessors are inherited, so find_method
// returns the same Method* for every instance; caching the first resolve removes
// that lookup pair from each per-element, per-frame call (the GetValue cache also
// covers the up-to-64-iteration HUD loop). Mirrors the "cache MethodInfo once" rule.
static struct {
    reframework::API::Method* getGameObject = nullptr;
    reframework::API::Method* getName = nullptr;
    reframework::API::Method* arrGetLength = nullptr;
    reframework::API::Method* arrGetValue = nullptr;
    reframework::API::Method* getView = nullptr;
    reframework::API::Method* getChildren = nullptr;
} g_hotMethods;

static reframework::InvokeRet InvokeCached(reframework::API::ManagedObject* obj,
                                           reframework::API::Method*& cache,
                                           const char* methodName,
                                           const std::vector<void*>& args) {
    if (!cache) {
        auto t = obj->get_type_definition();
        if (t) cache = t->find_method(methodName);
        if (!cache) return obj->invoke(methodName, args);
    }
    return cache->invoke(obj, args);
}

static void* CallMethod(reframework::API::Method* method, void* obj) {
    auto ret = method->invoke(reinterpret_cast<reframework::API::ManagedObject*>(obj), g_emptyArgs);
    return ret.ptr;
}

static void* GetCameraTransform() {
    uint64_t now = QpcNowMicros();
    if (g_transformCache.ptr && (now - g_transformCache.timestamp) < kTransformCacheWindowUs) {
        return g_transformCache.ptr;
    }

    if (!g_fn.smCached) {
        g_fn.sceneManager = reframework::API::get()->get_native_singleton("via.SceneManager");
        if (g_fn.sceneManager) g_fn.smCached = true;
    }
    if (!g_fn.sceneManager) return nullptr;

    // Reflecting through a camera that was torn down during a scene transition can
    // dereference freed memory inside the engine; guard the whole chain so a stale
    // object yields nullptr instead of crashing.
    void* transform = nullptr;
    __try {
        auto mv = CallMethod(g_fn.getMainView, g_fn.sceneManager);
        if (!mv) return nullptr;
        auto cam = CallMethod(g_fn.getPrimaryCamera, mv);
        if (!cam) return nullptr;
        auto go = CallMethod(g_fn.getGameObject, cam);
        if (!go) return nullptr;
        transform = CallMethod(g_fn.getTransform, go);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        g_transformCache.ptr = nullptr;
        return nullptr;
    }

    if (transform) {
        g_transformCache.ptr = transform;
        g_transformCache.timestamp = now;
    } else {
        g_transformCache.ptr = nullptr;
    }
    return transform;
}

static float GetLivePrimaryCameraFov() {
    if (!g_fn.getCameraFov || !g_fn.getMainView || !g_fn.getPrimaryCamera) return 0.f;
    void* sm = g_fn.sceneManager;
    if (!sm) sm = reframework::API::get()->get_native_singleton("via.SceneManager");
    if (!sm) return 0.f;

    // Same stale-camera hazard documented in GetCameraTransform: a camera torn
    // down mid scene-transition can dereference freed engine memory when
    // reflected through. invoke()'s exception_thrown flag only covers managed
    // exceptions, not a raw access violation, so guard the native chain with
    // SEH and fall back to "no live FOV" instead of crashing the game.
    float fovDeg = 0.f;
    __try {
        auto mv = g_fn.getMainView->invoke(reinterpret_cast<reframework::API::ManagedObject*>(sm), g_emptyArgs);
        if (mv.exception_thrown || !mv.ptr) return 0.f;
        auto cam = g_fn.getPrimaryCamera->invoke(reinterpret_cast<reframework::API::ManagedObject*>(mv.ptr), g_emptyArgs);
        if (cam.exception_thrown || !cam.ptr) return 0.f;
        auto fov = g_fn.getCameraFov->invoke(reinterpret_cast<reframework::API::ManagedObject*>(cam.ptr), g_emptyArgs);
        if (fov.exception_thrown) return 0.f;

        if (fov.f >= 10.f && fov.f <= 170.f) fovDeg = fov.f;
        else { float fromD = static_cast<float>(fov.d); if (fromD >= 10.f && fromD <= 170.f) fovDeg = fromD; }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return 0.f;
    }
    return fovDeg;
}

static void EulerToMatrix3x3(float yawRad, float pitchRad, float rollRad, float out[3][3]) {
    float cy = cosf(yawRad), sy = sinf(yawRad);
    float cp = cosf(pitchRad), sp = sinf(pitchRad);
    float cr = cosf(rollRad), sr = sinf(rollRad);
    out[0][0] = cy*cr + sy*sp*sr;   out[0][1] = cr*sy*sp - cy*sr;   out[0][2] = cp*sy;
    out[1][0] = cp*sr;               out[1][1] = cp*cr;               out[1][2] = -sp;
    out[2][0] = cy*sp*sr - cr*sy;   out[2][1] = sy*sr + cy*cr*sp;   out[2][2] = cy*cp;
}

static void ApplyHeadTracking(Matrix4x4f* worldMat) {
    float yaw, pitch, roll;
    if (!Mod::Instance().GetProcessedRotation(yaw, pitch, roll)) return;

    Matrix4x4f preRotationAxes = *worldMat;

    float yr = -yaw * DEG_TO_RAD;
    float pr = pitch * DEG_TO_RAD;
    float rr = roll * DEG_TO_RAD;

    if (Mod::Instance().IsWorldSpaceYaw()) {
        float cy = cosf(yr), sy = -sinf(yr);
        for (int r = 0; r < 3; r++) {
            float x = worldMat->m[r][0], z = worldMat->m[r][2];
            worldMat->m[r][0] = x * cy - z * sy;
            worldMat->m[r][2] = x * sy + z * cy;
        }
        float cp = cosf(pr), sp = sinf(pr), cr = cosf(rr), sr = sinf(rr);
        float prRot[3][3] = { {cr,sr,0}, {-cp*sr,cp*cr,sp}, {sp*sr,-sp*cr,cp} };
        for (int c = 0; c < 3; c++) {
            float c0=worldMat->m[0][c], c1=worldMat->m[1][c], c2=worldMat->m[2][c];
            worldMat->m[0][c] = prRot[0][0]*c0 + prRot[0][1]*c1 + prRot[0][2]*c2;
            worldMat->m[1][c] = prRot[1][0]*c0 + prRot[1][1]*c1 + prRot[1][2]*c2;
            worldMat->m[2][c] = prRot[2][0]*c0 + prRot[2][1]*c1 + prRot[2][2]*c2;
        }
    } else {
        // EulerToMatrix3x3 composes the opposite-handed rotation from the
        // world-yaw path above, so applying it directly inverts every axis.
        // Apply its transpose (the inverse rotation) to match the world-yaw
        // sign convention on yaw, pitch, and roll, including combined poses.
        float headRot[3][3];
        EulerToMatrix3x3(yr, pr, rr, headRot);
        for (int c = 0; c < 3; c++) {
            float c0=worldMat->m[0][c], c1=worldMat->m[1][c], c2=worldMat->m[2][c];
            worldMat->m[0][c] = headRot[0][0]*c0 + headRot[1][0]*c1 + headRot[2][0]*c2;
            worldMat->m[1][c] = headRot[0][1]*c0 + headRot[1][1]*c1 + headRot[2][1]*c2;
            worldMat->m[2][c] = headRot[0][2]*c0 + headRot[1][2]*c1 + headRot[2][2]*c2;
        }
    }

    // 6DOF position
    float px, py, pz;
    if (Mod::Instance().GetPositionOffset(px, py, pz)) {
        px = -px;
        const Matrix4x4f& gm = preRotationAxes;
        worldMat->m[3][0] += px*gm.m[0][0] + py*gm.m[1][0] + pz*gm.m[2][0];
        worldMat->m[3][1] += px*gm.m[0][1] + py*gm.m[1][1] + pz*gm.m[2][1];
        worldMat->m[3][2] += px*gm.m[0][2] + py*gm.m[1][2] + pz*gm.m[2][2];
    }
}

// --- Camera controller hooks ---
static int CameraUpdatePreHook(int argc, void** argv, REFrameworkTypeDefinitionHandle* arg_tys, unsigned long long ret_addr) {
    if (!g_saved.hasGameMatrix || !Mod::Instance().IsEnabled()) return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    void* transform = GetCameraTransform();
    if (!transform) return REFRAMEWORK_HOOK_CALL_ORIGINAL;
    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(reinterpret_cast<uint8_t*>(transform) + TX_WORLDMATRIX_OFFSET);
    // RE Engine transform pointers can go stale across scene transitions; guard
    // the raw write so a torn-down camera never crashes the game.
    __try {
        *worldMat = g_saved.gameMatrix;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return REFRAMEWORK_HOOK_CALL_ORIGINAL;
}

static void CameraUpdatePostHook(void** ret_val, REFrameworkTypeDefinitionHandle ret_ty, unsigned long long ret_addr) {
    void* transform = GetCameraTransform();
    if (!transform) return;
    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(reinterpret_cast<uint8_t*>(transform) + TX_WORLDMATRIX_OFFSET);
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

// --- GUI compensation ---
static bool GetMarkerProjectionFocalLengths(float& fx, float& fy) {
    constexpr float kHalfW = 960.f, kHalfH = 540.f, kAspect = kHalfW / kHalfH;
    // Reuse the FOV already fetched this frame in OnPreBeginRendering when valid;
    // avoids 4 reflection invokes per matching marker element.
    float fov = g_crosshair.valid ? g_crosshair.fovDegrees : GetLivePrimaryCameraFov();
    if (fov < 10.f || fov > 170.f) return false;
    float tanHFovY = tanf(fov * DEG_TO_RAD * 0.5f);
    fx = kHalfW / (tanHFovY * kAspect);
    fy = kHalfH / tanHFovY;
    return true;
}

static constexpr float kMarkerAssumedDepthMeters = 1.5f;

static bool ProjectCleanMarkerRayToHeadGui(float cleanX, float cleanY, float cleanZ,
                                           float fx, float fy, float& guiX, float& guiY) {
    if (!g_C_valid) return false;
    float rr = g_crosshair.rollDegrees * DEG_TO_RAD;
    float cr = cosf(rr), sr = sinf(rr);
    float C0[3], C1[3];
    for (int j = 0; j < 3; j++) {
        C0[j] = cr * g_C[0][j] - sr * g_C[1][j];
        C1[j] = sr * g_C[0][j] + cr * g_C[1][j];
    }
    float vx = C0[0]*cleanX + C0[1]*cleanY + C0[2]*cleanZ;
    float vy = C1[0]*cleanX + C1[1]*cleanY + C1[2]*cleanZ;
    float vz = g_C[2][0]*cleanX + g_C[2][1]*cleanY + g_C[2][2]*cleanZ;
    if (vz < 1e-4f) return false;
    guiX = -(vx / vz) * fx;
    guiY =  (vy / vz) * fy;
    return true;
}

static void ApplyMarkerCompensation(reframework::API::ManagedObject* guiMo) {
    if (!guiMo || !g_guiCam.guiFindObjectsByType || !g_guiCam.playObjectRuntimeType
        || !g_guiCam.transformSetPosition || !g_guiCam.transformGetGlobalPosition) return;
    if (!g_crosshair.valid || !Mod::Instance().IsEnabled() || !IsInGameplay()) return;

    float fx = 0.f, fy = 0.f;
    if (!GetMarkerProjectionFocalLengths(fx, fy)) return;

    void* findArgs[1] = { (void*)g_guiCam.playObjectRuntimeType };
    auto arrRet = g_guiCam.guiFindObjectsByType->invoke(guiMo, std::span<void*>(findArgs));
    if (arrRet.exception_thrown || !arrRet.ptr) return;
    auto arr = reinterpret_cast<reframework::API::ManagedObject*>(arrRet.ptr);
    auto lenRet = InvokeCached(arr, g_hotMethods.arrGetLength, "get_Length", g_emptyArgs);
    if (lenRet.exception_thrown || lenRet.dword < 2) return;

    static std::vector<void*> idx1 = { (void*)(uintptr_t)1 };
    auto elemRet = InvokeCached(arr, g_hotMethods.arrGetValue, "GetValue", idx1);
    if (elemRet.exception_thrown || !elemRet.ptr) return;
    auto child1 = reinterpret_cast<reframework::API::ManagedObject*>(elemRet.ptr);

    float zeroPos[3] = { 0.f, 0.f, 0.f };
    void* zeroArgs[1] = { (void*)&zeroPos[0] };
    g_guiCam.transformSetPosition->invoke(child1, std::span<void*>(zeroArgs));

    float markerX = 0.f, markerY = 0.f;
    auto gp = g_guiCam.transformGetGlobalPosition->invoke(child1, g_emptyArgs);
    if (!gp.exception_thrown) {
        markerX = *reinterpret_cast<float*>(&gp.bytes[0]);
        markerY = *reinterpret_cast<float*>(&gp.bytes[4]);
    }

    float cleanX = (-markerX / fx) * kMarkerAssumedDepthMeters + g_posCleanX;
    float cleanY = ( markerY / fy) * kMarkerAssumedDepthMeters + g_posCleanY;
    float cleanZ = kMarkerAssumedDepthMeters + g_posCleanZ;
    if (cleanZ < 0.25f) cleanZ = 0.25f;

    float projectedX = 0.f, projectedY = 0.f;
    bool projected = ProjectCleanMarkerRayToHeadGui(cleanX, cleanY, cleanZ, fx, fy, projectedX, projectedY);

    float deltaX = -g_crosshair.tanRight * fx;
    float deltaY =  g_crosshair.tanUp * fy;
    if (projected) { deltaX = projectedX - markerX; deltaY = projectedY - markerY; }

    float pos[3] = { deltaX, deltaY, 0.f };
    void* setArgs[1] = { (void*)&pos[0] };
    g_guiCam.transformSetPosition->invoke(child1, std::span<void*>(setArgs));
}

static void ApplyCrosshairOffset(reframework::API::ManagedObject* guiMo) {
    if (!guiMo || !g_guiCam.guiFindObjectsByType || !g_guiCam.playObjectRuntimeType
        || !g_guiCam.transformSetPosition) return;
    if (!g_crosshair.valid || !Mod::Instance().IsEnabled() || !IsInGameplay()) return;

    float fovRad = g_crosshair.fovDegrees * DEG_TO_RAD;
    float tanHalfFovY = tanf(fovRad * 0.5f);
    constexpr float kCanvasW = 1920.f, kCanvasH = 1080.f;
    float tanHalfFovX = tanHalfFovY * (kCanvasW / kCanvasH);
    float deltaX = -(g_crosshair.tanRight / tanHalfFovX) * (kCanvasW * 0.5f);
    float deltaY = (g_crosshair.tanUp / tanHalfFovY) * (kCanvasH * 0.5f);

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
            auto lenRet = InvokeCached(descendants, g_hotMethods.arrGetLength, "get_Length", g_emptyArgs);
            if (!lenRet.exception_thrown) descendantCount = lenRet.dword;
            else descendants = nullptr;
        }
    }

    float pos[3] = { deltaX, deltaY, 0.f };
    void* setArgs[1] = { (void*)&pos[0] };

    if (descendantCount > kLargeHudDescendantThreshold) {
        auto viewRet = InvokeCached(guiMoPtr, g_hotMethods.getView, "get_View", g_emptyArgs);
        if (viewRet.exception_thrown || !viewRet.ptr) return;
        auto view = reinterpret_cast<reframework::API::ManagedObject*>(viewRet.ptr);
        auto childrenRet = InvokeCached(view, g_hotMethods.getChildren, "getChildren", g_emptyArgs);
        if (childrenRet.exception_thrown || !childrenRet.ptr) return;
        auto childArr = reinterpret_cast<reframework::API::ManagedObject*>(childrenRet.ptr);
        auto lenRet = InvokeCached(childArr, g_hotMethods.arrGetLength, "get_Length", g_emptyArgs);
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
                auto elemRet = InvokeCached(childArr, g_hotMethods.arrGetValue, "GetValue", idxArgs);
                if (elemRet.exception_thrown || !elemRet.ptr) continue;
                auto elem = reinterpret_cast<reframework::API::ManagedObject*>(elemRet.ptr);
                g_guiCam.transformSetPosition->invoke(elem, std::span<void*>(zeroArgs));
                auto gpRet = g_guiCam.transformGetGlobalPosition->invoke(elem, g_emptyArgs);
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
                auto elemRet = InvokeCached(childArr, g_hotMethods.arrGetValue, "GetValue", idxArgs);
                if (elemRet.exception_thrown || !elemRet.ptr) continue;
                g_guiCam.transformSetPosition->invoke(
                    reinterpret_cast<reframework::API::ManagedObject*>(elemRet.ptr), std::span<void*>(setArgs));
            }
        }
    } else {
        if (!descendants || descendantCount < 2) return;
        static std::vector<void*> idxArgs = { (void*)(uintptr_t)1 };
        auto elemRet = InvokeCached(descendants, g_hotMethods.arrGetValue, "GetValue", idxArgs);
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

    bool ready = g_guiCam.guiFindObjectsByType && g_guiCam.transformSetPosition && g_guiCam.playObjectRuntimeType;
    if (!ready) { g_guiCam.giveUp = true; return; }
    Logger::Instance().Info("GUI compensation methods resolved");
}

static bool ReadGuiElementName(void* guiMo, char* out, size_t outSize) {
    out[0] = 0;
    auto mo = reinterpret_cast<reframework::API::ManagedObject*>(guiMo);
    auto goRet = InvokeCached(mo, g_hotMethods.getGameObject, "get_GameObject", g_emptyArgs);
    if (goRet.exception_thrown || !goRet.ptr) return false;
    auto goMo = reinterpret_cast<reframework::API::ManagedObject*>(goRet.ptr);
    auto nameRet = InvokeCached(goMo, g_hotMethods.getName, "get_Name", g_emptyArgs);
    if (nameRet.exception_thrown || !nameRet.ptr) return false;
    // SEH probe: raw pointer arithmetic into managed-string layout (length@+0x10, UTF-16 chars@+0x14).
    // If the string layout ever shifts, we catch the AV and log — do not swallow silently.
    __try {
        auto* raw = reinterpret_cast<uint8_t*>(nameRet.ptr);
        uint32_t strLen = *reinterpret_cast<uint32_t*>(raw + 0x10);
        if (strLen > outSize - 1) strLen = static_cast<uint32_t>(outSize - 1);
        auto* chars = reinterpret_cast<uint16_t*>(raw + 0x14);
        for (uint32_t i = 0; i < strLen; i++) out[i] = (char)chars[i];
        out[strLen] = 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        static bool s_loggedOnce = false;
        if (!s_loggedOnce) {
            Logger::Instance().Warning("ReadGuiElementName: access violation probing managed-string memory; GUI compensation degraded");
            s_loggedOnce = true;
        }
        return false;
    }
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

    // Gui_ui2010* is RE4's world-anchored marker layer (objective markers, interaction prompts)
    if (strncmp(goName, "Gui_ui2010", 10) == 0) ApplyMarkerCompensation(mo);

    bool isCrosshairCandidate = (strncmp(goName, "Gui_ui20", 8) == 0)
                             && (strncmp(goName, "Gui_ui2010", 10) != 0);
    if (isCrosshairCandidate && g_crosshair.valid) ApplyCrosshairOffset(mo);

    return true;
}

// --- Initialization ---
static bool InitCachedFunctions() {
    if (g_fn.initialized) return !g_fn.failed;
    g_fn.initialized = true;

    const auto& api = reframework::API::get();
    auto tdb = api->tdb();
    auto smType = tdb->find_type("via.SceneManager");
    auto svType = tdb->find_type("via.SceneView");
    auto camType = tdb->find_type("via.Camera");
    auto goType = tdb->find_type("via.GameObject");
    if (!smType || !svType || !camType || !goType) { g_fn.failed = true; return false; }

    g_fn.getMainView = smType->find_method("get_MainView");
    g_fn.getPrimaryCamera = svType->find_method("get_PrimaryCamera");
    g_fn.getGameObject = camType->find_method("get_GameObject");
    g_fn.getTransform = goType->find_method("get_Transform");
    g_fn.getCameraFov = camType->find_method("get_FOV");
    if (!g_fn.getMainView || !g_fn.getPrimaryCamera || !g_fn.getGameObject || !g_fn.getTransform) {
        g_fn.failed = true; return false;
    }

    const char* cameraHookCandidates[][2] = {
        {"chainsaw.PlayerCameraController", "onCameraUpdate"},
        {"chainsaw.PlayerCameraController", "lateUpdate"},
        {"chainsaw.PlayerCameraController", "update"},
        {"chainsaw.CameraManager", "onCameraUpdate"},
        {"chainsaw.CameraManager", "lateUpdate"},
        {"chainsaw.CameraManager", "update"},
    };
    for (const auto& [typeName, methodName] : cameraHookCandidates) {
        auto type = tdb->find_type(typeName);
        if (!type) continue;
        auto method = type->find_method(methodName);
        if (!method) continue;
        auto id = method->add_hook(CameraUpdatePreHook, CameraUpdatePostHook, false);
        Logger::Instance().Info("Hooked %s.%s (id=%u)", typeName, methodName, id);
        break;
    }

    Logger::Instance().Info("Methods cached");
    return true;
}

// Head-tracking translation expressed in the clean camera's local axes; the
// marker compensation pass reuses it to keep world-anchored GUI on target.
static void UpdateCleanPositionDelta(const Matrix4x4f& clean, const Matrix4x4f& head) {
    float dwx = head.m[3][0]-clean.m[3][0], dwy = head.m[3][1]-clean.m[3][1], dwz = head.m[3][2]-clean.m[3][2];
    g_posCleanX = dwx*clean.m[0][0] + dwy*clean.m[0][1] + dwz*clean.m[0][2];
    g_posCleanY = dwx*clean.m[1][0] + dwy*clean.m[1][1] + dwz*clean.m[1][2];
    g_posCleanZ = dwx*clean.m[2][0] + dwy*clean.m[2][1] + dwz*clean.m[2][2];
}

// Project the clean aim point into the head-tracked view to derive the smoothed
// reticle/marker offset (tangents + live FOV). Roll is sourced already-smoothed
// from the processor pipeline.
static void UpdateCrosshairProjection(const Matrix4x4f& clean, const Matrix4x4f& head) {
    constexpr float kAimDist = 50.0f;
    float aimX = clean.m[3][0]+kAimDist*clean.m[2][0], aimY = clean.m[3][1]+kAimDist*clean.m[2][1], aimZ = clean.m[3][2]+kAimDist*clean.m[2][2];
    float dx = aimX-head.m[3][0], dy = aimY-head.m[3][1], dz = aimZ-head.m[3][2];
    float vx = dx*head.m[0][0]+dy*head.m[0][1]+dz*head.m[0][2];
    float vy = dx*head.m[1][0]+dy*head.m[1][1]+dz*head.m[1][2];
    float vz = dx*head.m[2][0]+dy*head.m[2][1]+dz*head.m[2][2];
    if (vz <= 1e-4f) {
        g_crosshair.valid = false;
        return;
    }

    float rawTanRight = vx / vz;
    float rawTanUp = vy / vz;
    float liveFov = GetLivePrimaryCameraFov();

    // Frame-rate-independent baseline smoothing. Raw perspective division and
    // per-frame FOV reads jitter; the reticle/marker offsets consume these
    // directly, so unsmoothed they visibly shake.
    float dt = Mod::Instance().GetLastDeltaTime();
    float t = cameraunlock::math::CalculateSmoothingFactor(
        static_cast<float>(cameraunlock::math::kBaselineSmoothing), dt);

    static float s_tanRight = 0.f, s_tanUp = 0.f, s_fov = 75.f;
    static bool s_init = false;
    if (!s_init) {
        s_tanRight = rawTanRight;
        s_tanUp = rawTanUp;
        if (liveFov > 10.f) s_fov = liveFov;
        s_init = true;
    } else {
        s_tanRight = cameraunlock::math::Lerp(s_tanRight, rawTanRight, t);
        s_tanUp = cameraunlock::math::Lerp(s_tanUp, rawTanUp, t);
        if (liveFov > 10.f) s_fov = cameraunlock::math::Lerp(s_fov, liveFov, t);
    }

    g_crosshair.tanRight = s_tanRight;
    g_crosshair.tanUp = s_tanUp;
    g_crosshair.fovDegrees = s_fov;
    g_crosshair.valid = g_crosshair.fovDegrees > 10.f;
    float yaw=0, pitch=0, roll=0;
    Mod::Instance().GetProcessedRotation(yaw, pitch, roll);
    g_crosshair.rollDegrees = roll;
}

void OnPreBeginRendering() {
    Mod::Instance().PollChordHotkeys();
    if (!InitCachedFunctions()) return;
    if (!Mod::Instance().IsEnabled()) return;
    if (!IsInGameplay()) return;
    if (ShouldRecenter()) Mod::Instance().Recenter();

    void* transform = GetCameraTransform();
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + TX_WORLDMATRIX_OFFSET);

    g_cleanCameraMatrix.matrix = *worldMat;
    g_cleanCameraMatrix.valid = true;

    ApplyHeadTracking(worldMat);
    g_trackingAppliedThisFrame = true;

    ComputeCleanToHeadRotation(g_cleanCameraMatrix.matrix, *worldMat, g_C);
    g_C_valid = true;

    UpdateCleanPositionDelta(g_cleanCameraMatrix.matrix, *worldMat);
    UpdateCrosshairProjection(g_cleanCameraMatrix.matrix, *worldMat);
}

void OnPostBeginRendering() {
    if (!g_trackingAppliedThisFrame) return;
    g_trackingAppliedThisFrame = false;
    if (!g_cleanCameraMatrix.valid) return;

    void* transform = GetCameraTransform();
    if (!transform) return;

    Matrix4x4f* worldMat = reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + TX_WORLDMATRIX_OFFSET);
    __try {
        *worldMat = g_cleanCameraMatrix.matrix;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

} // namespace RE4HT
