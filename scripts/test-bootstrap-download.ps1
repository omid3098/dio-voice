[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BootstrapPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$BootstrapPath = (Resolve-Path -LiteralPath $BootstrapPath).Path
$Utf8NoBom = New-Object System.Text.UTF8Encoding -ArgumentList $false
$TestRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    "dio-bootstrap-download-" + [guid]::NewGuid().ToString("N") + " فارسی")
$CabBuildRoot = Join-Path ([System.IO.Path]::GetDirectoryName($BootstrapPath)) (
    "cab-fixture-" + [guid]::NewGuid().ToString("N"))
$PayloadSource = Join-Path $CabBuildRoot "dio-voice.exe"
$PayloadPath = Join-Path $CabBuildRoot "payload.cab"
$FixtureJobs = New-Object System.Collections.ArrayList

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Get-Sha256Hex {
    param([string]$Path)
    $stream = [System.IO.File]::OpenRead($Path)
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $digest = $sha256.ComputeHash($stream)
        return [System.BitConverter]::ToString($digest).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $sha256.Dispose()
        $stream.Dispose()
    }
}

function Get-FreeTcpPort {
    $listener = [System.Net.Sockets.TcpListener]::new(
        [System.Net.IPAddress]::Loopback,
        0)
    try {
        $listener.Start()
        return ([System.Net.IPEndPoint]$listener.LocalEndpoint).Port
    }
    finally {
        $listener.Stop()
    }
}

function Start-FixtureServer {
    param(
        [ValidateSet("full", "interrupt", "resume", "corrupt")]
        [string]$Mode
    )

    $port = Get-FreeTcpPort
    $fixtureId = [guid]::NewGuid().ToString("N")
    $readyPath = Join-Path $TestRoot ("server-{0}.ready" -f $fixtureId)
    $logPath = Join-Path $TestRoot ("server-{0}.request.txt" -f $fixtureId)
    $job = Start-Job -ScriptBlock {
        param($Port, $PayloadPath, $Mode, $ReadyPath, $LogPath)
        Set-StrictMode -Version Latest
        $ErrorActionPreference = "Stop"
        $listener = [System.Net.Sockets.TcpListener]::new(
            [System.Net.IPAddress]::Loopback,
            [int]$Port)
        $client = $null
        $stream = $null
        try {
            $listener.Server.ExclusiveAddressUse = $true
            $listener.Start()
            [System.IO.File]::WriteAllText($ReadyPath, "ready")
            $client = $listener.AcceptTcpClient()
            $stream = $client.GetStream()
            $buffer = New-Object byte[] 4096
            $requestBytes = New-Object System.IO.MemoryStream
            try {
                $request = ""
                while ($requestBytes.Length -lt 65536) {
                    $read = $stream.Read($buffer, 0, $buffer.Length)
                    if ($read -eq 0) {
                        break
                    }
                    $requestBytes.Write($buffer, 0, $read)
                    $request = [System.Text.Encoding]::ASCII.GetString(
                        $requestBytes.GetBuffer(),
                        0,
                        [int]$requestBytes.Length)
                    if ($request.Contains("`r`n`r`n")) {
                        break
                    }
                }
            }
            finally {
                $requestBytes.Dispose()
            }
            [System.IO.File]::WriteAllText($LogPath, $request)

            $payload = [System.IO.File]::ReadAllBytes($PayloadPath)
            $offset = 0
            $bodyLength = $payload.Length
            $status = "200 OK"
            $extraHeaders = ""
            if ($Mode -eq "resume") {
                $range = [regex]::Match(
                    $request,
                    "(?im)^Range:\s*bytes=(\d+)-\s*$")
                $ifRange = [regex]::IsMatch(
                    $request,
                    '(?im)^If-Range:\s*"dio-test-etag"\s*$')
                if (-not $range.Success -or -not $ifRange) {
                    $status = "400 Bad Request"
                    $bodyLength = 0
                }
                else {
                    $offset = [int]$range.Groups[1].Value
                    if ($offset -le 0 -or $offset -ge $payload.Length) {
                        $status = "416 Range Not Satisfiable"
                        $bodyLength = 0
                    }
                    else {
                        $status = "206 Partial Content"
                        $bodyLength = $payload.Length - $offset
                        $last = $payload.Length - 1
                        $extraHeaders = "Content-Range: bytes $offset-$last/$($payload.Length)`r`n"
                    }
                }
            }
            elseif ($Mode -eq "interrupt") {
                $bodyLength = [int]($payload.Length / 2)
            }
            elseif ($Mode -eq "corrupt") {
                $payload[0] = $payload[0] -bxor 0xff
                $payload[$payload.Length - 1] =
                    $payload[$payload.Length - 1] -bxor 0xff
            }

            $declaredLength = if ($Mode -eq "interrupt") {
                $payload.Length
            }
            else {
                $bodyLength
            }
            $response = "HTTP/1.1 $status`r`n" +
                "Content-Length: $declaredLength`r`n" +
                'ETag: "dio-test-etag"' + "`r`n" +
                "Accept-Ranges: bytes`r`n" +
                $extraHeaders +
                "Connection: close`r`n`r`n"
            $responseBytes = [System.Text.Encoding]::ASCII.GetBytes($response)
            $stream.Write($responseBytes, 0, $responseBytes.Length)
            if ($bodyLength -gt 0) {
                $stream.Write($payload, $offset, $bodyLength)
            }
            $stream.Flush()
        }
        finally {
            if ($stream -ne $null) {
                $stream.Dispose()
            }
            if ($client -ne $null) {
                $client.Dispose()
            }
            $listener.Stop()
        }
    } -ArgumentList $port, $PayloadPath, $Mode, $readyPath, $logPath
    [void]$FixtureJobs.Add($job)

    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        if (Test-Path -LiteralPath $readyPath) {
            return [pscustomobject]@{
                Port = $port
                Job = $job
                RequestLog = $logPath
            }
        }
        if ($job.State -eq "Failed") {
            break
        }
        Start-Sleep -Milliseconds 50
    }
    try {
        Receive-Job -Job $job -ErrorAction Stop | Out-Null
    }
    finally {
        Remove-Job -Job $job -Force
        [void]$FixtureJobs.Remove($job)
    }
    throw "HTTP fixture server did not become ready."
}

