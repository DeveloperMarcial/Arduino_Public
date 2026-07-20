[CmdletBinding()]
param(
    [ValidateSet("All", "Full", "Resume")]
    [string]$Mode = "All",
    [string]$Python = "python",
    [ValidateRange(1, 65535)]
    [int]$Port = 8080,
    [string]$Storage = ".portenta-sim",
    [ValidateRange(1, 30)]
    [int]$StartupTimeoutSeconds = 10,
    [switch]$AutoCloseSimulatorTerminal
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$uploaderScript = Join-Path $projectRoot "tools\esp32_uploader.py"
$simulatorTerminalScript = Join-Path $projectRoot "Start-JudgeSimulatorTerminal.ps1"
$storagePath = if ([System.IO.Path]::IsPathRooted($Storage)) {
    [System.IO.Path]::GetFullPath($Storage)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $projectRoot $Storage))
}
$baseUrl = "http://127.0.0.1:$Port"
$controlDirectory = Join-Path (
    [System.IO.Path]::GetTempPath()
) "flashbridge-$PID-$([Guid]::NewGuid().ToString('N'))"

$fullImages = @(
    "bootloader:$(Join-Path $projectRoot 'images\9ADE7FFE281152CE1367D725B05AA39B\Blinky_1000ms.ino.bootloader.bin')",
    "partitions:$(Join-Path $projectRoot 'images\9ADE7FFE281152CE1367D725B05AA39B\Blinky_1000ms.ino.partitions.bin')",
    "app0:$(Join-Path $projectRoot 'images\9ADE7FFE281152CE1367D725B05AA39B\Blinky_1000ms.ino.bin')"
)
$resumeImages = @(
    "bootloader:$(Join-Path $projectRoot 'images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.bootloader.bin')",
    "partitions:$(Join-Path $projectRoot 'images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.partitions.bin')",
    "app0:$(Join-Path $projectRoot 'images\A6205501DBB2B531B3C95BA725DCEFF2\Blinky_500ms.ino.bin')"
)

function Write-DemoHeading {
    param([string]$Text)

    Write-Host ""
    Write-Host ("=" * 72) -ForegroundColor Cyan
    Write-Host "TERMINAL 2 - SIMULATION UPLOADER: $Text" -ForegroundColor Cyan
    Write-Host "No Portenta or ESP32 hardware will be accessed or flashed." -ForegroundColor Cyan
    Write-Host ("=" * 72) -ForegroundColor Cyan
}

function Assert-File {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file was not found: $Path"
    }
}

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

function Wait-ForSignal {
    param(
        [string]$Name,
        [System.Diagnostics.Process]$TerminalProcess
    )

    $signalPath = Join-Path $controlDirectory $Name
    $errorPath = Join-Path $controlDirectory "error.txt"
    $deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $signalPath) {
            return
        }
        if (Test-Path -LiteralPath $errorPath) {
            throw (Get-Content -LiteralPath $errorPath -Raw)
        }
        if ($TerminalProcess.HasExited) {
            throw "The simulator terminal exited unexpectedly."
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for simulator signal '$Name'."
}

function Start-SimulatorTerminal {
    if (Test-LocalPort) {
        throw "Port $Port is already in use. Stop the existing server or use -Port."
    }

    New-Item -ItemType Directory -Path $controlDirectory -Force | Out-Null
    New-Item -ItemType Directory -Path $storagePath -Force | Out-Null
    $powerShellExecutable = if ($PSVersionTable.PSEdition -eq "Core") {
        Join-Path $PSHOME "pwsh.exe"
    } else {
        Join-Path $PSHOME "powershell.exe"
    }
    $arguments = @(
        "-NoLogo",
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $simulatorTerminalScript,
        "-Python", $script:pythonExecutable,
        "-Port", "$Port",
        "-Storage", $storagePath,
        "-ControlDirectory", $controlDirectory,
        "-StartupTimeoutSeconds", "$StartupTimeoutSeconds"
    )
    if ($AutoCloseSimulatorTerminal) {
        $arguments += "-AutoClose"
    }
    $argumentLine = ($arguments | ForEach-Object { ConvertTo-ProcessArgument $_ }) -join " "

    Write-Host "Opening Terminal 1 for live simulator output..." -ForegroundColor Cyan
    $terminalProcess = Start-Process `
        -FilePath $powerShellExecutable `
        -ArgumentList $argumentLine `
        -WorkingDirectory $projectRoot `
        -PassThru
    Wait-ForSignal -Name "ready.signal" -TerminalProcess $terminalProcess
    Write-Host "Terminal 1 simulator ready: $baseUrl" -ForegroundColor Green
    return $terminalProcess
}

