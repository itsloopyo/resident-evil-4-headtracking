#pragma once

#include <cstdint>

#include "constants.h"

namespace RE4HT {

struct Config {
    // Network
    uint16_t udpPort = DEFAULT_UDP_PORT;

    // Sensitivity
    float yawMultiplier = 1.0f;
    float pitchMultiplier = 1.0f;
    float rollMultiplier = 1.0f;

    // Hotkeys (Virtual Key codes)
    int toggleKey = DEFAULT_TOGGLE_KEY;
    int recenterKey = DEFAULT_RECENTER_KEY;
    int positionToggleKey = DEFAULT_POSITION_TOGGLE_KEY;
    int reticleToggleKey = DEFAULT_RETICLE_TOGGLE_KEY;
    int yawModeKey = DEFAULT_YAW_MODE_KEY;

    // Position (6DOF) — RE Engine's native head-bob range is narrow, so we scale 2x to match
    // player expectation for lean/peek at default tracker range. Same rationale as RE:Requiem.
    float positionSensitivityX = 2.0f;
    float positionSensitivityY = 2.0f;
    float positionSensitivityZ = 2.0f;
    float positionLimitX = 0.30f;
    float positionLimitY = 0.20f;
    float positionLimitZ = 0.40f;
    float positionLimitZBack = 0.10f;
    float positionSmoothing = 0.15f;
    bool positionInvertX = false;
    bool positionInvertY = false;
    bool positionInvertZ = false;
    bool positionEnabled = true;

    // Reticle
    bool reticleEnabled = true;

    // General
    bool autoEnable = true;
    bool worldSpaceYaw = true;

    bool Load(const char* path);
    bool Save(const char* path) const;
    void SetDefaults();

private:
    void Validate();
};

} // namespace RE4HT
