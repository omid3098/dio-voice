[CmdletBinding()]
param(
    [ValidatePattern("^[A-Za-z0-9._-]+$")]
    [string]$Version = "0.1.0-rc.1",
    [ValidateSet("Internal", "RC", "Stable")]
    [string]$Channel = "Internal",
    [string]$AssetBaseUrl,
    [string]$BuildDirectory,
    [string]$HarnessPath,
    [ValidatePattern("^[0-9A-Fa-f]{40}$")]
    [string]$SigningThumbprint,
    [switch]$SbomOnly,
    [switch]$VerifyRuntime,
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repo = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$outRoot = Join-Path $repo "out"
$utf8 = [Text.UTF8Encoding]::new($false)

function Write-Utf8File {
    param([string]$Path, [string]$Text)
    [IO.File]::WriteAllText($Path, $Text, $utf8)
}

function Assert-ChildPath {
    param([string]$Path, [string]$Parent = $outRoot)
    $full = [IO.Path]::GetFullPath($Path)
    $base = [IO.Path]::GetFullPath($Parent).TrimEnd("\") + "\"
    if (-not $full.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path escapes the owned output directory: $full"
    }
    return $full
}

function Reset-OwnedDirectory {
    param([string]$Path)
    $full = Assert-ChildPath $Path
    if (Test-Path -LiteralPath $full) {
        Remove-Item -LiteralPath $full -Recurse -Force
    }
    New-Item -ItemType Directory -Path $full -Force | Out-Null
    return $full
}

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-PinnedDependency {
    param([object]$Dependency)
    $downloads = Join-Path $outRoot "downloads\sources"
    New-Item -ItemType Directory -Path $downloads -Force | Out-Null
    $filename = [IO.Path]::GetFileName($Dependency.destination)
    $path = Join-Path $downloads $filename
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Invoke-WebRequest -UseBasicParsing -Uri $Dependency.url -OutFile $path
    }
    $item = Get-Item -LiteralPath $path
    if (($Dependency.PSObject.Properties.Name -contains "bytes" -and
            $item.Length -ne [long]$Dependency.bytes) -or
        (Get-Sha256 $path) -ne $Dependency.sha256) {
        throw "Pinned source failed integrity validation: $($Dependency.name)"
    }
    return $path
}

function Test-SafeRelativePath {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path) -or [IO.Path]::IsPathRooted($Path)) {
        return $false
    }
    foreach ($segment in ($Path -split "[\\/]")) {
        if (-not $segment -or $segment -eq "." -or $segment -eq ".." -or
            $segment.EndsWith(".", [StringComparison]::Ordinal) -or
            $segment.EndsWith(" ", [StringComparison]::Ordinal) -or
            $segment.IndexOfAny([char[]]'<>:"|?*') -ge 0 -or
            @($segment.ToCharArray() | Where-Object { [int]$_ -lt 32 }).Count -gt 0) {
            return $false
        }
        $base = $segment.Split('.', 2)[0].TrimEnd([char[]]@(' ', '.'))
        if ($base -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$') {
            return $false
        }
    }
    return $true
}