function Restart-SimulatorTerminal {
    param([System.Diagnostics.Process]$TerminalProcess)

    Remove-Item `
        -LiteralPath (Join-Path $controlDirectory "restart.complete") `
        -Force `
        -ErrorAction SilentlyContinue
    Set-Content `
        -LiteralPath (Join-Path $controlDirectory "restart.request") `
        -Value "restart"
    Wait-ForSignal -Name "restart.complete" -TerminalProcess $TerminalProcess
    Write-Host "Terminal 1 restarted with the same persistent storage." -ForegroundColor Green
}

function Stop-SimulatorTerminal {
    param([System.Diagnostics.Process]$TerminalProcess)

    if ($null -eq $TerminalProcess -or $TerminalProcess.HasExited) {
        return
    }
    Set-Content `
        -LiteralPath (Join-Path $controlDirectory "stop.request") `
        -Value "stop"
    try {
        Wait-ForSignal -Name "stop.complete" -TerminalProcess $TerminalProcess
    } catch {
        Write-Warning $_
    }
}

function Get-UploaderArguments {
    param(
        [string[]]$Images,
        [int]$ChunkSize,
        [string]$SessionId,
        [switch]$Resume
    )

    $arguments = @(
        "-u",
        $uploaderScript,
        "--host", "127.0.0.1",
        "--port", "$Port",
        "--target", "arduino_nano_esp32",
        "--baud", "460800",
        "--chunk-size", "$ChunkSize",
        "--poll-interval", "0.2",
        "--no-narration"
    )
    if ($SessionId) {
        $arguments += @("--session-id", $SessionId)
    }
    if ($Resume) {
        $arguments += "--resume"
    }
    foreach ($image in $Images) {
        $arguments += @("--image", $image)
    }
    return $arguments
}

function Invoke-Uploader {
    param([string[]]$Arguments)

    $script:lastChunkTime = $null
    & $script:pythonExecutable @Arguments | ForEach-Object {
        $line = $_.ToString()
        if ($line -match '^total time:\s+([0-9.]+) seconds$') {
            $script:lastChunkTime = [double]$Matches[1]
            Write-Host $line -ForegroundColor DarkCyan
        } else {
            Write-Host $line
        }
    }
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "The uploader failed with exit code $exitCode."
    }
}

function Start-Uploader {
    param([string[]]$Arguments)

    $argumentLine = ($Arguments | ForEach-Object { ConvertTo-ProcessArgument $_ }) -join " "
    return Start-Process `
        -FilePath $script:pythonExecutable `
        -ArgumentList $argumentLine `
        -WorkingDirectory $projectRoot `
        -NoNewWindow `
        -PassThru
}

function Stop-Uploader {
    param([System.Diagnostics.Process]$Process)

    if ($null -ne $Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force
        $Process.WaitForExit()
    }
}

function Get-KnownSessionIds {
    try {
        return @(
            (Invoke-RestMethod -Uri "$baseUrl/api/v1/sessions" -TimeoutSec 2).session_ids
        )
    } catch {
        return @()
    }
}

function Wait-ForPartialUpload {
    param(
        [System.Diagnostics.Process]$UploaderProcess,
        [string[]]$PreviousSessionIds
    )

    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    $sessionId = $null
    $imageName = "Blinky_500ms.ino.bin"
    $encodedImage = [Uri]::EscapeDataString($imageName)

    while ([DateTime]::UtcNow -lt $deadline) {
        if ($UploaderProcess.HasExited) {
            throw "The initial uploader exited before a partial upload could be captured."
        }
        if (-not $sessionId) {
            $currentIds = Get-KnownSessionIds
            $sessionId = @(
                $currentIds | Where-Object { $_ -notin $PreviousSessionIds }
            ) | Select-Object -Last 1
        }
        if ($sessionId) {
            try {
                $progress = Invoke-RestMethod `
                    -Uri "$baseUrl/api/v1/session/$sessionId/chunks/$encodedImage" `
                    -TimeoutSec 1
                if (
                    $progress.received_chunks -ge 120 -and
                    $progress.received_chunks -lt $progress.total_chunks
                ) {
                    return [pscustomobject]@{
                        SessionId = $sessionId
                        Received = [int]$progress.received_chunks
                        Total = [int]$progress.total_chunks
                        Image = $imageName
                    }
                }
            } catch {
                # The manifest or first chunks may not have reached the simulator yet.
            }
        }
        Start-Sleep -Milliseconds 25
    }
    throw "Timed out while waiting for a demonstrably partial upload."
}

