#!/usr/bin/env pwsh
#Requires -Version 5.1
# Build and run the tests in a build tree of their own, so the normal build/ output (and anything
# the packager reads from it) is untouched. Every test target is built and every registered test
# runs - adding a target to CMakeLists is enough, there is no list to keep in step here.

[CmdletBinding()]
param(
    [string]$Config = 'Debug',
    # Build the test binaries and stop, for pixi run render-config.
    [switch]$BuildOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$BuildDir = Join-Path $ProjectRoot 'build-tests'

cmake -S $ProjectRoot -B $BuildDir -A x64 -DRE4HT_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

cmake --build $BuildDir --config $Config --target re4ht_tests
if ($LASTEXITCODE -ne 0) { throw "Test build failed ($LASTEXITCODE)" }
if ($BuildOnly) { return }

ctest --test-dir $BuildDir -C $Config --output-on-failure -R '^re4ht_'
if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)" }

Write-Host 'All tests passed' -ForegroundColor Green
