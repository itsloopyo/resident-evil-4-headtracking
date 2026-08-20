#include "pch.h"
#include "mod.h"
#include "logger.h"

#include <cameraunlock/time/qpc_clock.h>

namespace RE4HT {

using cameraunlock::TrackingMode;

// The session re-reads the receiver's connection locality every Update() and
// selects LocalSmoothing or RemoteSmoothing from it, but that wiring is
// SFINAE-gated on the receiver exposing IsRemoteConnection(). A receiver
// adapter that failed to forward the method would still compile and would
// silently pin every connection to LocalSmoothing forever.
static_assert(cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>::kHasRemoteConnection,
              "receiver must expose IsRemoteConnection() or remote smoothing never applies");

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

    cameraunlock::SensitivitySettings sensitivity;
    sensitivity.yaw = m_config.yawMultiplier;
    sensitivity.pitch = m_config.pitchMultiplier;
    sensitivity.roll = m_config.rollMultiplier;
    m_session.GetProcessor().SetSensitivity(sensitivity);

    Logger::Instance().Info("Sensitivity: yaw=%.2f pitch=%.2f roll=%.2f",
                            sensitivity.yaw, sensitivity.pitch, sensitivity.roll);

    Logger::Instance().Info("Smoothing: local=%.2f remote=%.2f",
                            m_config.localSmoothing, m_config.remoteSmoothing);

    m_session.SetMode(m_config.positionEnabled ? TrackingMode::RotationAndPosition
                                               : TrackingMode::RotationOnly);
    m_reticleEnabled.store(m_config.reticleEnabled, std::memory_order_relaxed);
    m_worldSpaceYaw.store(m_config.worldSpaceYaw, std::memory_order_relaxed);

    // Assigned by name rather than through the positional constructor.
    // PositionSettings takes nine floats before its three inversion bools, so a
    // positional call that gains or loses one argument silently rebinds a bool
    // to a float parameter - an invert flag would land in a smoothing slot - and
    // still compiles clean. Naming every field removes that failure mode.
    cameraunlock::PositionSettings posSettings;
    posSettings.sensitivity_x = m_config.positionSensitivityX;
    posSettings.sensitivity_y = m_config.positionSensitivityY;
    posSettings.sensitivity_z = m_config.positionSensitivityZ;
    posSettings.limit_x = m_config.positionLimitX;
    posSettings.limit_y = m_config.positionLimitY;
    // Asymmetric Z: negative z is the forward lean, so the generous limit_z is
    // the forward range and limit_z_back restricts leaning back into the player.
    posSettings.limit_z = m_config.positionLimitZ;
    posSettings.limit_z_back = m_config.positionLimitZBack;
    // Position smoothing lives on the settings; the processor picks between the
    // two per connection from the flag the session feeds it.
    posSettings.local_smoothing = m_config.localSmoothing;
    posSettings.remote_smoothing = m_config.remoteSmoothing;
    posSettings.invert_x = m_config.positionInvertX;
    posSettings.invert_y = m_config.positionInvertY;
    posSettings.invert_z = m_config.positionInvertZ;
    m_session.GetPositionProcessor().SetSettings(posSettings);

    // Rotation smoothing. The session setter also re-writes the two values into
    // the position settings above, so it has to run after SetSettings; the
    // values are identical either way, which keeps rotation and position from
    // ever drifting apart.
    m_session.SetLocalSmoothing(m_config.localSmoothing);
    m_session.SetRemoteSmoothing(m_config.remoteSmoothing);

    // The previous per-mod pipeline never engaged tracker pivot compensation
    // (it passed radians to a degrees API, zeroing the artifact). Keep that
    // tuning until pivot compensation is verified in game.
    m_session.GetPositionProcessor().SetTrackerPivotForward(0.0f);

    Logger::Instance().Info("Position: %s, sens=%.1f/%.1f/%.1f",
                            m_session.IsPositionActive() ? "6DOF" : "3DOF",
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
        Logger::Instance().Warning("Config not found at %s - defaults written there", configPath.c_str());
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

void Mod::ToggleReticle() {
    bool enabled = !m_reticleEnabled.load(std::memory_order_relaxed);
    m_reticleEnabled.store(enabled, std::memory_order_relaxed);
    Logger::Instance().Info("Reticle %s", enabled ? "enabled" : "disabled");
}

void Mod::TogglePosition() {
    bool enabled = !m_session.IsPositionActive();
    m_session.SetMode(enabled ? TrackingMode::RotationAndPosition : TrackingMode::RotationOnly);
    Logger::Instance().Info("Position tracking %s", enabled ? "enabled" : "disabled");
}

void Mod::ProcessDeferredActions() {
    if (!m_initialized.load()) return;
    if (m_togglePositionRequested.Consume()) TogglePosition();
}

void Mod::TickFrame() {
    if (!m_initialized.load()) return;

    uint64_t now = cameraunlock::time::QpcNowMicros();
    float deltaTime = DEFAULT_FRAME_TIME_S;
    if (m_lastFrameTickTime > 0) {
        deltaTime = (now - m_lastFrameTickTime) / 1000000.0f;
        if (deltaTime > MAX_FRAME_TIME_S) deltaTime = MAX_FRAME_TIME_S;
        if (deltaTime < MIN_FRAME_TIME_S) deltaTime = MIN_FRAME_TIME_S;
    }
    m_lastFrameTickTime = now;
    m_lastDeltaTime = deltaTime;

    if (!m_session.Update(deltaTime)) return;
}

void Mod::LogFirstTrackerPose() {
    if (!m_initialized.load()) return;
    if (m_loggedFirstPose) return;

    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    if (!m_udpReceiver.GetRotation(yaw, pitch, roll)) return;

    m_loggedFirstPose = true;
    Logger::Instance().Info("First tracker pose received: yaw=%.2f pitch=%.2f roll=%.2f (%s connection)",
                            yaw, pitch, roll,
                            m_udpReceiver.IsRemoteConnection() ? "remote" : "local");
}

bool Mod::GetProcessedRotation(float& yaw, float& pitch, float& roll) {
    return m_session.GetRotation(yaw, pitch, roll);
}

bool Mod::GetPositionOffset(float& x, float& y, float& z) {
    return m_session.GetPositionOffset(x, y, z);
}

void Mod::ToggleYawMode() {
    bool worldSpace = !m_worldSpaceYaw.load(std::memory_order_relaxed);
    m_worldSpaceYaw.store(worldSpace, std::memory_order_relaxed);
    Logger::Instance().Info("Yaw mode: %s", worldSpace ? "world-space (horizon-locked)" : "camera-local");
}

} // namespace RE4HT