function Invoke-FullDemo {
    Write-DemoHeading "FULL UPLOAD"
    Write-Host (
        "Observe the teal 'total time': Full uploads every image chunk."
    ) -ForegroundColor DarkCyan
    $arguments = Get-UploaderArguments `
        -Images $fullImages `
        -ChunkSize 1024 `
        -SessionId ""
    Invoke-Uploader $arguments
    $script:fullChunkTime = $script:lastChunkTime
    Write-Host "FULL UPLOAD SIMULATION PASSED" -ForegroundColor Green
}

function Invoke-ResumeDemo {
    param([System.Diagnostics.Process]$SimulatorTerminal)

    Write-DemoHeading "INTERRUPTED UPLOAD AND RESUME"
    Write-Host (
        "Observe the teal 'total time': Resume sends only missing chunks, " +
        "so it should be shorter than Full."
    ) -ForegroundColor DarkCyan
    $initialUploader = $null
    try {
        $knownSessionIds = Get-KnownSessionIds
        $initialArguments = Get-UploaderArguments `
            -Images $resumeImages `
            -ChunkSize 1024 `
            -SessionId ""
        $initialUploader = Start-Uploader $initialArguments
        $partial = Wait-ForPartialUpload `
            -UploaderProcess $initialUploader `
            -PreviousSessionIds $knownSessionIds
        Stop-Uploader $initialUploader

        Write-Host ""
        Write-Host "INTENTIONAL SIMULATED INTERRUPTION" -ForegroundColor Yellow
        Write-Host "Session ID: $($partial.SessionId)" -ForegroundColor Yellow
        Write-Host (
            "Persisted $($partial.Received) of $($partial.Total) chunks for " +
            "$($partial.Image)."
        ) -ForegroundColor Yellow
        Write-Host "Pausing 3 seconds so the interrupted state is visible..." -ForegroundColor Yellow
        Start-Sleep -Seconds 3
        Write-Host "Watch Terminal 1 restart with the same storage." -ForegroundColor Yellow

        Restart-SimulatorTerminal $SimulatorTerminal

        Write-Host ""
        Write-Host "Resuming missing chunks for session $($partial.SessionId)..." -ForegroundColor Cyan
        $resumeArguments = Get-UploaderArguments `
            -Images $resumeImages `
            -ChunkSize 1024 `
            -SessionId $partial.SessionId `
            -Resume
        Invoke-Uploader $resumeArguments
        $script:resumeChunkTime = $script:lastChunkTime
        if ($null -ne $script:fullChunkTime -and $null -ne $script:resumeChunkTime) {
            Write-Host (
                "Chunk-time comparison: Full $($script:fullChunkTime.ToString('0.00'))s; " +
                "Resume $($script:resumeChunkTime.ToString('0.00'))s."
            ) -ForegroundColor DarkCyan
        }
        Write-Host "RESUME UPLOAD SIMULATION PASSED" -ForegroundColor Green
    } finally {
        Stop-Uploader $initialUploader
    }
}

Assert-File $uploaderScript
Assert-File $simulatorTerminalScript
foreach ($imageSpec in @($fullImages + $resumeImages)) {
    Assert-File ($imageSpec.Substring($imageSpec.IndexOf(":") + 1))
}

$pythonCommand = Get-Command $Python -ErrorAction SilentlyContinue
if ($null -eq $pythonCommand) {
    throw "Python was not found: $Python"
}
$script:pythonExecutable = $pythonCommand.Source
& $script:pythonExecutable -c "import requests" 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "Python package 'requests' is missing. Run: python -m pip install -r tools/requirements.txt"
}

$simulatorTerminal = $null
$script:fullChunkTime = $null
$script:resumeChunkTime = $null
$script:lastChunkTime = $null
try {
    $simulatorTerminal = Start-SimulatorTerminal
    switch ($Mode) {
        "Full" {
            Invoke-FullDemo
        }
        "Resume" {
            Invoke-ResumeDemo $simulatorTerminal
        }
        "All" {
            Invoke-FullDemo
            Invoke-ResumeDemo $simulatorTerminal
        }
    }
    Write-Host ""
    Write-Host "ALL REQUESTED FLASHBRIDGE SIMULATIONS PASSED" -ForegroundColor Green
} finally {
    Stop-SimulatorTerminal $simulatorTerminal
    Remove-Item -LiteralPath $controlDirectory -Recurse -Force -ErrorAction SilentlyContinue
}
