#!/usr/bin/env pwsh
#Requires -Version 5.1
# Refresh the vendored REFramework nightly to the latest upstream. The
# vendored zip is the install-time source of truth, so this is a manual
# bump-and-commit step. CI does not call this.
# See ~/.claude/CLAUDE.md "Vendoring Third-Party Dependencies".

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'
if (-not (Test-Path $module)) {
    throw "ModLoaderSetup.psm1 not found at $module. Run 'pixi run sync' to update the cameraunlock-core submodule."
}
Import-Module $module -Force

# Praydog ships a single universal REFramework.zip (dinput8.dll +
# reframework_revision.txt) that works across all supported RE Engine games,
# including RE4. We deliberately vendor the universal asset, NOT the per-game
# RE4.zip nightly: the per-game zip bundles VR runtime DLLs (openvr_api.dll,
# openxr_loader.dll) and VR autorun Lua that auto-enable REFramework's VR mod
# and fight our flat head tracking. The universal package carries neither, so
# the vendored zip is flatscreen by construction and lopari's manifest-mode
# deploy (which extracts the archive verbatim, with no strip step) is correct.
# We pin the on-disk filename to RE4.zip so deploy.ps1, package-release.ps1,
# install.cmd, and launcher-manifest.json can hardcode it.
$out = Join-Path $projectDir 'vendor/reframework'
Refresh-VendoredLoader `
    -Name 'reframework' `
    -OutputDir $out `
    -OutputFileName 'RE4.zip' `
    -Owner 'praydog' -Repo 'REFramework-nightly' `
    -AssetPattern '^REFramework\.zip$' `
    -AllowPrerelease `
    -LicenseUrl 'https://raw.githubusercontent.com/praydog/REFramework/master/LICENSE' | Out-Null

Write-Host ""
Write-Host "vendor/reframework refreshed. Review and commit." -ForegroundColor Green
