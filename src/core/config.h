#pragma once

#include <cameraunlock/reframework/plugin_config.h>

namespace RE4HT {

using Config = cameraunlock::reframework::PluginConfig;

// RE4's INI schema: the [Position] Invert keys it has always exposed, and the
// 2x position sensitivity default - RE Engine's native head-bob range is narrow
// enough that 1:1 reads as no movement at typical tracker range.
inline constexpr cameraunlock::reframework::PluginConfigSchema kConfigSchema{
    /*title*/ "RE4 Head Tracking",
    /*positionInvertKeys*/ true,
    /*flashlight*/ false,
    /*diagnosticMarkerKey*/ false,
    /*positionSensitivity*/ 2.0f,
    /*modId*/ "re4",
};

} // namespace RE4HT
