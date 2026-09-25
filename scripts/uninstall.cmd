@echo off
:: ============================================
:: Resident Evil 4 - Uninstall
:: ============================================
:: Thin wrapper - uninstall body lives in cameraunlock-core/scripts/uninstall-body.cmd
:: (one body, framework-aware via FRAMEWORK_TYPE).

:: --- CONFIG BLOCK ---
set "GAME_ID=resident-evil-4"
set "MOD_DISPLAY_NAME=RE4 Head Tracking"
set "MOD_DLLS=RE4HeadTracking.dll HeadTracking.ini"
set "MOD_INTERNAL_NAME=RE4HeadTracking"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=REFramework"
set "LEGACY_DLLS="
:: Files install.cmd seeded write-if-absent. MUST list the same names as
:: install.cmd's MOD_SEED_FILES, or an uninstall leaves the mod's config behind.
set "MOD_SEED_FILES="
:: Config files the uninstall leaves in place so the player's settings survive a
:: reinstall: paths relative to the game folder, quoted when one holds a space.
:: Keep the line when it is blank, or the list another mod's uninstall.cmd set
:: in the same console is used instead.
set "PRESERVE_FILES="

set "MANAGED_SUBFOLDER="
set "ASSEMBLY_DLL="
set "MANAGED_EXTRAS="
set "ASI_LOADER_NAME=winmm.dll"
:: Not used by this mod. Set blank so a value another mod's wrapper left in
:: the same console does not reach the body.
set "PLUGIN_SUBFOLDER="
set "MOD_LEFTOVERS="
set "ROOT_EXTRAS="
set "USER_FOLDER_EXTRAS="
set "PATCH_MARKER="
set "SHIM_MARKER="
set "SHIM_MARKER_ALT="
set "ASI_SUBDIR="
set "UE4_BINARIES_RELDIR="
:: --- END CONFIG BLOCK ---

:: Pin delayed expansion off before `%*` is expanded on the `call` below.
:: Under `cmd /V:ON`, or with DelayedExpansion=1 in
:: HKCU\Software\Microsoft\Command Processor, cmd.exe eats a `!` out of the
:: expanded line, and a real game path like C:\Games\Oh! My Game reaches the
:: body already mangled. The body pins expansion off at its own outer scope
:: too, but that is one `call` too late to save the argument it was handed.
setlocal disabledelayedexpansion

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\uninstall-body.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\uninstall-body.cmd"
if not exist "%_BODY%" (
    echo ERROR: uninstall-body.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%