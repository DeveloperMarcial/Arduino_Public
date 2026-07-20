[CmdletBinding()]
param(
    [string]$Python = "python",
    [ValidateRange(1, 65535)]
    [int]$Port = 8080
)

& (Join-Path $PSScriptRoot "Run-JudgeSimulation.ps1") `
    -Mode Resume `
    -Python $Python `
    -Port $Port
exit $LASTEXITCODE

