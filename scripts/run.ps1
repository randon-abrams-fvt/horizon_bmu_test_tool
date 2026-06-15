<#
.SYNOPSIS
    Run the installed BMU Test Tool executable.

.DESCRIPTION
    Launches BmuTestTool.exe from output/<buildType>/bin with that directory as
    the working directory so it can find its descriptor, YAML, font, and DLLs.

.PARAMETER BuildType
    'debug' or 'release'. Defaults to 'debug'.

.EXAMPLE
    .\run.ps1
    .\run.ps1 -BuildType release
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
$binDir   = Join-Path $repoRoot "output\$BuildType\bin"
$exePath  = Join-Path $binDir 'BmuTestTool.exe'

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Executable not found at '$exePath'. Build it first: .\build.ps1 -BuildType $BuildType"
}

Write-Host "Running BmuTestTool ($BuildType)..." -ForegroundColor Cyan
$exit = 0
Push-Location -LiteralPath $binDir
try {
    & $exePath
    $exit = $LASTEXITCODE
}
finally {
    Pop-Location
}

Write-Host "BmuTestTool exited with code $exit." -ForegroundColor DarkGray
exit $exit
