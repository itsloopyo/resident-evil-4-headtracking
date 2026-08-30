#include "pch.h"

#include <reframework/API.hpp>

#include "camera/game_state_detector.h"
#include "camera/gui_compensation.h"
#include "core/config.h"

#include <cameraunlock/reframework/gameplay_gate.h>
#include <cameraunlock/reframework/gui_elements.h>
#include <cameraunlock/reframework/plugin_bootstrap.h>

namespace ref = cameraunlock::reframework;

namespace {

// RE4 Remake's game code lives under the `chainsaw` root namespace. Namespace
// changes and unknown controllers fall back to the hooker's TDB short-name scan
// and parent-chain walk.
const char* const kControllerTypeCandidates[] = {
    "chainsaw.PlayerCameraController",
    "chainsaw.CameraManager",
};

// Assumed range to the point being aimed at, in metres.
//
// Fixed, not measured. The reticle marks where the clean aim lands in the
// head-turned view, and that projection needs a range because the drawn frame
// comes from the leaned eye: the correction is the rotation term plus a
// parallax term of lean/range. Held constant, the parallax is exact at this
// range and drifts with the lean either side of it, crossing zero here.
//
// Requiem measures the range instead, with a physics cast whose collision-layer
// allow-list was derived from captures of that title. Doing the same here needs
// the same captures - the layers that stop a bullet are per-game, and a wrong
// one collapses the range onto a trigger volume the player is standing in,
// which oversizes the correction rather than removing it. Until those captures
// exist for this game, a constant that is right at conversational-to-room range
// beats a measurement that can be wrong by an order of magnitude.
constexpr float kAimDistanceMeters = 50.0f;

const ref::PluginBootstrapDescriptor kPlugin = [] {
    ref::PluginBootstrapDescriptor d;
    d.logTag = "RE4HT";
    d.mod.displayName = RE4HT::RE4HT_PLUGIN_NAME;
    d.mod.version = RE4HT::RE4HT_VERSION;
    d.mod.config = RE4HT::kConfigSchema;
    d.camera.controllerCandidateTypes = kControllerTypeCandidates;
    d.camera.controllerCandidateCount =
        static_cast<int>(std::size(kControllerTypeCandidates));
    // The chainsaw.* types exist in the TDB before gameplay starts, so the
    // candidate fast path hooks at init; the parent-chain walk needs a live
    // camera rig and runs from the gameplay retry if this misses.
    d.camera.hookControllerAtInit = true;
    d.camera.aimDistanceMeters = kAimDistanceMeters;
    d.camera.gate = RE4HT::GameplayGateInstance();
    d.camera.onInit = []() { ref::InitGuiMethods(); };
    d.preGuiDrawElement = &RE4HT::OnPreGuiDrawElement;
    return d;
}();

} // namespace

// --- REFramework plugin exports ---

extern "C" __declspec(dllexport)
void reframework_plugin_required_version(REFrameworkPluginVersion* version) {
    version->major = REFRAMEWORK_PLUGIN_VERSION_MAJOR;
    version->minor = REFRAMEWORK_PLUGIN_VERSION_MINOR;
    version->patch = REFRAMEWORK_PLUGIN_VERSION_PATCH;
    version->game_name = nullptr;
}

extern "C" __declspec(dllexport)
bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam* param) {
    if (!param) return false;
    return ref::InitializePlugin(param, kPlugin);
}