function Complete-FixtureServer {
    param($Server)
    $completed = Wait-Job -Job $Server.Job -Timeout 15
    if ($null -eq $completed) {
        Stop-Job -Job $Server.Job
        throw "HTTP fixture server timed out."
    }
    try {
        Receive-Job -Job $Server.Job -ErrorAction Stop | Out-Null
        Assert-True ($Server.Job.State -eq "Completed") (
            "HTTP fixture server ended in state {0}." -f $Server.Job.State)
    }
    finally {
        Remove-Job -Job $Server.Job -Force
        [void]$FixtureJobs.Remove($Server.Job)
    }
}

function Write-TestManifest {
    param(
        [string]$Path,
        [int]$Port,
        [string]$PayloadHash,
        [long]$PayloadBytes,
        [long]$InstalledBytes
    )
    $manifest = [ordered]@{
        schema = 1
        version = "download-test"
        entrypoint = "versions/download-test/dio-voice.exe"
        components = @(
            [ordered]@{
                id = "core"
                url = "http://127.0.0.1:$Port/payload.cab"
                sha256 = $PayloadHash
                bytes = $PayloadBytes
                installed_bytes = $InstalledBytes
                target = "versions/download-test"
                profile = "all"
                archive = "cab"
            }
        )
    }
    [System.IO.File]::WriteAllText(
        $Path,
        ($manifest | ConvertTo-Json -Depth 6),
        $Utf8NoBom)
}

function New-InstallRoot {
    param([string]$Name)
    $root = Join-Path $TestRoot $Name
    [System.IO.Directory]::CreateDirectory($root) | Out-Null
    Copy-Item -LiteralPath $BootstrapPath -Destination (
        Join-Path $root "dio-voice.exe")
    return $root
}

