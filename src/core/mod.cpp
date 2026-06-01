#include "pch.h"
#include "mod.h"
#include "logger.h"
#include "qpc_clock.h"

namespace RE4HT {

// Skip noisy initial frames before auto-recentering (~0.5s at 60fps)
constexpr int STABILIZATION_FRAME_COUNT = 30;
// Avoid re-processing rotation within the same frame (microseconds)
constexpr uint64_t ROTATION_CACHE_THRESHOLD_US = 1000;
// Seed frame time (~60fps) used before two timestamps exist, plus the clamp band
// that stops a stall or debugger pause from injecting a huge dt into smoothing.
constexpr float DEFAULT_FRAME_TIME_S = 0.016f;
constexpr float MIN_FRAME_TIME_S = 0.0001f;
constexpr float MAX_FRAME_TIME_S = 0.1f;

Mod& Mod::Instance() {
    static Mod instance;
    return instance;
}

bool Mod::Initialize() {
    if (m_initialized.load()) {
        Logger::Instance().Warning("Mod already initialized");
        return true;
    }

    Logger::Instance().Info("RE4 Head Tracking v%s initializing...", RE4HT_VERSION);

    if (!LoadConfig()) {
        Logger::Instance().Warning("Using default configuration");
    }

    // Initialize TrackingProcessor
    cameraunlock::SensitivitySettings sensitivity;
    sensitivity.yaw = m_config.yawMultiplier;
    sensitivity.pitch = m_config.pitchMultiplier;
    sensitivity.roll = m_config.rollMultiplier;
    m_processor.SetSensitivity(sensitivity);

    Logger::Instance().Info("Sensitivity: yaw=%.2f pitch=%.2f roll=%.2f",
                            sensitivity.yaw, sensitivity.pitch, sensitivity.roll);

    // Initialize position processor
    m_positionEnabled.store(m_config.positionEnabled, std::memory_order_relaxed);
    m_reticleEnabled.store(m_config.reticleEnabled, std::memory_order_relaxed);
    m_worldSpaceYaw.store(m_config.worldSpaceYaw, std::memory_order_relaxed);

    cameraunlock::PositionSettings posSettings(
        m_config.positionSensitivityX, m_config.positionSensitivityY, m_config.positionSensitivityZ,
        m_config.positionLimitX, m_config.positionLimitY, m_config.positionLimitZ, m_config.positionLimitZBack,
        m_config.positionSmoothing,
        m_config.positionInvertX, m_config.positionInvertY, m_config.positionInvertZ
    );
    m_positionProcessor.SetSettings(posSettings);

    Logger::Instance().Info("Position: %s, sens=%.1f/%.1f/%.1f",
                            m_positionEnabled ? "6DOF" : "3DOF",
                            posSettings.sensitivity_x, posSettings.sensitivity_y, posSettings.sensitivity_z);

    // Route receiver bind/retry diagnostics to our logger so users see
    // "Failed to bind" / "Still waiting" / "Bound after N retries".
    m_udpReceiver.SetLog([](const std::string& msg) {
        Logger::Instance().Info("%s", msg.c_str());
    });

    // A false return means the port is held by another process; the receiver has
    // scheduled a background bind retry every 5s. Keep the mod fully alive so
    // tracking resumes the moment the port is released - do not abort init here.
    if (!m_udpReceiver.Start(m_config.udpPort)) {
        Logger::Instance().Warning("UDP port %d is busy; retrying in the background", m_config.udpPort);
    } else {
        Logger::Instance().Info("UDP receiver started on port %d", m_config.udpPort);
    }

    if (m_config.autoEnable) {
        m_enabled.store(true);
        Logger::Instance().Info("Head tracking auto-enabled");
    }

    m_initialized.store(true);
    Logger::Instance().Info("Initialization complete");
    return true;
}

void Mod::Shutdown() {
    if (!m_initialized.load()) return;

    Logger::Instance().Info("Shutting down...");
    m_udpReceiver.Stop();
    m_initialized.store(false);
    Logger::Instance().Info("Shutdown complete");
}

bool Mod::LoadConfig() {
    // Config file is in the same directory as the DLL (reframework/plugins/)
    HMODULE hModule = nullptr;
    char dllPath[MAX_PATH] = {};
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&Mod::Instance, &hModule)) {
        GetModuleFileNameA(hModule, dllPath, MAX_PATH);
    }

    std::string configPath;
    if (dllPath[0] != '\0') {
        configPath = dllPath;
        auto lastSlash = configPath.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            configPath = configPath.substr(0, lastSlash + 1);
        }
    }
    configPath += "HeadTracking.ini";

    if (!m_config.Load(configPath.c_str())) {
        m_config.SetDefaults();
        m_config.Save(configPath.c_str());
        return false;
    }
    return true;
}

