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

set "MANAGED_SUBFOLDER="
set "ASSEMBLY_DLL="
set "MANAGED_EXTRAS="
set "ASI_LOADER_NAME=winmm.dll"
:: --- END CONFIG BLOCK ---

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