function Assert-TargetTopology {
    param([object[]]$Components, [string]$Profile)
    $selected = @($Components | Where-Object {
        $_.profile -eq "all" -or $_.profile -eq $Profile
    })
    for ($left = 0; $left -lt $selected.Count; $left++) {
        $a = $selected[$left].target.Replace("/", "\").TrimEnd("\")
        for ($right = $left + 1; $right -lt $selected.Count; $right++) {
            $b = $selected[$right].target.Replace("/", "\").TrimEnd("\")
            if ($a.Equals($b, [StringComparison]::OrdinalIgnoreCase)) {
                continue
            }
            if ($a.StartsWith("$b\", [StringComparison]::OrdinalIgnoreCase) -or
                $b.StartsWith("$a\", [StringComparison]::OrdinalIgnoreCase)) {
                throw "Nested publish targets are unsafe for $Profile`: $a and $b"
            }
        }
    }
}

function Get-TreeMetrics {
    param([string]$Root)
    $resolved = (Resolve-Path -LiteralPath $Root).Path
    $files = @(Get-ChildItem -LiteralPath $resolved -Recurse -File |
        Where-Object {
            $_.Extension -ne ".pyc" -and
            $_.FullName -notmatch "[\\/]__pycache__[\\/]"
        })
    $relativeFiles = [string[]]@($files | ForEach-Object {
        [IO.Path]::GetRelativePath($resolved, $_.FullName).Replace("\", "/")
    })
    [Array]::Sort($relativeFiles, [StringComparer]::Ordinal)
    $tree = [Security.Cryptography.IncrementalHash]::CreateHash(
        [Security.Cryptography.HashAlgorithmName]::SHA256)
    [uint64]$bytes = 0
    foreach ($relative in $relativeFiles) {
        $file = Get-Item -LiteralPath (
            Join-Path $resolved $relative.Replace("/", "\"))
        $line = "$(Get-Sha256 $file.FullName) $($file.Length) $relative`n"
        $tree.AppendData([Text.Encoding]::UTF8.GetBytes($line))
        $bytes += $file.Length
    }
    $digest = [Convert]::ToHexString(
        $tree.GetHashAndReset()).ToLowerInvariant()
    $tree.Dispose()
    return [pscustomobject]@{
        file_count = $relativeFiles.Count
        bytes = $bytes
        tree_sha256 = $digest
    }
}

function Test-ReleaseMetadata {
    param([switch]$RequireRuntime)
    $thirdParty = Get-Content -Raw -LiteralPath (
        Join-Path $repo "metadata\third-party.lock.json") | ConvertFrom-Json
    foreach ($dependency in $thirdParty.dependencies) {
        if (-not $dependency.name -or -not $dependency.version -or
            -not $dependency.url.StartsWith("https://") -or
            -not (Test-SafeRelativePath $dependency.destination) -or
            -not $dependency.license -or -not $dependency.scope -or
            -not ($dependency.PSObject.Properties.Name -contains "sha256")) {
            throw "Incomplete third-party lock entry: $($dependency.name)"
        }
        if ($dependency.sha256 -notmatch "^[0-9a-f]{64}$") {
            throw "Invalid SHA-256 for $($dependency.name)."
        }
        if ($dependency.PSObject.Properties.Name -contains "bytes" -and
            [long]$dependency.bytes -le 0) {
            throw "Invalid byte count for $($dependency.name)."
        }
        if ($dependency.PSObject.Properties.Name -contains "license_file" -and
            -not (Test-Path -LiteralPath (
                Join-Path $repo $dependency.license_file) -PathType Leaf)) {
            throw "Missing license file for $($dependency.name)."
        }
        if ($dependency.PSObject.Properties.Name -contains "notice_file" -and
            -not (Test-Path -LiteralPath (
                Join-Path $repo $dependency.notice_file) -PathType Leaf)) {
            throw "Missing notice file for $($dependency.name)."
        }
        if ($dependency.PSObject.Properties.Name -contains "trademark_notice_file" -and
            -not (Test-Path -LiteralPath (
                Join-Path $repo $dependency.trademark_notice_file) -PathType Leaf)) {
            throw "Missing trademark notice for $($dependency.name)."
        }
        if ($dependency.PSObject.Properties.Name -contains "base_license_file" -and
            -not (Test-Path -LiteralPath (
                Join-Path $repo $dependency.base_license_file) -PathType Leaf)) {
            throw "Missing base license for $($dependency.name)."
        }
        if ($dependency.PSObject.Properties.Name -contains "source_commit" -and
            $dependency.source_commit -notmatch "^[0-9a-f]{40}$") {
            throw "Invalid source commit for $($dependency.name)."
        }
        if ($dependency.PSObject.Properties.Name -contains "local_file") {
            $localFile = Join-Path $repo $dependency.local_file
            if (-not (Test-SafeRelativePath $dependency.local_file) -or
                -not ($dependency.PSObject.Properties.Name -contains "bytes") -or
                -not (Test-Path -LiteralPath $localFile -PathType Leaf) -or
                (Get-Item -LiteralPath $localFile).Length -ne [long]$dependency.bytes -or
                (Get-Sha256 $localFile) -ne $dependency.sha256) {
                throw "Bundled file differs from lock: $($dependency.name)"
            }
        }
        if ($dependency.PSObject.Properties.Name -contains "compiled_file") {
            if (-not (Test-SafeRelativePath $dependency.compiled_file) -or
                -not ($dependency.PSObject.Properties.Name -contains
                    "compiled_file_sha256") -or
                $dependency.compiled_file_sha256 -notmatch "^[0-9a-f]{64}$" -or
                -not ($dependency.PSObject.Properties.Name -contains
                    "transitive_lock_name")) {
                throw "Incomplete transitive build pin: $($dependency.name)"
            }
        }
    }
    foreach ($dependency in @($thirdParty.dependencies | Where-Object {
                $_.PSObject.Properties.Name -contains "contained_by"
            })) {
        if (@($thirdParty.dependencies | Where-Object {
                    $_.name -eq $dependency.contained_by
                }).Count -ne 1) {
            throw "Missing containing dependency for $($dependency.name)."
        }
    }
    $serialized = $thirdParty | ConvertTo-Json -Depth 8
    foreach ($forbidden in @("openwakeword", "chatterbox", "gbrain")) {
        if ($serialized.IndexOf($forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "Obsolete dependency remains in lock: $forbidden"
        }
    }

    $runtimeLock = Get-Content -Raw -LiteralPath (
        Join-Path $repo "metadata\runtime.lock.json") | ConvertFrom-Json
    foreach ($package in $runtimeLock.python_packages) {
        $packageLicense = $package.license_file.Replace("\", "/")
        $packageLicenseNeedsRuntime = $packageLicense.StartsWith(
            "runtime/", [StringComparison]::OrdinalIgnoreCase)
        if (-not $package.name -or -not $package.version -or
            -not $package.url.StartsWith("https://") -or
            [long]$package.bytes -le 0 -or
            $package.sha256 -notmatch "^[0-9a-f]{64}$" -or
            -not $package.license -or -not $package.license_file -or
            -not (Test-SafeRelativePath $package.license_file) -or
            ((-not $packageLicenseNeedsRuntime -or $RequireRuntime) -and
                -not (Test-Path -LiteralPath (
                    Join-Path $repo $package.license_file) -PathType Leaf))) {
            throw "Incomplete Python runtime lock entry: $($package.name)"
        }
        if ($package.PSObject.Properties.Name -contains "notice_file" -and
            (-not (Test-SafeRelativePath $package.notice_file) -or
             -not (Test-Path -LiteralPath (
                    Join-Path $repo $package.notice_file) -PathType Leaf))) {
            throw "Missing Python runtime notice: $($package.name)"
        }
    }
    foreach ($component in $runtimeLock.embedded_components) {
        $componentLicense = $component.license_file.Replace("\", "/")
        $componentLicenseNeedsRuntime = $componentLicense.StartsWith(
            "runtime/", [StringComparison]::OrdinalIgnoreCase)
        if (-not $component.name -or -not $component.version -or
            -not $component.url.StartsWith("https://") -or
            $component.sha256 -notmatch "^[0-9a-f]{64}$" -or
            [long]$component.bytes -le 0 -or
            -not (Test-SafeRelativePath $component.destination) -or
            -not $component.license -or -not $component.license_file -or
            -not (Test-SafeRelativePath $component.license_file) -or
            ((-not $componentLicenseNeedsRuntime -or $RequireRuntime) -and
                -not (Test-Path -LiteralPath (
                    Join-Path $repo $component.license_file) -PathType Leaf))) {
            throw "Incomplete embedded runtime lock entry: $($component.name)"
        }
        if ($component.PSObject.Properties.Name -contains "notice_file" -and
            (-not (Test-SafeRelativePath $component.notice_file) -or
             -not (Test-Path -LiteralPath (
                    Join-Path $repo $component.notice_file) -PathType Leaf))) {
            throw "Missing embedded runtime notice: $($component.name)"
        }
    }
    if (-not $RequireRuntime) { return $thirdParty }
    [uint64]$total = 0
    foreach ($expected in $runtimeLock.trees) {
        $actual = Get-TreeMetrics (Join-Path $repo "runtime\$($expected.path)")
        if ($actual.file_count -ne $expected.file_count -or
            $actual.bytes -ne $expected.bytes -or
            $actual.tree_sha256 -ne $expected.tree_sha256) {
            throw "Runtime tree differs from lock: $($expected.path); " +
                "expected $($expected.file_count)/$($expected.bytes)/$($expected.tree_sha256), " +
                "actual $($actual.file_count)/$($actual.bytes)/$($actual.tree_sha256)"
        }
        $total += $actual.bytes
    }
    if ($total -ne $runtimeLock.total_bytes) {
        throw "Runtime byte total differs from lock."
    }
    foreach ($package in $runtimeLock.forbidden_runtime_packages) {
        $normalized = $package.Replace("-", "_")
        if (Test-Path -LiteralPath (
                Join-Path $repo "runtime\piper\python\Lib\site-packages\$normalized")) {
            throw "Heavy runtime package remains: $package"
        }
    }
    foreach ($component in @($runtimeLock.embedded_components | Where-Object {
                $_.PSObject.Properties.Name -contains "contained_by"
            })) {
        $path = Join-Path $repo ($component.destination -replace "/", "\")
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-Item -LiteralPath $path).Length -ne [long]$component.bytes -or
            (Get-Sha256 $path) -ne $component.sha256) {
            throw "Embedded runtime file differs from lock: $($component.name)"
        }
    }
    return $thirdParty
}

function New-Cabinet {
    param(
        [string]$SourceDirectory,
        [string]$Destination,
        [string]$WorkDirectory,
        [string]$ArchivePrefix = ""
    )
    $source = (Resolve-Path -LiteralPath $SourceDirectory).Path
    $destinationFull = [IO.Path]::GetFullPath($Destination)
    $destinationDirectory = Split-Path -Parent $destinationFull
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    $files = @(Get-ChildItem -LiteralPath $source -Recurse -File)
    $relativeFiles = [string[]]@($files | ForEach-Object {
        [IO.Path]::GetRelativePath($source, $_.FullName).Replace("\", "/")
    })
    [Array]::Sort($relativeFiles, [StringComparer]::Ordinal)
    if ($relativeFiles.Count -eq 0) { throw "Cannot create an empty cabinet." }
    if ($ArchivePrefix -and -not (Test-SafeRelativePath $ArchivePrefix)) {
        throw "Unsafe cabinet prefix: $ArchivePrefix"
    }

    $ddf = Join-Path $WorkDirectory "$([IO.Path]::GetFileNameWithoutExtension($Destination)).ddf"
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add(".OPTION EXPLICIT")
    $lines.Add(".Set Cabinet=ON")
    $lines.Add(".Set Compress=ON")
    $lines.Add(".Set CompressionType=MSZIP")
    $lines.Add(".Set MaxDiskSize=0")
    $lines.Add(".Set CabinetNameTemplate=$([IO.Path]::GetFileName($destinationFull))")
    $lines.Add(".Set DiskDirectoryTemplate=`"$destinationDirectory`"")
    [uint64]$folderBytes = 0
    foreach ($relativePath in $relativeFiles) {
        $file = Get-Item -LiteralPath (
            Join-Path $source $relativePath.Replace("/", "\"))
        $relative = $relativePath.Replace("/", "\")
        if ($ArchivePrefix) {
            $relative = "$($ArchivePrefix.TrimEnd('\', '/'))\$relative"
        }
        if (-not (Test-SafeRelativePath $relative)) {
            throw "Unsafe cabinet path: $relative"
        }
        if ($folderBytes -gt 0 -and $folderBytes + $file.Length -gt 1800MB) {
            $lines.Add(".New Folder")
            $folderBytes = 0
        }
        $lines.Add("`"$($file.FullName)`" `"$relative`"")
        $folderBytes += $file.Length
    }
    Write-Utf8File $ddf (($lines -join "`r`n") + "`r`n")
    $makecabLog = "$ddf.log"
    & "$env:SystemRoot\System32\makecab.exe" /V0 /F $ddf *> $makecabLog
    if ($LASTEXITCODE -ne 0 -or
        -not (Test-Path -LiteralPath $destinationFull -PathType Leaf)) {
        $tail = Get-Content -LiteralPath $makecabLog -Tail 12 -ErrorAction SilentlyContinue
        throw "makecab failed for $Destination`n$($tail -join "`n")"
    }
    $cab = Get-Item -LiteralPath $destinationFull
    if ($cab.Length -ge 2GB) {
        throw "Cabinet exceeds the Windows cabinet size limit: $($cab.Name)"
    }
    return [pscustomobject]@{
        path = $destinationFull
        bytes = [uint64]$cab.Length
        installed_bytes = [uint64](($files | Measure-Object Length -Sum).Sum)
        sha256 = Get-Sha256 $destinationFull
    }
}

function Invoke-InstallSelfTest {
    param(
        [object[]]$Components,
        [string]$Profile,
        [string]$ReleaseDirectory,
        [string]$WorkDirectory,
        [string]$BootstrapPath,
        [string]$ManifestPath
    )
    Assert-TargetTopology $Components $Profile
    $testRoot = Join-Path $WorkDirectory "install self-test $Profile فارسی"
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
    $cache = Join-Path $testRoot ".dio\cache"
    New-Item -ItemType Directory -Path $cache -Force | Out-Null
    $testBootstrap = Join-Path $testRoot "dio-voice.exe"
    Copy-Item -LiteralPath $BootstrapPath -Destination $testBootstrap
    $selected = @($Components | Where-Object {
        $_.profile -eq "all" -or $_.profile -eq $Profile
    })
    $seedCache = {
        foreach ($component in $selected) {
            $asset = Join-Path $ReleaseDirectory (
                [IO.Path]::GetFileName(([uri]$component.url).AbsolutePath))
            $extension = if ($component.archive -eq "cab") { "cab" } else { "bin" }
            $cached = Join-Path $cache (
                "$($component.id)-$($component.sha256.Substring(0, 12)).$extension")
            try {
                New-Item -ItemType HardLink -Path $cached -Target $asset `
                    -ErrorAction Stop | Out-Null
            } catch {
                Copy-Item -LiteralPath $asset -Destination $cached
            }
        }
    }
    & $seedCache
    $process = Start-Process -FilePath $testBootstrap -ArgumentList @(
        "--install-only",
        $Profile,
        "--manifest-file",
        "`"$ManifestPath`"") -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "Bootstrap $Profile install self-test exited $($process.ExitCode)."
    }
    $installed = Join-Path $testRoot ".dio"
    foreach ($required in @(
            "versions\$Version\dio-voice.exe",
            "versions\$Version\runtime\piper\app\server.py",
            "models\porcupine\alexa_windows.ppn",
            "models\silero_vad.onnx",
            "models\vosk\am\final.mdl")) {
        if (-not (Test-Path -LiteralPath (Join-Path $installed $required) -PathType Leaf)) {
            throw "Install self-test missing $Profile payload: $required"
        }
    }
    $marker = Get-Content -Raw -LiteralPath (
        Join-Path $installed "data\install.json") | ConvertFrom-Json
    if ($marker.version -ne $Version -or $marker.profile -ne $Profile) {
        throw "Bootstrap $Profile install marker is incorrect."
    }
    foreach ($component in $selected) {
        $extension = if ($component.archive -eq "cab") { "cab" } else { "bin" }
        $cached = Join-Path $cache (
            "$($component.id)-$($component.sha256.Substring(0, 12)).$extension")
        foreach ($residue in @($cached, "$cached.part", "$cached.part.etag")) {
            if (Test-Path -LiteralPath $residue) {
                throw "Bootstrap retained a completed $Profile payload cache: $residue"
            }
        }
    }
    if ($Profile -eq "small") {
        $installedApplication = Join-Path $installed "versions\$Version\dio-voice.exe"
        $expectedApplicationHash = Get-Sha256 $installedApplication
        [IO.File]::WriteAllBytes($installedApplication, [byte[]](0x44, 0x49, 0x4f))
        & $seedCache
        $repair = Start-Process -FilePath $testBootstrap -ArgumentList @(
            "--repair",
            "--install-only",
            $Profile,
            "--manifest-file",
            "`"$ManifestPath`"") -Wait -PassThru -WindowStyle Hidden
        if ($repair.ExitCode -ne 0 -or
            (Get-Sha256 $installedApplication) -ne $expectedApplicationHash) {
            throw "Bootstrap Small Repair self-test failed."
        }
        foreach ($component in $selected) {
            $extension = if ($component.archive -eq "cab") { "cab" } else { "bin" }
            $cached = Join-Path $cache (
                "$($component.id)-$($component.sha256.Substring(0, 12)).$extension")
            if (Test-Path -LiteralPath $cached) {
                throw "Bootstrap Repair retained a completed cache: $cached"
            }
        }
    }
    [uint64]$footprint = (Get-ChildItem -LiteralPath $installed -Recurse -File |
        Measure-Object Length -Sum).Sum
    [uint64]$maximum = if ($Profile -eq "small") {
        1GB
    } else {
        [uint64](3.5 * 1GB)
    }
    if ($footprint -gt $maximum) {
        throw "Bootstrap $Profile folder footprint is $footprint bytes; cap is $maximum."
    }
    foreach ($forbidden in @("cache", "data", "logs", "models", "staging", "versions")) {
        if (Test-Path -LiteralPath (Join-Path $testRoot $forbidden)) {
            throw "Bootstrap wrote outside .dio during $Profile self-test: $forbidden"
        }
    }
    Remove-Item -LiteralPath $testRoot -Recurse -Force
    Write-Host "Bootstrap $Profile end-to-end install self-test passed ($footprint bytes)."
}

function New-Sbom {
    param([object]$ThirdParty, [string]$Destination)
    $packages = [Collections.Generic.List[object]]::new()
    $packages.Add([ordered]@{
        SPDXID = "SPDXRef-Package-DIO-Voice"
        name = "dio-voice"
        versionInfo = $Version
        downloadLocation = "https://github.com/omid3098/dio-voice"
        filesAnalyzed = $false
        licenseConcluded = "MIT"
        licenseDeclared = "MIT"
        primaryPackagePurpose = "APPLICATION"
    })
    $index = 0
    foreach ($dependency in $ThirdParty.dependencies) {
        $index++
        $package = [ordered]@{
            SPDXID = "SPDXRef-Package-$index"
            name = $dependency.name
            versionInfo = $dependency.version
            downloadLocation = $dependency.url
            filesAnalyzed = $false
            licenseConcluded = $dependency.license
            licenseDeclared = $dependency.license
            primaryPackagePurpose = switch ($dependency.scope) {
                "application" { "APPLICATION" }
                "runtime" { "LIBRARY" }
                "tool" { "APPLICATION" }
                default { "SOURCE" }
            }
        }
        if ($dependency.PSObject.Properties.Name -contains "sha256") {
            $package.checksums = @([ordered]@{
                algorithm = "SHA256"
                checksumValue = $dependency.sha256
            })
        }
        $packages.Add($package)
    }
    $requiredPackages = [ordered]@{
        "vazirmatn-variable-font" = "OFL-1.1"
        "lucide-icons" = "ISC AND MIT"
    }
    foreach ($required in $requiredPackages.GetEnumerator()) {
        $matches = @($packages | Where-Object { $_.name -eq $required.Key })
        if ($matches.Count -ne 1 -or
            $matches[0].licenseDeclared -ne $required.Value -or
            @($matches[0].checksums).Count -ne 1 -or
            $matches[0].checksums[0].algorithm -ne "SHA256" -or
            $matches[0].checksums[0].checksumValue -notmatch "^[0-9a-f]{64}$") {
            throw "Required SPDX package is missing or incomplete: $($required.Key)"
        }
    }
    $runtimeLock = Get-Content -Raw -LiteralPath (
        Join-Path $repo "metadata\runtime.lock.json") | ConvertFrom-Json
    foreach ($dependency in $runtimeLock.python_packages) {
        $index++
        $packages.Add([ordered]@{
            SPDXID = "SPDXRef-Package-$index"
            name = $dependency.name
            versionInfo = $dependency.version
            downloadLocation = $dependency.url
            filesAnalyzed = $false
            licenseConcluded = $dependency.license
            licenseDeclared = $dependency.license
            primaryPackagePurpose = "LIBRARY"
            checksums = @([ordered]@{
                algorithm = "SHA256"
                checksumValue = $dependency.sha256
            })
        })
    }
    foreach ($dependency in $runtimeLock.embedded_components) {
        $index++
        $packages.Add([ordered]@{
            SPDXID = "SPDXRef-Package-$index"
            name = $dependency.name
            versionInfo = $dependency.version
            downloadLocation = $dependency.url
            filesAnalyzed = $false
            licenseConcluded = $dependency.license
            licenseDeclared = $dependency.license
            primaryPackagePurpose = "LIBRARY"
            checksums = @([ordered]@{
                algorithm = "SHA256"
                checksumValue = $dependency.sha256
            })
        })
    }
    $created = (& git -C $repo show -s --format=%cI HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $created) {
        $created = "2026-07-31T00:00:00Z"
    } else {
        $created = ([DateTimeOffset]::Parse($created)).UtcDateTime.ToString(
            "yyyy-MM-ddTHH:mm:ssZ")
    }
    $sbom = [ordered]@{
        spdxVersion = "SPDX-2.3"
        dataLicense = "CC0-1.0"
        SPDXID = "SPDXRef-DOCUMENT"
        name = "DIO Voice $Version"
        documentNamespace = "https://github.com/omid3098/dio-voice/releases/$Version/sbom"
        creationInfo = [ordered]@{
            created = $created
            creators = @("Tool: scripts/build-release.ps1")
        }
        packages = $packages
    }
    Write-Utf8File $Destination (($sbom | ConvertTo-Json -Depth 8) + "`n")
}

function Get-PinnedModel {
    param([string]$Profile, [string]$WorkDirectory)
    $lock = Get-Content -Raw -LiteralPath (
        Join-Path $repo "metadata\models.lock.json") | ConvertFrom-Json
    $entry = $lock.profiles.$Profile
    $archive = Join-Path $repo "out\downloads\$($entry.directory).zip"
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $archive) -Force |
            Out-Null
        Invoke-WebRequest -UseBasicParsing -Uri $entry.url -OutFile $archive
    }
    $item = Get-Item -LiteralPath $archive
    if ($item.Length -ne $entry.archive_bytes -or
        (Get-Sha256 $archive) -ne $entry.sha256) {
        throw "Pinned $Profile Vosk archive failed integrity validation."
    }
    $canonical = Join-Path $repo "models\$($entry.directory)"
    if (Test-Path -LiteralPath $canonical -PathType Container) {
        $canonicalFiles = @(Get-ChildItem -LiteralPath $canonical -Recurse -File)
        if ($canonicalFiles.Count -ne $entry.file_count -or
            ($canonicalFiles | Measure-Object Length -Sum).Sum -ne $entry.installed_bytes) {
            throw "Prepared $Profile Vosk model differs from its lock."
        }
        return $canonical
    }
    $expanded = Join-Path $WorkDirectory "model-$Profile"
    New-Item -ItemType Directory -Path $expanded -Force | Out-Null
    Expand-Archive -LiteralPath $archive -DestinationPath $expanded
    $model = Join-Path $expanded $entry.directory
    $files = @(Get-ChildItem -LiteralPath $model -Recurse -File)
    if ($files.Count -ne $entry.file_count -or
        ($files | Measure-Object Length -Sum).Sum -ne $entry.installed_bytes) {
        throw "Pinned $Profile Vosk archive has an unexpected layout."
    }
    return $model
}

function Find-SignTool {
    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $kits = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    return Get-ChildItem -LiteralPath $kits -Filter signtool.exe -Recurse -File |
        Where-Object FullName -like "*\x64\signtool.exe" |
        Sort-Object FullName -Descending | Select-Object -First 1 -Expand FullName
}

function Find-CMake {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $visualStudio = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio"
    return Get-ChildItem -LiteralPath $visualStudio -Filter cmake.exe -Recurse -File |
        Where-Object FullName -like "*\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" |
        Sort-Object FullName -Descending | Select-Object -First 1 -Expand FullName
}

function Invoke-BootstrapBuild {
    param([string]$ManifestUrl, [string]$ManifestSha256, [string]$Output)
    $build = Reset-OwnedDirectory (Join-Path $outRoot "release-build\$Version")
    $cmake = Find-CMake
    if (-not $cmake) { throw "cmake.exe was not found." }
    & $cmake -S $repo -B $build -A x64 `
        -DDIO_BOOTSTRAP_ONLY=ON `
        "-DDIO_BOOTSTRAP_MANIFEST_URL=$ManifestUrl" `
        "-DDIO_BOOTSTRAP_MANIFEST_SHA256=$ManifestSha256"
    if ($LASTEXITCODE -ne 0) { throw "Bootstrap configure failed." }
    & $cmake --build $build --config Release --target dio_voice_bootstrap
    if ($LASTEXITCODE -ne 0) { throw "Bootstrap build failed." }
    $binary = Join-Path $build "bootstrap\dio-voice.exe"
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
        throw "Bootstrap output was not produced."
    }
    Copy-Item -LiteralPath $binary -Destination $Output -Force
    $process = Start-Process -FilePath $Output -ArgumentList "--self-test" `
        -Wait -PassThru -NoNewWindow
    if ($process.ExitCode -ne 0) { throw "Bootstrap self-test failed." }
}

function Invoke-PackagingSelfTest {
    $work = Reset-OwnedDirectory (
        Join-Path $outRoot "release-self-test-$([Guid]::NewGuid().ToString('N'))")
    $source = Join-Path $work "source"
    New-Item -ItemType Directory -Path (Join-Path $source "nested") -Force |
        Out-Null
    Write-Utf8File (Join-Path $source "one.txt") "one`n"
    Write-Utf8File (Join-Path $source "nested\two.txt") "two`n"
    $tree = Get-TreeMetrics $source
    if ($tree.file_count -ne 2 -or $tree.bytes -ne 8 -or
        $tree.tree_sha256 -ne
            "1321af386158725d12a49fbb55ea6e4ab778dcd2925657dd70ad5b6fa8412821") {
        throw "Ordinal tree-metric self-test failed."
    }
    if (-not (Test-SafeRelativePath "models\normal.con\file.bin")) {
        throw "Safe path self-test failed."
    }
    foreach ($unsafe in @(
            "..\escape", ".. \escape", "C:\escape", "\\server\share",
            "models\foo.\file", "models\foo \file", "models\file:stream",
            "models\CON", "models\con.txt", "models\con .txt",
            "models\PRN.log", "models\AUX", "models\NUL.bin",
            "models\COM1", "models\com9.dll", "models\LPT1",
            "models\lpt9.txt")) {
        if (Test-SafeRelativePath $unsafe) {
            throw "Traversal/device guard self-test failed: $unsafe"
        }
    }
    $cab = New-Cabinet $source (Join-Path $work "self-test.cab") $work "payload"
    $expanded = Join-Path $work "expanded"
    New-Item -ItemType Directory -Path $expanded | Out-Null
    & "$env:SystemRoot\System32\expand.exe" -F:* $cab.path $expanded | Out-Null
    if ($LASTEXITCODE -ne 0 -or
        (Get-Content -Raw -LiteralPath (Join-Path $expanded "payload\one.txt")) -ne "one`n" -or
        (Get-Content -Raw -LiteralPath (Join-Path $expanded "payload\nested\two.txt")) -ne "two`n") {
        throw "Cabinet round-trip self-test failed."
    }
    Assert-TargetTopology @(
        [pscustomobject]@{ target = "models"; profile = "all" },
        [pscustomobject]@{ target = "models"; profile = "small" }
    ) "small"
    try {
        Assert-TargetTopology @(
            [pscustomobject]@{ target = "models"; profile = "all" },
            [pscustomobject]@{ target = "models/vosk"; profile = "small" }
        ) "small"
        throw "Nested publish target self-test unexpectedly passed."
    } catch {
        if ($_.Exception.Message -notlike "Nested publish targets are unsafe*") {
            throw
        }
    }
    Test-ReleaseMetadata | Out-Null
    Write-Host "Release packaging self-test passed."
}

if ($SelfTest) {
    Invoke-PackagingSelfTest
    exit 0
}

if ($SbomOnly) {
    $thirdParty = Test-ReleaseMetadata
    $sbomDirectory = Reset-OwnedDirectory (Join-Path $outRoot "sbom")
    New-Sbom $thirdParty (Join-Path $sbomDirectory "sbom.spdx.json")
    Write-Host "SBOM generated: $sbomDirectory\sbom.spdx.json"
    exit 0
}

if ($VerifyRuntime) {
    Test-ReleaseMetadata -RequireRuntime | Out-Null
    Write-Host "Pinned runtime trees match metadata/runtime.lock.json."
    exit 0
}

if (-not $AssetBaseUrl) {
    $AssetBaseUrl = "https://github.com/omid3098/dio-voice/releases/download/v$Version"
}
if (-not $AssetBaseUrl.StartsWith("https://")) {
    throw "AssetBaseUrl must use HTTPS."
}
if ($Channel -eq "Stable" -and -not $SigningThumbprint) {
    throw "Stable releases require -SigningThumbprint."
}

$thirdParty = Test-ReleaseMetadata -RequireRuntime
if ($Channel -ne "Internal") {
    $blocked = @($thirdParty.dependencies | Where-Object {
        $_.PSObject.Properties.Name -contains "release_gate"
    })
    if ($blocked.Count -gt 0) {
        throw "Public release gates remain: $(($blocked.release_gate | Sort-Object -Unique) -join ', ')"
    }
}

if (-not $BuildDirectory) {
    $buildCandidates = @(
        (Join-Path $repo "out\build\vs-x64-release\bin\Release"),
        (Join-Path $repo "out\build\x64-release\bin"))
    $BuildDirectory = @($buildCandidates | Where-Object {
        Test-Path -LiteralPath (Join-Path $_ "dio-voice.exe") -PathType Leaf
    } | Select-Object -First 1)[0]
    if (-not $BuildDirectory) { $BuildDirectory = $buildCandidates[0] }
}
if (-not $HarnessPath) {
    $HarnessPath = if ($env:DIO_HARNESS_EXE) { $env:DIO_HARNESS_EXE } else {
        $harnessRoot = Join-Path (Split-Path -Parent $repo) "dio-harness"
        $harnessCandidates = @(
            (Join-Path $harnessRoot "out\build\vs-x64-release\Release\dio.exe"),
            (Join-Path $harnessRoot "out\build\x64-release\dio.exe"))
        $resolvedHarness = @($harnessCandidates | Where-Object {
            Test-Path -LiteralPath $_ -PathType Leaf
        } | Select-Object -First 1)[0]
        if ($resolvedHarness) { $resolvedHarness } else { $harnessCandidates[0] }
    }
}
$application = Join-Path $BuildDirectory "dio-voice.exe"
foreach ($required in @(
        $application,
        $HarnessPath,
        (Join-Path $BuildDirectory "Vazirmatn-Variable.ttf"),
        (Join-Path $repo "LICENSE"))) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing release input: $required"
    }
}

$release = Reset-OwnedDirectory (Join-Path $outRoot "release\$Version")
$work = Reset-OwnedDirectory (Join-Path $outRoot "release-work\$Version")
$core = Join-Path $work "core"
$runtimeStage = Join-Path $work "runtime"
$common = Join-Path $work "common"
New-Item -ItemType Directory -Path $core, $runtimeStage, $common -Force |
    Out-Null
Copy-Item -LiteralPath $application -Destination (Join-Path $core "dio-voice.exe")
Copy-Item -LiteralPath $HarnessPath -Destination (Join-Path $core "dio.exe")
Copy-Item -LiteralPath (Join-Path $BuildDirectory "Vazirmatn-Variable.ttf") `
    -Destination $core
Copy-Item -LiteralPath (Join-Path $repo "LICENSE") -Destination $core
Copy-Item -LiteralPath (Join-Path $repo "licenses") -Destination $core -Recurse
$sources = Join-Path $core "sources"
New-Item -ItemType Directory -Path $sources -Force | Out-Null
foreach ($dependency in @($thirdParty.dependencies | Where-Object {
            $_.PSObject.Properties.Name -contains "delivery" -and
            $_.delivery -eq "release-source"
        })) {
    $archive = Get-PinnedDependency $dependency
    Copy-Item -LiteralPath $archive -Destination (
        Join-Path $sources ([IO.Path]::GetFileName($dependency.destination)))
}
$patches = Join-Path $sources "dio-piper-patches"
New-Item -ItemType Directory -Path (Join-Path $patches "piper") -Force |
    Out-Null
foreach ($source in @("server.py", "SOURCES.md", "test_slim_runtime.py")) {
    Copy-Item -LiteralPath (Join-Path $repo "assets\piper\$source") `
        -Destination $patches
}
Copy-Item -LiteralPath (Join-Path $repo "assets\piper\piper\audio.py") `
    -Destination (Join-Path $patches "piper")
New-Sbom $thirdParty (Join-Path $core "sbom.spdx.json")

Copy-Item -LiteralPath (Join-Path $repo "runtime") `
    -Destination $runtimeStage -Recurse
$generatedPython = @(Get-ChildItem -LiteralPath (
    Join-Path $runtimeStage "runtime\piper") -Directory -Filter "__pycache__" `
    -Recurse -Force)
foreach ($directory in $generatedPython) {
    Remove-Item -LiteralPath $directory.FullName -Recurse -Force
}
$runtimeLock = Get-Content -Raw -LiteralPath (
    Join-Path $repo "metadata\runtime.lock.json") | ConvertFrom-Json
foreach ($excluded in $runtimeLock.release_excludes) {
    $path = Join-Path $runtimeStage "runtime\$excluded"
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        Remove-Item -LiteralPath $path -Force
    }
}
Copy-Item -LiteralPath (Join-Path $repo "models\porcupine") `
    -Destination $common -Recurse
Copy-Item -LiteralPath (Join-Path $repo "models\silero_vad.onnx") `
    -Destination $common

$small = Get-PinnedModel "small" $work
$large = Get-PinnedModel "large" $work
$definitions = @(
    [pscustomobject]@{ id = "core"; source = $core; profile = "all"; target = "versions/$Version"; prefix = "" },
    [pscustomobject]@{ id = "runtime"; source = $runtimeStage; profile = "all"; target = "versions/$Version"; prefix = "" },
    [pscustomobject]@{ id = "common-models"; source = $common; profile = "all"; target = "models"; prefix = "" },
    [pscustomobject]@{ id = "vosk-small"; source = $small; profile = "small"; target = "models"; prefix = "vosk" },
    [pscustomobject]@{ id = "vosk-large"; source = $large; profile = "large"; target = "models"; prefix = "vosk" }
)
$components = [Collections.Generic.List[object]]::new()
foreach ($definition in $definitions) {
    $filename = "dio-voice-$Version-$($definition.id).cab"
    $cab = New-Cabinet $definition.source (Join-Path $release $filename) `
        $work $definition.prefix
    $components.Add([pscustomobject][ordered]@{
        id = $definition.id
        url = "$($AssetBaseUrl.TrimEnd('/'))/$filename"
        sha256 = $cab.sha256
        bytes = $cab.bytes
        installed_bytes = $cab.installed_bytes
        target = $definition.target
        profile = $definition.profile
        archive = "cab"
    })
}

$smallBytes = [uint64](($components | Where-Object {
    $_.profile -eq "all" -or $_.profile -eq "small"
} | Measure-Object installed_bytes -Sum).Sum)
$largeBytes = [uint64](($components | Where-Object {
    $_.profile -eq "all" -or $_.profile -eq "large"
} | Measure-Object installed_bytes -Sum).Sum)
if ($smallBytes -gt 1GB) { throw "Small install exceeds 1 GiB." }
if ($largeBytes -gt [uint64](3.5 * 1GB)) { throw "Large install exceeds 3.5 GiB." }

$manifest = [ordered]@{
    schema = 1
    version = $Version
    entrypoint = "versions/$Version/dio-voice.exe"
    components = $components
}
$manifestPath = Join-Path $release "release-manifest.json"
Write-Utf8File $manifestPath (($manifest | ConvertTo-Json -Depth 6) + "`n")
$manifestSha = Get-Sha256 $manifestPath
$bootstrap = Join-Path $release "dio-voice.exe"
Invoke-BootstrapBuild "$($AssetBaseUrl.TrimEnd('/'))/release-manifest.json" `
    $manifestSha $bootstrap
Invoke-InstallSelfTest $components "small" $release $work $bootstrap $manifestPath
Invoke-InstallSelfTest $components "large" $release $work $bootstrap $manifestPath

if ($SigningThumbprint) {
    $signTool = Find-SignTool
    if (-not $signTool) { throw "signtool.exe was not found." }
    & $signTool sign /sha1 $SigningThumbprint /fd SHA256 /td SHA256 `
        /tr "http://timestamp.digicert.com" $bootstrap
    if ($LASTEXITCODE -ne 0) { throw "Authenticode signing failed." }
    $signature = Get-AuthenticodeSignature -LiteralPath $bootstrap
    if ($signature.Status -ne "Valid") {
        throw "Signed bootstrap did not validate: $($signature.Status)"
    }
}
if ((Get-Item -LiteralPath $bootstrap).Length -gt 999999) {
    throw "Signed bootstrap exceeds 999,999 bytes."
}

$hashes = @(Get-ChildItem -LiteralPath $release -File | Sort-Object Name |
    ForEach-Object { "$(Get-Sha256 $_.FullName)  $($_.Name)" })
Write-Utf8File (Join-Path $release "SHA256SUMS.txt") (($hashes -join "`n") + "`n")
Write-Host "Release staged: $release"
Write-Host "Bootstrap bytes: $((Get-Item -LiteralPath $bootstrap).Length)"
Write-Host "Small installed bytes: $smallBytes"
Write-Host "Large installed bytes: $largeBytes"
