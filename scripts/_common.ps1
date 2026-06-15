# Shared helpers for the BMU Test Tool build scripts.
# Dot-source this file from the other scripts: . "$PSScriptRoot\_common.ps1"

Set-StrictMode -Version Latest

# Repository root is the parent of the scripts/ directory.
$script:RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

function Get-RepoRoot {
    return $script:RepoRoot
}

# Locate vcvarsall.bat. Honors the VCVARSALL env var, then tries vswhere,
# then falls back to the Visual Studio 2022 BuildTools path used by the
# VS Code tasks.
function Find-VcVarsAll {
    if ($env:VCVARSALL -and (Test-Path -LiteralPath $env:VCVARSALL)) {
        return $env:VCVARSALL
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installPath = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null
        if ($installPath) {
            $candidate = Join-Path $installPath 'VC\Auxiliary\Build\vcvarsall.bat'
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    $fallback = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'
    if (Test-Path -LiteralPath $fallback) {
        return $fallback
    }

    throw "Could not find vcvarsall.bat. Set the VCVARSALL environment variable to its full path."
}

# Import the MSVC x64 developer environment into the current PowerShell session
# by running vcvarsall.bat in cmd and capturing the resulting environment.
#
# vcvarsall.bat can intermittently fail (e.g. exit code 255) when a security/EDR
# agent interferes with the spawned cmd.exe. The call is therefore retried, and
# on final failure the actual vcvarsall output is printed so the cause is visible
# instead of a bare exit code.
function Import-MsvcEnvironment {
    param(
        [string] $Arch = 'x64',
        [int]    $Retries = 2
    )

    $vcvars = Find-VcVarsAll
    Write-Host "Importing MSVC environment ($Arch) from:" -ForegroundColor Cyan
    Write-Host "  $vcvars" -ForegroundColor DarkGray

    # A sentinel lets us split vcvarsall's banner/diagnostics from the env dump,
    # so a partial/failed init cannot be mistaken for a valid environment.
    $sentinel = '___VCVARS_ENV_BELOW___'
    $output = $null
    $exitCode = 1

    for ($attempt = 1; $attempt -le ($Retries + 1); $attempt++) {
        $output = & cmd.exe /d /c "`"$vcvars`" $Arch && echo $sentinel && set" 2>&1
        $exitCode = $LASTEXITCODE
        if ($exitCode -eq 0) {
            break
        }
        if ($attempt -le $Retries) {
            Write-Warning "vcvarsall.bat failed (exit $exitCode); retrying ($attempt/$Retries)..."
            Start-Sleep -Milliseconds 500
        }
    }

    if ($exitCode -ne 0) {
        Write-Host ('=' * 72) -ForegroundColor Red
        Write-Host "vcvarsall.bat failed (exit $exitCode): $vcvars $Arch" -ForegroundColor Red
        Write-Host ('=' * 72) -ForegroundColor Red
        if ($output) {
            Write-Host '--- vcvarsall output ---' -ForegroundColor Red
            foreach ($line in $output) {
                Write-Host $line
            }
            Write-Host '--- end vcvarsall output ---' -ForegroundColor Red
        }
        else {
            Write-Host '(vcvarsall produced no output)' -ForegroundColor Red
        }
        throw "vcvarsall.bat failed with exit code $exitCode. See output above."
    }

    # Only parse lines after the sentinel as environment variables.
    $seenSentinel = $false
    $applied = 0
    foreach ($line in $output) {
        $text = [string]$line
        if (-not $seenSentinel) {
            if ($text.Trim() -eq $sentinel) {
                $seenSentinel = $true
            }
            continue
        }
        if ($text -match '^([^=]+)=(.*)$') {
            Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2]
            $applied++
        }
    }

    if (-not $seenSentinel -or $applied -eq 0) {
        Write-Host '--- vcvarsall output ---' -ForegroundColor Red
        foreach ($line in $output) {
            Write-Host $line
        }
        Write-Host '--- end vcvarsall output ---' -ForegroundColor Red
        throw "vcvarsall.bat reported success but no environment was captured. See output above."
    }
}

# Run a command and stop the script if it fails.
#
# Output (stdout + stderr) is streamed live to the console so build progress and
# any compiler diagnostics are visible as they happen. The combined output is
# also captured; on failure it is re-printed inside a clearly marked block so the
# actual error is never buried by the terminating PowerShell exception.
function Invoke-Checked {
    param(
        [Parameter(Mandatory)] [string] $Exe,
        [Parameter(ValueFromRemainingArguments)] [string[]] $Arguments
    )

    Write-Host "> $Exe $($Arguments -join ' ')" -ForegroundColor Yellow

    # Merge stderr into stdout (2>&1) so we capture everything, and Tee-Object
    # streams each line live while also collecting it for replay on failure.
    $captured = & $Exe @Arguments 2>&1 | Tee-Object -Variable lines
    $exitCode = $LASTEXITCODE

    if ($exitCode -ne 0) {
        Write-Host ''
        Write-Host ('=' * 72) -ForegroundColor Red
        Write-Host "Command failed (exit ${exitCode}): $Exe $($Arguments -join ' ')" -ForegroundColor Red
        Write-Host ('=' * 72) -ForegroundColor Red

        # Re-print the captured output so the error is unmissable even if the
        # live stream scrolled off or was swallowed by a calling context.
        if ($lines) {
            Write-Host '--- command output ---' -ForegroundColor Red
            foreach ($line in $lines) {
                Write-Host $line
            }
            Write-Host '--- end command output ---' -ForegroundColor Red
        }

        throw "Command failed (exit ${exitCode}): $Exe $($Arguments -join ' ')"
    }
}
