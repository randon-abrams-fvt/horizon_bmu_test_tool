<#
.SYNOPSIS
    Build the BMU Test Tool with CMake.

.DESCRIPTION
    Imports the MSVC x64 developer environment and runs the CMake build step
    (parallel) for the selected preset. The build preset targets 'install', so
    the runnable output lands under output/<buildType>/bin.

.PARAMETER BuildType
    'debug' or 'release'. Defaults to 'debug'.

.PARAMETER Configure
    Force the configure step before building. Configure also runs automatically
    if the build tree has not been configured yet.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -BuildType release
    .\build.ps1 -Configure
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string] $BuildType = 'debug',

    [switch] $Configure
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\_common.ps1"

$repoRoot = Get-RepoRoot
$preset   = "windows-$BuildType"
$cacheFile = Join-Path $repoRoot "build\$BuildType\CMakeCache.txt"

# Auto-configure if requested, or if the build tree has never been configured.
if ($Configure -or -not (Test-Path -LiteralPath $cacheFile)) {
    if (-not $Configure) {
        Write-Host "No CMake cache found for '$preset'; configuring first..." -ForegroundColor Cyan
    }
    & "$PSScriptRoot\config.ps1" -BuildType $BuildType
}

Import-MsvcEnvironment -Arch x64

Write-Host "Building preset '$preset'..." -ForegroundColor Cyan
Push-Location -LiteralPath $repoRoot
try {
    Invoke-Checked cmake --build --preset $preset --parallel
    Write-Host "Build done." -ForegroundColor Green
}
finally {
    Pop-Location
}