void Mod::SetEnabled(bool enabled) {
    bool wasEnabled = m_enabled.exchange(enabled);
    if (wasEnabled != enabled) {
        Logger::Instance().Info("Head tracking %s", enabled ? "enabled" : "disabled");
    }
}

void Mod::Toggle() {
    SetEnabled(!m_enabled.load());
}

void Mod::Recenter() {
    std::lock_guard<std::mutex> lock(m_pipelineMutex);
    RecenterImpl();
}

void Mod::RecenterImpl() {
    m_udpReceiver.Recenter();
    m_processor.Reset();
    m_poseInterpolator.Reset();
    m_lastProcessTime = 0;

    float px, py, pz;
    if (m_udpReceiver.GetPosition(px, py, pz)) {
        cameraunlock::PositionData posCenter(px, py, pz);
        m_positionProcessor.SetCenter(posCenter);
    }
    m_positionInterpolator.Reset();

    Logger::Instance().Info("View recentered");
}

void Mod::ToggleReticle() {
    bool enabled = !m_reticleEnabled.load(std::memory_order_relaxed);
    m_reticleEnabled.store(enabled, std::memory_order_relaxed);
    Logger::Instance().Info("Reticle %s", enabled ? "enabled" : "disabled");
}

void Mod::TogglePosition() {
    bool enabled = !m_positionEnabled.load(std::memory_order_relaxed);
    m_positionEnabled.store(enabled, std::memory_order_relaxed);
    if (!enabled) {
        std::lock_guard<std::mutex> lock(m_pipelineMutex);
        m_positionProcessor.Reset();
        m_positionInterpolator.Reset();
    }
    Logger::Instance().Info("Position tracking %s", enabled ? "enabled" : "disabled");
}

bool Mod::GetProcessedRotation(float& yaw, float& pitch, float& roll) {
    std::lock_guard<std::mutex> lock(m_pipelineMutex);
    uint64_t now = QpcNowMicros();
    if (m_lastProcessTime > 0 && (now - m_lastProcessTime) < ROTATION_CACHE_THRESHOLD_US) {
        yaw = m_cachedYaw;
        pitch = m_cachedPitch;
        roll = m_cachedRoll;
        return m_cachedValid;
    }

    float rawYaw, rawPitch, rawRoll;
    if (!m_udpReceiver.GetRotation(rawYaw, rawPitch, rawRoll)) {
        m_lastProcessTime = now;
        m_cachedValid = false;
        return false;
    }

    // Wait for stabilization before auto-recentering (skip noisy initial frames)
    if (!m_hasCentered) {
        m_stabilizationFrames++;
        if (m_stabilizationFrames >= STABILIZATION_FRAME_COUNT) {
            m_hasCentered = true;
            RecenterImpl();
            Logger::Instance().Info("Auto-recentered after %d frames", m_stabilizationFrames);
        }
        // Still process data below so smoothing settles
    }

    float deltaTime = DEFAULT_FRAME_TIME_S;
    if (m_lastProcessTime > 0) {
        deltaTime = (now - m_lastProcessTime) / 1000000.0f;
        if (deltaTime > MAX_FRAME_TIME_S) deltaTime = MAX_FRAME_TIME_S;
        if (deltaTime < MIN_FRAME_TIME_S) deltaTime = MIN_FRAME_TIME_S;
    }
    m_lastProcessTime = now;
    m_lastDeltaTime = deltaTime;

    int64_t receiveTs = m_udpReceiver.GetLastReceiveTimestamp();
    bool isNewPacket = (receiveTs != m_lastReceiveTimestamp);
    m_lastReceiveTimestamp = receiveTs;

    bool isNewSample = isNewPacket &&
        (rawYaw != m_lastRawYaw || rawPitch != m_lastRawPitch || rawRoll != m_lastRawRoll);
    if (isNewPacket) {
        m_lastRawYaw = rawYaw;
        m_lastRawPitch = rawPitch;
        m_lastRawRoll = rawRoll;
    }

    cameraunlock::InterpolatedPose interpolated = m_poseInterpolator.Update(
        rawYaw, rawPitch, rawRoll, isNewSample, deltaTime);

    cameraunlock::TrackingPose processed = m_processor.Process(
        interpolated.yaw, interpolated.pitch, interpolated.roll, deltaTime);

    yaw = processed.yaw;
    pitch = processed.pitch;
    roll = processed.roll;

    m_cachedYaw = yaw;
    m_cachedPitch = pitch;
    m_cachedRoll = roll;
    m_cachedValid = true;

    return true;
}

