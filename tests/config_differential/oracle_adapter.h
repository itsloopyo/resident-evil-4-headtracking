#pragma once

#include <cstdint>

// The published build's Config, copied out field by field so the differential test can read it
// without including the oracle's config.h, whose names clash with this build's.
// oracle_adapter.cpp is compiled with the oracle, where RE4HT is renamed to re4ht_oracle.

namespace oracle_api {

struct Config {
    uint16_t udpPort = 0;

    float yawMultiplier = 0.0f;
    float pitchMultiplier = 0.0f;
    float rollMultiplier = 0.0f;

    float localSmoothing = 0.0f;
    float remoteSmoothing = 0.0f;

    int toggleKey = 0;
    int positionToggleKey = 0;
    int reticleToggleKey = 0;
    int yawModeKey = 0;

    float positionSensitivityX = 0.0f;
    float positionSensitivityY = 0.0f;
    float positionSensitivityZ = 0.0f;
    float positionLimitX = 0.0f;
    float positionLimitY = 0.0f;
    float positionLimitZ = 0.0f;
    float positionLimitZBack = 0.0f;
    bool positionInvertX = false;
    bool positionInvertY = false;
    bool positionInvertZ = false;
    bool positionEnabled = false;

    bool reticleEnabled = false;

    bool autoEnable = false;
    bool worldSpaceYaw = false;
};

// A default-constructed Config of the published build.
Config Defaults();

enum class LoadStatus {
    // The file was read.
    Read,
    // There was no file, and the build wrote one of defaults.
    Created,
};

// The published build's Mod::LoadConfig on the file at `iniPath`: Config::Load, and when that
// finds no file, Config::SetDefaults and Config::Save.
LoadStatus LoadOrCreate(const char* iniPath, Config& out);

}  // namespace oracle_api
