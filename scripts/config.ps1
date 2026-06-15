<#
.SYNOPSIS
    Configure the BMU Test Tool with CMake.

.DESCRIPTION
    Imports the MSVC x64 developer environment and runs the CMake configure
    step for the selected preset (windows-debug or windows-release).

.PARAMETER BuildType
    'debug' or 'release'. Defaults to 'debug'.

.EXAMPLE
    .\config.ps1
    .\config.ps1 -BuildType release
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string] $BuildType = 'debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\_common.ps1"

$repoRoot = Get-RepoRoot
$preset   = "windows-$BuildType"

Import-MsvcEnvironment -Arch x64

Write-Host "Configuring preset '$preset'..." -ForegroundColor Cyan
Push-Location -LiteralPath $repoRoot
try {
    Invoke-Checked cmake --preset $preset
    Write-Host "Configure done." -ForegroundColor Green
}
finally {
    Pop-Location
}