bool Mod::GetPositionOffset(float& x, float& y, float& z) {
    std::lock_guard<std::mutex> lock(m_pipelineMutex);
    if (!m_positionEnabled.load(std::memory_order_relaxed)) {
        x = y = z = 0.0f;
        return false;
    }

    float rawX, rawY, rawZ;
    if (!m_udpReceiver.GetPosition(rawX, rawY, rawZ)) {
        x = y = z = 0.0f;
        return false;
    }

    float deltaTime = m_lastDeltaTime;
    cameraunlock::PositionData rawPos(rawX, rawY, rawZ);
    cameraunlock::PositionData interpolatedPos = m_positionInterpolator.Update(rawPos, deltaTime);

    cameraunlock::math::Quat4 headRotQ = cameraunlock::math::Quat4::FromYawPitchRoll(
        m_cachedYaw * static_cast<float>(cameraunlock::math::kDegToRad),
        m_cachedPitch * static_cast<float>(cameraunlock::math::kDegToRad),
        m_cachedRoll * static_cast<float>(cameraunlock::math::kDegToRad));

    cameraunlock::math::Vec3 offset = m_positionProcessor.Process(interpolatedPos, headRotQ, deltaTime);

    x = offset.x;
    y = offset.y;
    z = offset.z;
    return true;
}

void Mod::ToggleYawMode() {
    bool worldSpace = !m_worldSpaceYaw.load(std::memory_order_relaxed);
    m_worldSpaceYaw.store(worldSpace, std::memory_order_relaxed);
    Logger::Instance().Info("Yaw mode: %s", worldSpace ? "world-space (horizon-locked)" : "camera-local");
}

void Mod::PollChordHotkeys() {
    constexpr int kPressed = 0x8000;
    const bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & kPressed) != 0;
    const bool shift = (GetAsyncKeyState(VK_SHIFT)   & kPressed) != 0;
    const bool mods  = ctrl && shift;

    const bool recenterNow = mods && ((GetAsyncKeyState('T') & kPressed) != 0);
    if (recenterNow && !m_recenterChordDown) Recenter();
    m_recenterChordDown = recenterNow;

    const bool toggleNow = mods && ((GetAsyncKeyState('Y') & kPressed) != 0);
    if (toggleNow && !m_toggleChordDown) Toggle();
    m_toggleChordDown = toggleNow;

    const bool positionNow = mods && ((GetAsyncKeyState('G') & kPressed) != 0);
    if (positionNow && !m_positionChordDown) TogglePosition();
    m_positionChordDown = positionNow;

    const bool yawNow = mods && ((GetAsyncKeyState('H') & kPressed) != 0);
    if (yawNow && !m_yawChordDown) ToggleYawMode();
    m_yawChordDown = yawNow;

    const bool reticleNow = mods && ((GetAsyncKeyState('U') & kPressed) != 0);
    if (reticleNow && !m_reticleChordDown) ToggleReticle();
    m_reticleChordDown = reticleNow;
}

} // namespace RE4HT
