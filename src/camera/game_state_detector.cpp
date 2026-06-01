#include "pch.h"
#include "game_state_detector.h"
#include "core/logger.h"

#include <reframework/API.hpp>

namespace RE4HT {

static const std::vector<void*> g_emptyArgs{};

static struct {
    bool inGameplay = false;
    uint64_t lastCheckTime = 0;
    static constexpr uint64_t CHECK_INTERVAL_MS = 100;

    bool typesInitialized = false;
    reframework::API::Method* getMainView = nullptr;
    reframework::API::Method* getPrimaryCamera = nullptr;

    // GuiManager-based game state detection
    reframework::API::Method* getIsPlayingEvent = nullptr;
    reframework::API::Method* getGuiOpenCloseData = nullptr;
    reframework::API::Method* getCurrActiveInputLevel = nullptr;
    bool guiMethodsAvailable = false;

    // CharacterManager — null player context = main menu / loading
    reframework::API::Method* getPlayerContextRef = nullptr;

    // Transition tracking for auto-recenter
    bool wasInGameplay = false;
    bool pendingRecenter = false;
} g_state;

static void* InvokePtr(reframework::API::Method* method, void* obj) {
    auto ret = method->invoke(reinterpret_cast<reframework::API::ManagedObject*>(obj), g_emptyArgs);
    return ret.ptr;
}

static void RefreshGameState() {
    uint64_t now = GetTickCount64();
    if (now - g_state.lastCheckTime < g_state.CHECK_INTERVAL_MS) return;
    g_state.lastCheckTime = now;

    const auto& api = reframework::API::get();
    if (!api) {
        g_state.inGameplay = false;
        return;
    }

    if (!g_state.typesInitialized) {
        g_state.typesInitialized = true;
        auto tdb = api->tdb();

        auto smType = tdb->find_type("via.SceneManager");
        if (smType) g_state.getMainView = smType->find_method("get_MainView");
        auto svType = tdb->find_type("via.SceneView");
        if (svType) g_state.getPrimaryCamera = svType->find_method("get_PrimaryCamera");

        // GuiManager-based detection (proven in RE4 modding community)
        auto guiType = tdb->find_type("chainsaw.GuiManager");
        if (guiType) {
            g_state.getIsPlayingEvent = guiType->find_method("get_IsPlayingEvent");
            g_state.getGuiOpenCloseData = guiType->find_method("get_GuiOpenCloseData");

            // Find CurrActiveInputLevel method on the GuiOpenCloseData type
            auto openCloseType = tdb->find_type("chainsaw.gui.GuiOpenCloseData");
            if (!openCloseType) openCloseType = tdb->find_type("chainsaw.GuiOpenCloseData");
            if (openCloseType) {
                g_state.getCurrActiveInputLevel = openCloseType->find_method("get_CurrActiveInputevel");
                if (!g_state.getCurrActiveInputLevel)
                    g_state.getCurrActiveInputLevel = openCloseType->find_method("get_CurrActiveInputLevel");
            }
        }

        // CharacterManager for player-exists check (null = main menu / loading)
        auto charType = tdb->find_type("chainsaw.CharacterManager");
        if (charType) {
            g_state.getPlayerContextRef = charType->find_method("getPlayerContextRef");
        }

        if (g_state.getIsPlayingEvent || g_state.getPlayerContextRef) {
            g_state.guiMethodsAvailable = true;
            Logger::Instance().Info("Game state detection: event=%p, playerCtx=%p, openClose=%p, inputLevel=%p",
                g_state.getIsPlayingEvent, g_state.getPlayerContextRef,
                g_state.getGuiOpenCloseData, g_state.getCurrActiveInputLevel);
        } else {
            Logger::Instance().Info("Game state detection: all lookups failed");
        }
    }

    // Determine gameplay state via tiered checks
    bool newState = false;

    do {
        // Tier 1: Camera exists?
        if (!g_state.getMainView || !g_state.getPrimaryCamera) break;

        auto sceneManager = api->get_native_singleton("via.SceneManager");
        if (!sceneManager) break;

        auto vmCtx = api->get_vm_context();
        auto mainView = g_state.getMainView->call<void*>(vmCtx, sceneManager);
        if (!mainView) break;

        auto camera = g_state.getPrimaryCamera->call<void*>(vmCtx, mainView);
        if (!camera) break;

        // Tier 2: GuiManager-based game state detection
        if (g_state.guiMethodsAvailable) {
            bool suppress = false;

            __try {
                // Null player context = main menu / loading
                if (g_state.getPlayerContextRef) {
                    auto charMgr = api->get_managed_singleton("chainsaw.CharacterManager");
                    if (!charMgr || !InvokePtr(g_state.getPlayerContextRef, charMgr)) {
                        suppress = true;
                        __leave;
                    }
                }

                auto guiMgr = api->get_managed_singleton("chainsaw.GuiManager");
                if (!guiMgr) { suppress = true; __leave; }

                if (g_state.getIsPlayingEvent) {
                    auto eventRet = g_state.getIsPlayingEvent->invoke(
                        reinterpret_cast<reframework::API::ManagedObject*>(guiMgr), g_emptyArgs);
                    if (eventRet.byte != 0) { suppress = true; __leave; }
                }

                // Non-zero input level = menu / pause / inventory
                if (g_state.getGuiOpenCloseData && g_state.getCurrActiveInputLevel) {
                    auto openCloseData = InvokePtr(g_state.getGuiOpenCloseData, guiMgr);
                    if (openCloseData) {
                        auto levelRet = g_state.getCurrActiveInputLevel->invoke(
                            reinterpret_cast<reframework::API::ManagedObject*>(openCloseData), g_emptyArgs);
                        if (levelRet.dword > 0) { suppress = true; __leave; }
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                suppress = false;
            }

            if (suppress) break;
        }

        newState = true;
    } while (false);

    g_state.inGameplay = newState;

    // Detect transition from non-gameplay to gameplay for auto-recenter
    if (g_state.inGameplay && !g_state.wasInGameplay) {
        g_state.pendingRecenter = true;
        Logger::Instance().Info("Game state: entered gameplay — pending recenter");
    } else if (!g_state.inGameplay && g_state.wasInGameplay) {
        Logger::Instance().Info("Game state: left gameplay");
    }
    g_state.wasInGameplay = g_state.inGameplay;
}

bool IsInGameplay() {
    RefreshGameState();
    return g_state.inGameplay;
}

bool ShouldRecenter() {
    if (g_state.pendingRecenter) {
        g_state.pendingRecenter = false;
        return true;
    }
    return false;
}

} // namespace RE4HT
