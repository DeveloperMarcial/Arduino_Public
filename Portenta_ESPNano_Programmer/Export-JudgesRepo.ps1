[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$Destination
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$sourceRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$sourceRepositoryRoot = [System.IO.Path]::GetFullPath(
    (Split-Path $sourceRoot -Parent)
)
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $publicRepositoryRoot = Join-Path (
        Split-Path $sourceRepositoryRoot -Parent
    ) "Arduino_Public"
    $Destination = Join-Path $publicRepositoryRoot "Portenta_ESPNano_Programmer"
}
$destinationRoot = [System.IO.Path]::GetFullPath($Destination)

$sourcePrefix = $sourceRoot.TrimEnd("\", "/") + [System.IO.Path]::DirectorySeparatorChar
if (
    $destinationRoot.Equals($sourceRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
    $destinationRoot.StartsWith($sourcePrefix, [System.StringComparison]::OrdinalIgnoreCase)
) {
    throw "Destination must be outside the source project: $destinationRoot"
}

$rootFiles = @(
    ".gitignore",
    "Example Flash Sequence.txt",
    "partitions_arduino_nano_esp32.csv",
    "Pinouts.drawio",
    "Pinouts.png",
    "platformio.ini",
    "README.md"
)
$includedDirectories = @(
    "examples/",
    "include/",
    "src/",
    "tests/",
    "tools/"
)

function Test-JudgeFile {
    param([string]$RelativePath)

    $normalized = $RelativePath.Replace("\", "/")
    if ($rootFiles -contains $normalized) {
        return $true
    }
    if ($normalized -match "^[^/]+\.(bat|ino|ps1)$") {
        return $true
    }
    foreach ($prefix in $includedDirectories) {
        if ($normalized.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    if (
        $normalized.StartsWith("images/", [System.StringComparison]::OrdinalIgnoreCase) -and
        $normalized.EndsWith(".bin", [System.StringComparison]::OrdinalIgnoreCase) -and
        -not $normalized.EndsWith(".merged.bin", [System.StringComparison]::OrdinalIgnoreCase)
    ) {
        return $true
    }
    return $false
}

$trackedFiles = @(& git -C $sourceRoot ls-files -- .)
if ($LASTEXITCODE -ne 0) {
    throw "Unable to enumerate tracked source files with git."
}

$filesToCopy = @(
    $trackedFiles |
        Where-Object { Test-JudgeFile $_ } |
        Sort-Object -Unique
)

# Include this exporter before its first commit so the initial export is reproducible.
$exporterName = Split-Path $PSCommandPath -Leaf
if (
    (Test-Path -LiteralPath (Join-Path $sourceRoot $exporterName)) -and
    $filesToCopy -notcontains $exporterName
) {
    $filesToCopy += $exporterName
}

if ($PSCmdlet.ShouldProcess($destinationRoot, "Create judges repository export")) {
    New-Item -ItemType Directory -Path $destinationRoot -Force | Out-Null
}

$copiedCount = 0
foreach ($relativePath in $filesToCopy) {
    $sourcePath = Join-Path $sourceRoot $relativePath
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Tracked source file is missing: $relativePath"
    }

    $destinationPath = Join-Path $destinationRoot $relativePath
    $destinationDirectory = Split-Path $destinationPath -Parent
    if ($PSCmdlet.ShouldProcess($destinationPath, "Copy judges file")) {
        New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
        Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
        $copiedCount += 1
    }
}

$licenseSource = Join-Path $sourceRepositoryRoot "LICENSE"
$licenseDestination = Join-Path $destinationRoot "LICENSE"
if (Test-Path -LiteralPath $licenseSource -PathType Leaf) {
    if ($PSCmdlet.ShouldProcess($licenseDestination, "Copy parent repository license")) {
        Copy-Item -LiteralPath $licenseSource -Destination $licenseDestination -Force
        $copiedCount += 1
    }
}

Write-Host ""
Write-Host "Judges export complete."
Write-Host "Source:      $sourceRoot"
Write-Host "Destination: $destinationRoot"
Write-Host "Files copied: $copiedCount"
Write-Host ""
Write-Host "Excluded: Git metadata, PlatformIO state, logs, caches, merged images,"
Write-Host "          ELF/map files, object/dependency files, and generated SDK output."
