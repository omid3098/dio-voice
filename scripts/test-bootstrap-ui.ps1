[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$BootstrapPath
)

$ErrorActionPreference = "Stop"
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class DioBootstrapWindowProbe {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string className, string windowName);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(
        IntPtr window,
        StringBuilder className,
        int capacity);

    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(
        IntPtr window,
        uint message,
        IntPtr wParam,
        IntPtr lParam);
}
"@

$process = Start-Process -FilePath (Resolve-Path $BootstrapPath) `
    -PassThru -WindowStyle Hidden
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    $window = [IntPtr]::Zero
    do {
        if ($process.HasExited) {
            throw "Bootstrap exited before creating its UI: $($process.ExitCode)"
        }
        $window = [DioBootstrapWindowProbe]::FindWindow(
            "DioVoiceBootstrapWindow", "DIO Voice")
        if ($window -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)

    if ($window -eq [IntPtr]::Zero) {
        throw "Bootstrap did not expose the expected DIO Voice window."
    }
    $className = [Text.StringBuilder]::new(128)
    if ([DioBootstrapWindowProbe]::GetClassName(
            $window, $className, $className.Capacity) -le 0 -or
        $className.ToString() -ne "DioVoiceBootstrapWindow") {
        throw "Bootstrap window class changed: $className"
    }
    [void][DioBootstrapWindowProbe]::SendMessage(
        $window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    $exited = $process.WaitForExit(3000)
    if (-not $exited -or $process.ExitCode -ne 0) {
        $code = if ($process.HasExited) { $process.ExitCode } else { "running" }
        throw "Bootstrap window did not close cleanly (exit=$code)."
    }
    Write-Host "Bootstrap zero-argument UI smoke passed."
} finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
    $process.Dispose()
}
