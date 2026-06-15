<#
.SYNOPSIS
    Clean BMU Test Tool build and install artifacts.

.DESCRIPTION
    Removes build/<buildType> and output/<buildType>. Use -All to remove the
    entire build/ and output/ directories (both debug and release).

.PARAMETER BuildType
    'debug' or 'release'. Defaults to 'debug'. Ignored when -All is set.

.PARAMETER All
    Remove the whole build/ and output/ directories.

.EXAMPLE
    .\clean.ps1
    .\clean.ps1 -BuildType release
    .\clean.ps1 -All
#>
[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string] $BuildType = 'debug',

    [switch] $All
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. "$PSScriptRoot\_common.ps1"

$repoRoot = Get-RepoRoot

if ($All) {
    $targets = @(
        (Join-Path $repoRoot 'build'),
        (Join-Path $repoRoot 'output')
    )
}
else {
    $targets = @(
        (Join-Path $repoRoot "build\$BuildType"),
        (Join-Path $repoRoot "output\$BuildType")
    )
}

$failed = $false
foreach ($target in $targets) {
    if (Test-Path -LiteralPath $target) {
        Write-Host "Removing $target" -ForegroundColor Yellow
        try {
            Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction Stop
        }
        catch {
            $failed = $true
            Write-Warning "Could not fully remove '$target': $($_.Exception.Message)"
            Write-Warning "Is BmuTestTool.exe still running? Close it and re-run clean."
        }
    }
    else {
        Write-Host "Skipping (not found) $target" -ForegroundColor DarkGray
    }
}

if ($failed) {
    Write-Host "Clean finished with errors." -ForegroundColor Red
    exit 1
}

Write-Host "Clean done." -ForegroundColor Green