function Invoke-BootstrapInstall {
    param(
        [string]$Root,
        [string]$ManifestPath,
        [bool]$AllowTestHttp
    )
    $executable = Join-Path $Root "dio-voice.exe"
    $arguments = "--install-only small --manifest-file `"$ManifestPath`""
    if ($AllowTestHttp) {
        $arguments += " --allow-test-http"
    }
    $startParameters = @{
        FilePath = $executable
        ArgumentList = $arguments
        PassThru = $true
        WindowStyle = "Hidden"
    }
    $process = Start-Process @startParameters
    try {
        if (-not $process.WaitForExit(20000)) {
            Stop-Process -Id $process.Id -Force
            throw "Bootstrap install timed out."
        }
        return $process.ExitCode
    }
    finally {
        $process.Dispose()
    }
}

function Assert-InstalledPayload {
    param(
        [string]$Root,
        [string]$ExpectedHash
    )
    $installed = Join-Path $Root ".dio\versions\download-test\dio-voice.exe"
    Assert-True (Test-Path -LiteralPath $installed -PathType Leaf) (
        "Installed entrypoint is missing: $installed")
    $actual = Get-Sha256Hex $installed
    Assert-True ($actual -ieq $ExpectedHash) "Installed payload hash differs."
    Assert-True (Test-Path -LiteralPath (
        Join-Path $Root ".dio\data\install.json") -PathType Leaf) (
        "Install marker is missing.")
    $cache = Join-Path $Root ".dio\cache"
    Assert-True (@(Get-ChildItem -LiteralPath $cache -Filter "core-*.cab" -File).Count -eq 0) (
        "Successful install retained the component cache.")
    Assert-True (@(Get-ChildItem -LiteralPath $cache -Filter "*.part" -File).Count -eq 0) (
        "Successful install retained a partial download.")
    Assert-True (@(Get-ChildItem -LiteralPath $cache -Filter "*.etag" -File).Count -eq 0) (
        "Successful install retained an ETag sidecar.")
}

try {
    [System.IO.Directory]::CreateDirectory($TestRoot) | Out-Null
    [System.IO.Directory]::CreateDirectory($CabBuildRoot) | Out-Null
    $payload = New-Object byte[] (512 * 1024)
    for ($index = 0; $index -lt $payload.Length; $index++) {
        $payload[$index] = [byte](($index * 31 + 17) % 251)
    }
    [System.IO.File]::WriteAllBytes($PayloadSource, $payload)
    $ddfPath = Join-Path $CabBuildRoot "payload.ddf"
    $ddf = @(
        ".OPTION EXPLICIT",
        ".Set Cabinet=ON",
        ".Set Compress=ON",
        ".Set CompressionType=MSZIP",
        ".Set MaxDiskSize=0",
        ".Set CabinetNameTemplate=payload.cab",
        ".Set DiskDirectoryTemplate=`"$CabBuildRoot`"",
        "`"$PayloadSource`" `"dio-voice.exe`"") -join "`r`n"
    [System.IO.File]::WriteAllText($ddfPath, $ddf + "`r`n", $Utf8NoBom)
    & "$env:SystemRoot\System32\makecab.exe" /V0 /F $ddfPath | Out-Null
    Assert-True ($LASTEXITCODE -eq 0 -and
        (Test-Path -LiteralPath $PayloadPath -PathType Leaf)) (
        "Could not build the CAB download fixture.")
    $installedHash = Get-Sha256Hex $PayloadSource
    $installedBytes = (Get-Item -LiteralPath $PayloadSource).Length
    $payloadHash = Get-Sha256Hex $PayloadPath
    $payloadBytes = (Get-Item -LiteralPath $PayloadPath).Length

    # The HTTP escape hatch must remain unavailable without the explicit,
    # install-only local-manifest test combination.
    $gatedRoot = New-InstallRoot "http-gate"
    $gatedManifest = Join-Path $TestRoot "http-gate.json"
    Write-TestManifest $gatedManifest 9 $payloadHash $payloadBytes $installedBytes
    $gatedExit = Invoke-BootstrapInstall $gatedRoot $gatedManifest $false
    Assert-True ($gatedExit -ne 0) "Production parsing accepted a plain HTTP payload URL."
    Assert-True (-not (Test-Path -LiteralPath (
        Join-Path $gatedRoot ".dio\versions\download-test\dio-voice.exe"))) (
        "Rejected HTTP manifest published a payload.")

    # Cold-cache download.
    $coldRoot = New-InstallRoot "cold cache فارسی"
    $coldManifest = Join-Path $TestRoot "cold.json"
    $coldServer = Start-FixtureServer "full"
    Write-TestManifest $coldManifest $coldServer.Port $payloadHash $payloadBytes $installedBytes
    $coldExit = Invoke-BootstrapInstall $coldRoot $coldManifest $true
    Complete-FixtureServer $coldServer
    Assert-True ($coldExit -eq 0) "Cold-cache install failed with exit code $coldExit."
    Assert-InstalledPayload $coldRoot $installedHash
    $coldRequest = Get-Content -LiteralPath $coldServer.RequestLog -Raw
    Assert-True (-not [regex]::IsMatch($coldRequest, "(?im)^Range:")) (
        "Cold-cache request unexpectedly used Range.")

    # The first response is intentionally truncated. The next run must retain
    # the .part file and resume it with both Range and If-Range/ETag.
    $resumeRoot = New-InstallRoot "resume فارسی"
    $resumeManifest = Join-Path $TestRoot "resume.json"
    $interruptServer = Start-FixtureServer "interrupt"
    Write-TestManifest $resumeManifest $interruptServer.Port $payloadHash $payloadBytes $installedBytes
    $interruptExit = Invoke-BootstrapInstall $resumeRoot $resumeManifest $true
    Complete-FixtureServer $interruptServer
    Assert-True ($interruptExit -ne 0) "Truncated download unexpectedly installed."
    $partParameters = @{
        LiteralPath = (Join-Path $resumeRoot ".dio\cache")
        Filter = "core-*.cab.part"
        File = $true
    }
    $partPath = Get-ChildItem @partParameters | Select-Object -First 1
    Assert-True ($null -ne $partPath) "Truncated download did not retain its .part file."
    $resumeOffset = $partPath.Length
    Assert-True ($resumeOffset -gt 0 -and $resumeOffset -lt $payloadBytes) (
        "Truncated .part size is not resumable: $resumeOffset.")
    Assert-True (Test-Path -LiteralPath ($partPath.FullName + ".etag")) (
        "Truncated download did not retain its ETag.")

    $resumeServer = Start-FixtureServer "resume"
    Write-TestManifest $resumeManifest $resumeServer.Port $payloadHash $payloadBytes $installedBytes
    $resumeExit = Invoke-BootstrapInstall $resumeRoot $resumeManifest $true
    Complete-FixtureServer $resumeServer
    Assert-True ($resumeExit -eq 0) "Resumed install failed with exit code $resumeExit."
    Assert-InstalledPayload $resumeRoot $installedHash
    $resumeRequest = Get-Content -LiteralPath $resumeServer.RequestLog -Raw
    Assert-True ([regex]::IsMatch(
        $resumeRequest,
        "(?im)^Range:\s*bytes=$resumeOffset-\s*$")) (
        "Resume request did not carry the expected byte offset $resumeOffset.")
    Assert-True ([regex]::IsMatch(
        $resumeRequest,
        '(?im)^If-Range:\s*"dio-test-etag"\s*$')) (
        "Resume request did not carry the saved ETag.")

    # A complete response with the expected length but wrong bytes must never
    # publish and must not poison the next retry's partial cache.
    $corruptRoot = New-InstallRoot "hash corrupt"
    $corruptManifest = Join-Path $TestRoot "corrupt.json"
    $corruptServer = Start-FixtureServer "corrupt"
    Write-TestManifest $corruptManifest $corruptServer.Port $payloadHash $payloadBytes $installedBytes
    $corruptExit = Invoke-BootstrapInstall $corruptRoot $corruptManifest $true
    Complete-FixtureServer $corruptServer
    Assert-True ($corruptExit -ne 0) "Hash-corrupt payload unexpectedly installed."
    Assert-True (-not (Test-Path -LiteralPath (
        Join-Path $corruptRoot ".dio\versions\download-test\dio-voice.exe"))) (
        "Hash-corrupt payload was published.")
    Assert-True (-not (Test-Path -LiteralPath (
        Join-Path $corruptRoot ".dio\data\install.json"))) (
        "Hash-corrupt payload produced an install marker.")
    $corruptCache = Join-Path $corruptRoot ".dio\cache"
    Assert-True (@(Get-ChildItem -LiteralPath $corruptCache -Filter "core-*.cab*" -File).Count -eq 0) (
        "Hash-corrupt payload remained in the component cache.")
    Assert-True (@(Get-ChildItem -LiteralPath $corruptCache -Filter "*.etag" -File).Count -eq 0) (
        "Hash-corrupt payload retained an ETag sidecar.")

    Write-Host "Bootstrap download self-test passed: Unicode CAB extraction, HTTP gate, cold cache, Range/ETag resume, and SHA-256 rejection."
}
finally {
    foreach ($job in @($FixtureJobs)) {
        if ($job.State -eq "Running" -or $job.State -eq "NotStarted") {
            Stop-Job -Job $job
        }
        Remove-Job -Job $job -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $TestRoot) {
        Remove-Item -LiteralPath $TestRoot -Recurse -Force
    }
    if (Test-Path -LiteralPath $CabBuildRoot) {
        Remove-Item -LiteralPath $CabBuildRoot -Recurse -Force
    }
}
