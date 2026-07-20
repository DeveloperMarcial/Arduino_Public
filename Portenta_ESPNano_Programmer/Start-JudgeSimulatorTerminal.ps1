[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$Python,
    [Parameter(Mandatory)]
    [ValidateRange(1, 65535)]
    [int]$Port,
    [Parameter(Mandatory)]
    [string]$Storage,
    [Parameter(Mandatory)]
    [string]$ControlDirectory,
    [ValidateRange(1, 30)]
    [int]$StartupTimeoutSeconds = 10,
    [switch]$AutoClose
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$simulatorScript = Join-Path $projectRoot "tools\portenta_sim.py"
$baseUrl = "http://127.0.0.1:$Port"
$simulator = $null

function ConvertTo-ProcessArgument {
    param([string]$Value)

    if ($Value -notmatch '[\s"]') {
        return $Value
    }
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Test-LocalPort {
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $result = $client.BeginConnect("127.0.0.1", $Port, $null, $null)
        if (-not $result.AsyncWaitHandle.WaitOne(200)) {
            return $false
        }
        $client.EndConnect($result)
        return $true
    } catch {
        return $false
    } finally {
        $client.Close()
    }
}

function Stop-Simulator {
    if ($null -ne $script:simulator -and -not $script:simulator.HasExited) {
        Stop-Process -Id $script:simulator.Id -Force
        $script:simulator.WaitForExit()
    }
    $script:simulator = $null

    $deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (-not (Test-LocalPort)) {
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Simulator port $Port did not close."
}

function Start-Simulator {
    Write-Host ""
    Write-Host ("=" * 72) -ForegroundColor Cyan
    Write-Host "TERMINAL 1 - PORTENTA SIMULATOR (SIMULATION ONLY)" -ForegroundColor Cyan
    Write-Host "No Portenta or ESP32 hardware will be accessed or flashed." -ForegroundColor Cyan
    Write-Host ("=" * 72) -ForegroundColor Cyan

    $arguments = @(
        "-u",
        $simulatorScript,
        "--host", "127.0.0.1",
        "--port", "$Port",
        "--storage", $Storage,
        "--flash-delay", "0.15"
    )
    $argumentLine = ($arguments | ForEach-Object { ConvertTo-ProcessArgument $_ }) -join " "
    $script:simulator = Start-Process `
        -FilePath $Python `
        -ArgumentList $argumentLine `
        -WorkingDirectory $projectRoot `
        -NoNewWindow `
        -PassThru

    $deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($script:simulator.HasExited) {
            throw "The Portenta simulator exited during startup."
        }
        try {
            $health = Invoke-RestMethod -Uri "$baseUrl/health" -TimeoutSec 1
            if ($health.simulation -eq $true) {
                Start-Sleep -Milliseconds 200
                return
            }
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    throw "The Portenta simulator did not become ready at $baseUrl."
}

New-Item -ItemType Directory -Path $ControlDirectory -Force | Out-Null
try {
    Start-Simulator
    Set-Content -LiteralPath (Join-Path $ControlDirectory "ready.signal") -Value "ready"
    while ($true) {
        if (Test-Path -LiteralPath (Join-Path $ControlDirectory "stop.request")) {
            Stop-Simulator
            Set-Content -LiteralPath (Join-Path $ControlDirectory "stop.complete") -Value "stopped"
            break
        }
        if (Test-Path -LiteralPath (Join-Path $ControlDirectory "restart.request")) {
            Write-Host ""
            Write-Host "SIMULATION: restarting with preserved session storage..." -ForegroundColor Yellow
            Remove-Item `
                -LiteralPath (Join-Path $ControlDirectory "restart.request") `
                -Force
            Stop-Simulator
            Start-Simulator
            Set-Content `
                -LiteralPath (Join-Path $ControlDirectory "restart.complete") `
                -Value "restarted"
        }
        if ($null -ne $simulator -and $simulator.HasExited) {
            throw "The Portenta simulator exited unexpectedly."
        }
        Start-Sleep -Milliseconds 100
    }
} catch {
    Set-Content `
        -LiteralPath (Join-Path $ControlDirectory "error.txt") `
        -Value $_.Exception.Message
    Write-Error $_
} finally {
    Stop-Simulator
}

Write-Host ""
if ($AutoClose) {
    Write-Host "Simulator stopped. Terminal 1 will now close." -ForegroundColor Green
} else {
    Write-Host "Simulator stopped. Press Enter to close Terminal 1." -ForegroundColor Green
    [void](Read-Host)
}
