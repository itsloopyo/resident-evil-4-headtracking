#!/usr/bin/env pwsh
#Requires -Version 5.1
param(
    [Parameter(Position=0)]
    [string]$Version = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$manifestPath = Join-Path $projectDir "manifest.json"
$cmakePath = Join-Path $projectDir "CMakeLists.txt"
$constantsPath = Join-Path $projectDir "src\core\constants.h"

Import-Module (Join-Path $projectDir "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force

function Get-CurrentVersion {
    $json = Get-Content $manifestPath -Raw | ConvertFrom-Json
    return $json.version
}

# manifest.json is the canonical version source. CMakeLists.txt project version
# and constants.h RE4HT_VERSION (compiled into the DLL) are kept in lockstep so
# the binary reports the same version users see in the release.
function Set-Version {
    param([string]$NewVersion)
    $json = Get-Content $manifestPath -Raw | ConvertFrom-Json
    $json.version = $NewVersion
    $json | ConvertTo-Json -Depth 10 | Set-Content $manifestPath -NoNewline

    (Get-Content $cmakePath -Raw) -replace 'project\(RE4HeadTracking VERSION \d+\.\d+\.\d+', "project(RE4HeadTracking VERSION $NewVersion" | Set-Content $cmakePath -NoNewline

    (Get-Content $constantsPath -Raw) -replace 'RE4HT_VERSION = "\d+\.\d+\.\d+"', "RE4HT_VERSION = `"$NewVersion`"" | Set-Content $constantsPath -NoNewline
}

Write-Host "=== RE4 Head Tracking Release ===" -ForegroundColor Cyan
Write-Host ""

$currentVersion = Get-CurrentVersion

if ([string]::IsNullOrWhiteSpace($Version)) {
    Write-Host "Current version: " -NoNewline -ForegroundColor Yellow
    Write-Host $currentVersion -ForegroundColor White
    Write-Host ""
    Write-Host "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>" -ForegroundColor Yellow
    exit 0
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1')
    exit $LASTEXITCODE
}

# Accept 'major', 'minor', 'patch', or a literal X.Y.Z. Resolve the bump
# keyword against the current version, then enforce strict X.Y.Z (the cmake /
# constants regex replaces and the tag name all assume no prerelease suffix).
try {
    $Version = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $currentVersion
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    Write-Host "Error: Invalid version format '$Version'" -ForegroundColor Red
    Write-Host "Use semantic versioning: X.Y.Z" -ForegroundColor Yellow
    exit 1
}

$tagName = "v$Version"

$currentBranch = git rev-parse --abbrev-ref HEAD
if ($currentBranch -ne "main") {
    Write-Host "Error: Must be on 'main' branch (currently on '$currentBranch')" -ForegroundColor Red
    exit 1
}

$status = git status --porcelain
if ($status) {
    Write-Host "Error: Working directory has uncommitted changes" -ForegroundColor Red
    exit 1
}

$existingTag = git tag -l $tagName
if ($existingTag) {
    Write-Host "Error: Tag '$tagName' already exists" -ForegroundColor Red
    exit 1
}

Write-Host "Current version: $currentVersion" -ForegroundColor Gray
Write-Host "New version:     $Version" -ForegroundColor Green
Write-Host ""

# Update version
Write-Host "Updating version to $Version..." -ForegroundColor Cyan
Set-Version $Version

# Update MOD_VERSION in install.cmd
$installCmdPath = Join-Path $scriptDir "install.cmd"
(Get-Content $installCmdPath -Raw) -replace 'set "MOD_VERSION=.*?"', "set `"MOD_VERSION=$Version`"" | Set-Content $installCmdPath -NoNewline

# Release-config build gate - never tag a build that doesn't compile.
Write-Host "Building release configuration..." -ForegroundColor Cyan
pixi run build-release
if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: release build failed" -ForegroundColor Red
    exit 1
}

# Generate CHANGELOG
Write-Host "Generating CHANGELOG..." -ForegroundColor Cyan
$changelogPath = Join-Path $projectDir "CHANGELOG.md"
$hasExistingTags = git tag -l 2>$null
if (-not $hasExistingTags) {
    $date = Get-Date -Format 'yyyy-MM-dd'
    $firstEntry = "# Changelog`n`n## [$Version] - $date`n`nFirst release.`n"
    Set-Content $changelogPath $firstEntry
} else {
    $changelogArgs = @{
        ChangelogPath = $changelogPath
        Version = $Version
        ArtifactPaths = @("src/", "cameraunlock-core/", "scripts/install.cmd", "scripts/uninstall.cmd")
    }
    New-ChangelogFromCommits @changelogArgs
}

# Commit
Write-Host "Committing version change..." -ForegroundColor Cyan
git add $manifestPath $cmakePath $constantsPath $changelogPath $installCmdPath
git commit -m "Release v$Version"

# Tag
Write-Host "Creating tag $tagName..." -ForegroundColor Cyan
git tag $tagName

# Push
Write-Host "Pushing to GitHub..." -ForegroundColor Cyan
git push origin main
git push origin $tagName

Write-Host ""
Write-Host "Release $tagName initiated!" -ForegroundColor Green
