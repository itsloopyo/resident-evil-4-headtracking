#!/usr/bin/env pwsh
#Requires -Version 5.1
# Refresh the vendored REFramework nightly to the latest upstream that ships
# RE4.zip. Vendored zip is the install-time source of truth, so this is a
# manual bump-and-commit step. CI does not call this.
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

# REFramework is per-game: each nightly publishes RE9.zip, RE2.zip, RE4.zip ...
# A given nightly may skip our game; Refresh-VendoredLoader picks the newest
# release whose asset list matches AssetPattern, so we naturally tolerate gaps.
$out = Join-Path $projectDir 'vendor/reframework'
Refresh-VendoredLoader `
    -Name 'reframework' `
    -OutputDir $out `
    -OutputFileName 'RE4.zip' `
    -Owner 'praydog' -Repo 'REFramework-nightly' `
    -AssetPattern '^RE4\.zip$' `
    -AllowPrerelease `
    -LicenseUrl 'https://raw.githubusercontent.com/praydog/REFramework/master/LICENSE' | Out-Null

Write-Host ""
Write-Host "vendor/reframework refreshed. Review and commit." -ForegroundColor Green
