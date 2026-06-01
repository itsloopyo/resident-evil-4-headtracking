#pragma once

#include <atomic>
#include <mutex>

#include "config.h"
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/processing/tracking_processor.h>
#include <cameraunlock/processing/pose_interpolator.h>
#include <cameraunlock/processing/position_processor.h>
#include <cameraunlock/processing/position_interpolator.h>

namespace RE4HT {

class Mod {
public:
    static Mod& Instance();

    bool Initialize();
    void Shutdown();

    bool IsEnabled() const { return m_enabled.load(); }
    void SetEnabled(bool enabled);
    void Toggle();

    void Recenter();
    void TogglePosition();
    void ToggleReticle();
    void ToggleYawMode();

    // Polls Ctrl+Shift+<letter> chord hotkeys. Call once per frame from the render callback.
    void PollChordHotkeys();

    Config& GetConfig() { return m_config; }
    const Config& GetConfig() const { return m_config; }

    bool GetProcessedRotation(float& yaw, float& pitch, float& roll);
    bool GetPositionOffset(float& x, float& y, float& z);

    // Wall-clock dt of the most recent processed frame, for frame-rate-independent
    // smoothing of derived quantities (e.g. crosshair projection).
    float GetLastDeltaTime() const { return m_lastDeltaTime; }

    bool IsReticleEnabled() const { return m_reticleEnabled.load(std::memory_order_relaxed); }
    bool IsPositionEnabled() const { return m_positionEnabled.load(std::memory_order_relaxed); }
    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(std::memory_order_relaxed); }

    Mod(const Mod&) = delete;
    Mod& operator=(const Mod&) = delete;

private:
    Mod() = default;
    ~Mod() = default;

    bool LoadConfig();

    // Recenter the tracking pipeline. Caller must hold m_pipelineMutex.
    void RecenterImpl();

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};

    // Ctrl+Shift+<letter> chord edge-detect state (nav-cluster-free alternatives).
    bool m_recenterChordDown = false;
    bool m_toggleChordDown = false;
    bool m_positionChordDown = false;
    bool m_reticleChordDown = false;
    bool m_yawChordDown = false;

    Config m_config;
    cameraunlock::UdpReceiver m_udpReceiver;

    // Guards the tracking pipeline state below (processors, interpolators,
    // cached pose, timing). The nav-cluster hotkeys fire Recenter/TogglePosition
    // from the background HotkeyPoller thread while the render thread is running
    // GetProcessedRotation/GetPositionOffset on the same objects; without this
    // lock those resets race the in-flight Process()/Update() calls.
    std::mutex m_pipelineMutex;
    cameraunlock::PoseInterpolator m_poseInterpolator;
    cameraunlock::TrackingProcessor m_processor;
    int64_t m_lastReceiveTimestamp = 0;

    cameraunlock::PositionProcessor m_positionProcessor;
    cameraunlock::PositionInterpolator m_positionInterpolator;
    std::atomic<bool> m_positionEnabled{true};
    std::atomic<bool> m_reticleEnabled{true};
    std::atomic<bool> m_worldSpaceYaw{true};

    uint64_t m_lastProcessTime = 0;
    float m_lastDeltaTime = 0.016f;

    float m_cachedYaw = 0.0f;
    float m_cachedPitch = 0.0f;
    float m_cachedRoll = 0.0f;
    bool m_cachedValid = false;
    bool m_hasCentered = false;
    int m_stabilizationFrames = 0;

    // Previous raw values for new-sample detection (data change, not just packet arrival)
    float m_lastRawYaw = 0.0f;
    float m_lastRawPitch = 0.0f;
    float m_lastRawRoll = 0.0f;
};

} // namespace RE4HT
