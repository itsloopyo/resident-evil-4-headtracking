#include "pch.h"
#include "config.h"
#include "logger.h"

#include <cameraunlock/config/ini_reader.h>
#include <cameraunlock/protocol/port_utils.h>

#include <algorithm>

namespace RE4HT {

void Config::SetDefaults() {
    udpPort = DEFAULT_UDP_PORT;

    yawMultiplier = 1.0f;
    pitchMultiplier = 1.0f;
    rollMultiplier = 1.0f;

    localSmoothing = 0.0f;
    remoteSmoothing = 0.15f;

    toggleKey = DEFAULT_TOGGLE_KEY;
    positionToggleKey = DEFAULT_POSITION_TOGGLE_KEY;
    reticleToggleKey = DEFAULT_RETICLE_TOGGLE_KEY;
    yawModeKey = DEFAULT_YAW_MODE_KEY;

    positionSensitivityX = 2.0f;
    positionSensitivityY = 2.0f;
    positionSensitivityZ = 2.0f;
    positionLimitX = 0.30f;
    positionLimitY = 0.20f;
    positionLimitZ = 0.40f;
    positionLimitZBack = 0.10f;
    positionInvertX = false;
    positionInvertY = false;
    positionInvertZ = false;
    positionEnabled = true;

    reticleEnabled = true;
    autoEnable = true;
    worldSpaceYaw = true;
}

void Config::Validate() {
    yawMultiplier = std::clamp(yawMultiplier, 0.1f, 5.0f);
    pitchMultiplier = std::clamp(pitchMultiplier, 0.1f, 5.0f);
    rollMultiplier = std::clamp(rollMultiplier, 0.0f, 2.0f);

    // Validation only: reject nonsense outside [0,1]. There is no minimum
    // floor - 0.0 means the user asked for zero smoothing and gets it.
    localSmoothing = std::clamp(localSmoothing, 0.0f, 1.0f);
    remoteSmoothing = std::clamp(remoteSmoothing, 0.0f, 1.0f);

    positionSensitivityX = std::clamp(positionSensitivityX, 0.1f, 10.0f);
    positionSensitivityY = std::clamp(positionSensitivityY, 0.1f, 10.0f);
    positionSensitivityZ = std::clamp(positionSensitivityZ, 0.1f, 10.0f);

    positionLimitX = std::clamp(positionLimitX, 0.01f, 2.0f);
    positionLimitY = std::clamp(positionLimitY, 0.01f, 2.0f);
    positionLimitZ = std::clamp(positionLimitZ, 0.01f, 2.0f);
    positionLimitZBack = std::clamp(positionLimitZBack, 0.01f, 2.0f);

    if (udpPort < 1024) {
        Logger::Instance().Warning("UDP port %d is in reserved range, using default %d",
                                   udpPort, DEFAULT_UDP_PORT);
        udpPort = DEFAULT_UDP_PORT;
    }
}

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// Smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
static void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
                                    const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    if (reader.ReadString(section, key, "").empty()) return;
    warned = true;
    Logger::Instance().Warning(
        "Config key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

bool Config::Load(const char* path) {
    SetDefaults();

    cameraunlock::IniReader reader;
    if (!reader.Open(path)) {
        Logger::Instance().Warning("Could not load config from %s, using defaults", path);
        return false;
    }

    int rawPort = reader.ReadInt("Network", "UDPPort", udpPort);
    bool portValid = false;
    udpPort = cameraunlock::NormalizeUdpPort(rawPort, DEFAULT_UDP_PORT, portValid);
    if (!portValid) {
        Logger::Instance().Warning("UDP port %d out of range (1024-65535), using default %d",
                                   rawPort, DEFAULT_UDP_PORT);
    }

    yawMultiplier = reader.ReadFloat("Sensitivity", "YawMultiplier", yawMultiplier);
    pitchMultiplier = reader.ReadFloat("Sensitivity", "PitchMultiplier", pitchMultiplier);
    rollMultiplier = reader.ReadFloat("Sensitivity", "RollMultiplier", rollMultiplier);

    localSmoothing = reader.ReadFloat("Smoothing", "LocalSmoothing", localSmoothing);
    remoteSmoothing = reader.ReadFloat("Smoothing", "RemoteSmoothing", remoteSmoothing);

    WarnRetiredSmoothingKey(reader, "Position", "Smoothing");

    toggleKey = reader.ReadHex("Hotkeys", "ToggleKey", toggleKey);
    positionToggleKey = reader.ReadHex("Hotkeys", "PositionToggleKey", positionToggleKey);
    reticleToggleKey = reader.ReadHex("Hotkeys", "ReticleToggleKey", reticleToggleKey);
    yawModeKey = reader.ReadHex("Hotkeys", "YawModeKey", yawModeKey);

    positionSensitivityX = reader.ReadFloat("Position", "SensitivityX", positionSensitivityX);
    positionSensitivityY = reader.ReadFloat("Position", "SensitivityY", positionSensitivityY);
    positionSensitivityZ = reader.ReadFloat("Position", "SensitivityZ", positionSensitivityZ);
    positionLimitX = reader.ReadFloat("Position", "LimitX", positionLimitX);
    positionLimitY = reader.ReadFloat("Position", "LimitY", positionLimitY);
    positionLimitZ = reader.ReadFloat("Position", "LimitZ", positionLimitZ);
    positionLimitZBack = reader.ReadFloat("Position", "LimitZBack", positionLimitZBack);
    positionInvertX = reader.ReadBool("Position", "InvertX", positionInvertX);
    positionInvertY = reader.ReadBool("Position", "InvertY", positionInvertY);
    positionInvertZ = reader.ReadBool("Position", "InvertZ", positionInvertZ);
    positionEnabled = reader.ReadBool("Position", "Enabled", positionEnabled);

    reticleEnabled = reader.ReadBool("Reticle", "Enabled", reticleEnabled);
    autoEnable = reader.ReadBool("General", "AutoEnable", autoEnable);
    worldSpaceYaw = reader.ReadBool("General", "WorldSpaceYaw", worldSpaceYaw);

    Validate();
    Logger::Instance().Info("Config loaded from %s", path);
    return true;
}

bool Config::Save(const char* path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        Logger::Instance().Error("Failed to save config to %s", path);
        return false;
    }

    file << "; RE4 Head Tracking Configuration\n";
    file << "; Delete this file to reset to defaults\n\n";

    file << "[Network]\n";
    file << "; UDP port for OpenTrack data (default: 4242)\n";
    file << "UDPPort=" << udpPort << "\n\n";

    file << "[Sensitivity]\n";
    file << "; Rotation sensitivity multipliers (1.0 = 1:1)\n";
    file << "YawMultiplier=" << yawMultiplier << "\n";
    file << "PitchMultiplier=" << pitchMultiplier << "\n";
    file << "RollMultiplier=" << rollMultiplier << "\n\n";

    file << "[Smoothing]\n";
    file << "; Smoothing applied when the tracker runs on this machine (loopback).\n";
    file << "; 0 = no smoothing, 1 = heavy. Covers rotation and position.\n";
    file << "LocalSmoothing=" << localSmoothing << "\n";
    file << "; Smoothing applied when the tracker is a remote device on the network.\n";
    file << "; 0 = no smoothing, 1 = heavy. Covers rotation and position.\n";
    file << "RemoteSmoothing=" << remoteSmoothing << "\n\n";

    file << "[Position]\n";
    file << "; Position tracking sensitivity (0.1-10.0, higher = more movement)\n";
    file << "SensitivityX=" << positionSensitivityX << "\n";
    file << "SensitivityY=" << positionSensitivityY << "\n";
    file << "SensitivityZ=" << positionSensitivityZ << "\n";
    file << "; Position limits in meters\n";
    file << "LimitX=" << positionLimitX << "\n";
    file << "LimitY=" << positionLimitY << "\n";
    file << "LimitZ=" << positionLimitZ << "\n";
    file << "LimitZBack=" << positionLimitZBack << "\n";
    file << "InvertX=" << (positionInvertX ? "true" : "false") << "\n";
    file << "InvertY=" << (positionInvertY ? "true" : "false") << "\n";
    file << "InvertZ=" << (positionInvertZ ? "true" : "false") << "\n";
    file << "Enabled=" << (positionEnabled ? "true" : "false") << "\n\n";

    file << "[Hotkeys]\n";
    file << "; Virtual key codes (hex)\n";
    file << "ToggleKey=0x" << std::hex << toggleKey << "           ; End\n";
    file << "PositionToggleKey=0x" << std::hex << positionToggleKey << "    ; Page Up\n";
    file << "ReticleToggleKey=0x" << std::hex << reticleToggleKey << "     ; Insert\n";
    file << "YawModeKey=0x" << std::hex << yawModeKey << "          ; Page Down\n\n";

    file << "[Reticle]\n";
    file << "Enabled=" << (reticleEnabled ? "true" : "false") << "\n\n";

    file << "[General]\n";
    file << "AutoEnable=" << (autoEnable ? "true" : "false") << "\n";
    file << "; World-space yaw locks horizon (true) vs. camera-local yaw follows camera pitch (false)\n";
    file << "WorldSpaceYaw=" << (worldSpaceYaw ? "true" : "false") << "\n";

    file.close();
    Logger::Instance().Info("Config saved to %s", path);
    return true;
}

} // namespace RE4HT
