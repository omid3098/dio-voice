[CmdletBinding()]
param(
    [string]$BuildDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "out\build\x64-release")
)

$ErrorActionPreference = "Stop"
$dumpbin = Get-Command dumpbin -ErrorAction Stop
$targets = @(
    (Join-Path $BuildDir "bin\dio_voice_probe.exe"),
    (Join-Path $BuildDir "bin\dio_voice_core_tests.exe")
)

foreach ($target in $targets) {
    if (-not (Test-Path -LiteralPath $target)) {
        throw "Missing target: $target"
    }
    $imports = & $dumpbin.Source /dependents $target | Out-String
    if ($imports -match "(?i)cui|d2d1|dwrite") {
        throw "$target unexpectedly imports the CUI/rendering boundary."
    }
}

Write-Host "Core tests and probe are independent of CUI."
