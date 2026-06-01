[CmdletBinding()]
param([switch]$AllowDirty)
$ErrorActionPreference = 'Stop'
$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

$constantsPath = Join-Path $ProjectRoot 'src\core\constants.h'
$match = Select-String -Path $constantsPath -Pattern 'RE4HT_VERSION = "([^"]+)"' | Select-Object -First 1
if (-not $match) {
    throw "Could not extract RE4HT_VERSION from $constantsPath"
}
$version = $match.Matches[0].Groups[1].Value

Publish-NightlyBuild `
    -ModId 'resident-evil-4' `
    -ModName 'RE4HeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -AllowDirty:$AllowDirty
