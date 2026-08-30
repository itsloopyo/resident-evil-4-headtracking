#include "pch.h"
#include "game_state_detector.h"

#include <cameraunlock/reframework/gameplay_gate.h>
#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/managed_utils.h>

#include <reframework/API.hpp>

namespace RE4HT {

namespace ref = cameraunlock::reframework;

// RE4 Remake (chainsaw.*) game-state signals:
//   CharacterManager.getPlayerContextRef()      null  => main menu / loading
//   GuiManager.get_IsPlayingEvent()             true  => cutscene
//   GuiOpenCloseData.CurrActiveInputLevel > 0         => menu / pause / inventory
//
// Named outright rather than probed. The generic manager probing the RE7/RE8/
// Requiem detectors use binds whichever candidate resolves first, which is the
// right trade only where nothing has been confirmed; these are the bindings the
// RE4 modding community established, and swapping them for a probe would trade
// a known type for a guess.
static constexpr const char* kCharacterManager = "chainsaw.CharacterManager";
static constexpr const char* kGuiManager = "chainsaw.GuiManager";

static struct {
    reframework::API::Method* getIsPlayingEvent = nullptr;
    reframework::API::Method* getGuiOpenCloseData = nullptr;
    reframework::API::Method* getCurrActiveInputLevel = nullptr;
    reframework::API::Method* getPlayerContextRef = nullptr;
    bool available = false;
} g_checks;

static void Discover() {
    auto tdb = reframework::API::get()->tdb();

    auto guiType = tdb->find_type(kGuiManager);
    if (guiType) {
        g_checks.getIsPlayingEvent = guiType->find_method("get_IsPlayingEvent");
        g_checks.getGuiOpenCloseData = guiType->find_method("get_GuiOpenCloseData");

        auto openCloseType = tdb->find_type("chainsaw.gui.GuiOpenCloseData");
        if (!openCloseType) openCloseType = tdb->find_type("chainsaw.GuiOpenCloseData");
        if (openCloseType) {
            // The engine ships the typo'd name on some builds and the corrected
            // one on others; both refer to the same property.
            g_checks.getCurrActiveInputLevel = openCloseType->find_method("get_CurrActiveInputevel");
            if (!g_checks.getCurrActiveInputLevel)
                g_checks.getCurrActiveInputLevel = openCloseType->find_method("get_CurrActiveInputLevel");
        }
    }

    auto charType = tdb->find_type(kCharacterManager);
    if (charType) g_checks.getPlayerContextRef = charType->find_method("getPlayerContextRef");

    g_checks.available = g_checks.getIsPlayingEvent || g_checks.getPlayerContextRef;

    if (g_checks.available) {
        ref::LogInfo("Game state detection: event=%p, playerCtx=%p, openClose=%p, inputLevel=%p",
            g_checks.getIsPlayingEvent, g_checks.getPlayerContextRef,
            g_checks.getGuiOpenCloseData, g_checks.getCurrActiveInputLevel);
    } else {
        ref::LogInfo("Game state detection: all lookups failed");
    }
}

// The managed calls, guarded together. A probe that faults reports no
// suppression rather than a state: the game is running, this detector is not,
// and dropping tracking on the detector's own failure would be the worse of the
// two errors.
static const char* SuppressReason(const reframework::API* api) {
    __try {
        if (g_checks.getPlayerContextRef) {
            auto charMgr = api->get_managed_singleton(kCharacterManager);
            if (!charMgr || !ref::CallMethod(g_checks.getPlayerContextRef, charMgr)) {
                return "no player (menu/loading)";
            }
        }

        auto guiMgr = api->get_managed_singleton(kGuiManager);
        if (!guiMgr) return "no GuiManager";

        if (g_checks.getIsPlayingEvent && ref::CallMethodBool(g_checks.getIsPlayingEvent, guiMgr)) {
            return "cutscene";
        }

        if (g_checks.getGuiOpenCloseData && g_checks.getCurrActiveInputLevel) {
            auto openCloseData = ref::CallMethod(g_checks.getGuiOpenCloseData, guiMgr);
            if (openCloseData) {
                auto levelRet = g_checks.getCurrActiveInputLevel->invoke(
                    reinterpret_cast<reframework::API::ManagedObject*>(openCloseData), ref::EmptyArgs());
                // InvokeRet's payload is a union: on a thrown managed exception
                // the return slot is never written, so dword carries whatever the
                // previous call left there. Reading it unchecked reports a menu
                // that is not open and drops tracking for as long as the throw
                // repeats. Not routed through CallMethodBool like the checks
                // above: this is an int32 level, and CallMethodBool reads the
                // low byte only.
                if (!levelRet.exception_thrown && levelRet.dword > 0) return "menu input level";
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

static bool Check(void* primaryCamera, bool diag, const char** reason) {
    (void)primaryCamera;
    (void)diag;
    if (!g_checks.available) return true;

    const char* suppress = SuppressReason(reframework::API::get().get());
    if (!suppress) return true;
    *reason = suppress;
    return false;
}

static ref::GameplayGate g_gate{&Discover, &Check};

ref::GameplayGate* GameplayGateInstance() { return &g_gate; }

bool IsInGameplay() { return g_gate.IsInGameplay(); }

} // namespace RE4HT
