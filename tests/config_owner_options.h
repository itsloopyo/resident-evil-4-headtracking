#pragma once

#include "core/config.h"

#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/reframework/plugin_config_table.h>

#include <string>

namespace RE4HT::testing {

inline constexpr wchar_t kConfigFileName[] = L"CameraUnlock.ini";
inline constexpr wchar_t kLegacyFileName[] = L"HeadTracking.ini";

inline cameraunlock::config::RenderHeader Header() {
    return cameraunlock::config::RenderHeader{kGameName};
}

// The owner core's PluginMod builds for this plugin with canonicalConfig
// (PluginMod::LoadCanonicalConfig), for the plugin folder `dir`: CameraUnlock.ini there,
// importing HeadTracking.ini beside it. A test passes DefaultsFile::At with a scratch path.
inline cameraunlock::config::ConfigOwnerOptions<Config> OwnerOptions(const std::wstring& dir,
                                                                     cameraunlock::config::DefaultsFile defaults) {
    cameraunlock::config::ConfigOwnerOptions<Config> options;
    options.path = dir + L"\\" + kConfigFileName;
    options.table = cameraunlock::reframework::PluginConfigTable(kConfigSchema);
    options.import = cameraunlock::reframework::PluginConfigLegacyImport(kConfigSchema);
    options.legacy_path = dir + L"\\" + kLegacyFileName;
    options.header = Header();
    options.defaults = std::move(defaults);
    return options;
}

}  // namespace RE4HT::testing
