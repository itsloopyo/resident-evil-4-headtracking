#pragma once

#include <atomic>

#include "config.h"
#include <cameraunlock/input/deferred_actions.h>
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/tracking/head_tracking_session.h>

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

    // Hotkey callbacks fire on the HotkeyPoller's background thread, but
    // Recenter and TogglePosition mutate the session's non-atomic
    // processor/interpolator smoothing state owned by the render thread. The
    // hotkey thread only requests the action; ProcessDeferredActions() runs it
    // on the render thread at the start of each frame.
    void RequestRecenter() { m_recenterRequested.Request(); }
    void RequestTogglePosition() { m_togglePositionRequested.Request(); }
    void ProcessDeferredActions();

    Config& GetConfig() { return m_config; }
    const Config& GetConfig() const { return m_config; }

    // Advance interpolation + smoothing pipelines once per render frame.
    // Every in-frame consumer (camera matrix, crosshair projection, GUI
    // compensation) then reads identical cached values.
    void TickFrame();

    bool GetProcessedRotation(float& yaw, float& pitch, float& roll);
    bool GetPositionOffset(float& x, float& y, float& z);

    // Wall-clock dt of the most recent frame tick, for frame-rate-independent
    // smoothing of derived quantities (e.g. crosshair projection).
    float GetLastDeltaTime() const { return m_lastDeltaTime; }

    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(std::memory_order_relaxed); }

    Mod(const Mod&) = delete;
    Mod& operator=(const Mod&) = delete;

private:
    Mod() = default;
    ~Mod() = default;

    bool LoadConfig();

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};

    Config m_config;
    cameraunlock::UdpReceiver m_udpReceiver;
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session{m_udpReceiver};

    // Read on the render thread, toggled on the hotkey thread.
    std::atomic<bool> m_reticleEnabled{true};
    std::atomic<bool> m_worldSpaceYaw{true};

    cameraunlock::input::DeferredAction m_recenterRequested;
    cameraunlock::input::DeferredAction m_togglePositionRequested;

    uint64_t m_lastFrameTickTime = 0;
    float m_lastDeltaTime = 0.016f;
};

} // namespace RE4HT
