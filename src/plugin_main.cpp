#include "pch.h"

#include <reframework/API.hpp>

#include "core/mod.h"
#include "core/logger.h"
#include "camera/camera_hook.h"

#include <cameraunlock/input/hotkey_poller.h>

static cameraunlock::input::HotkeyPoller g_hotkeyPoller;

static void OnPreBeginRendering() {
    RE4HT::OnPreBeginRendering();
}

static void OnPostBeginRendering() {
    RE4HT::OnPostBeginRendering();
}

static bool OnPreGuiDrawElement(void* element, void* context) {
    return RE4HT::OnPreGuiDrawElement(element, context);
}

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

    // Initialize REFramework SDK wrapper
    reframework::API::initialize(param);

    // Set up logging via REFramework's log functions
    RE4HT::Logger::Instance().SetREFunctions(
        param->functions->log_info,
        param->functions->log_warn,
        param->functions->log_error
    );

    RE4HT::Logger::Instance().Info("RE4 Head Tracking v%s - Plugin loaded", RE4HT::RE4HT_VERSION);

    // Initialize mod (tracking pipeline, UDP receiver)
    if (!RE4HT::Mod::Instance().Initialize()) {
        RE4HT::Logger::Instance().Error("Mod initialization failed");
        return false;
    }

    param->functions->on_pre_application_entry("BeginRendering", OnPreBeginRendering);
    param->functions->on_post_application_entry("BeginRendering", OnPostBeginRendering);
    param->functions->on_pre_gui_draw_element(OnPreGuiDrawElement);

    // Set up hotkeys
    auto& config = RE4HT::Mod::Instance().GetConfig();

    g_hotkeyPoller.SetToggleKey(config.toggleKey, []() {
        RE4HT::Mod::Instance().Toggle();
    });
    g_hotkeyPoller.SetRecenterKey(config.recenterKey, []() {
        RE4HT::Mod::Instance().Recenter();
    });
    g_hotkeyPoller.AddHotkey(config.positionToggleKey, []() {
        RE4HT::Mod::Instance().TogglePosition();
    });
    g_hotkeyPoller.AddHotkey(config.reticleToggleKey, []() {
        RE4HT::Mod::Instance().ToggleReticle();
    });
    g_hotkeyPoller.AddHotkey(config.yawModeKey, []() {
        RE4HT::Mod::Instance().ToggleYawMode();
    });
    g_hotkeyPoller.Start();

    RE4HT::Logger::Instance().Info("Plugin initialization complete");
    return true;
}
