[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$downloads = Join-Path $repo "out\downloads"
$runtime = Join-Path $repo "runtime"
$models = Join-Path $repo "models"
$piperAssets = Join-Path $repo "assets\piper"
$lockPath = Join-Path $repo "metadata\third-party.lock.json"
$runtimeLockPath = Join-Path $repo "metadata\runtime.lock.json"
$modelLockPath = Join-Path $repo "metadata\models.lock.json"

function Assert-RepoChild {
    param([string]$Path)
    $repoFull = [IO.Path]::GetFullPath($repo).TrimEnd("\") + "\"
    $pathFull = [IO.Path]::GetFullPath($Path)
    if (-not $pathFull.StartsWith($repoFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside the repository: $pathFull"
    }
}

function Assert-Hash {
    param([string]$Path, [string]$Expected)
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
    if ($actual -ne $Expected.ToLowerInvariant()) {
        throw "SHA-256 mismatch for $Path. Expected $Expected, got $actual."
    }
}

function Get-PinnedFile {
    param([pscustomobject]$Entry)
    $name = [IO.Path]::GetFileName(([uri]$Entry.url).AbsolutePath)
    $path = Join-Path $downloads $(
        if ($Entry.name -eq "vosk-model-fa") {
            $name
        } else {
            "$($Entry.name)-$name"
        })
    if (-not (Test-Path -LiteralPath $path)) {
        Invoke-WebRequest -UseBasicParsing -Uri $Entry.url -OutFile $path
    }
    if ($Entry.sha256) {
        Assert-Hash -Path $path -Expected $Entry.sha256
    } elseif ($Entry.name -ne "vosk-model-fa" -or
        $Entry.integrity -ne "metadata/models.lock.json") {
        throw "Unpinned download in metadata\third-party.lock.json: $($Entry.name)"
    }
    return $path
}

function Test-Hash {
    param([string]$Path, [string]$Expected)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }
    $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $Path
    $actual = $hash.Hash.ToLowerInvariant()
    return $actual -eq $Expected.ToLowerInvariant()
}

function Test-LockedTree {
    param([string]$Root, [object[]]$Entries)
    foreach ($entry in $Entries) {
        $path = Join-Path $Root ($entry.path -replace "/", "\")
        if ((-not (Test-Path -LiteralPath $path -PathType Leaf)) -or
            (Get-Item -LiteralPath $path).Length -ne [long]$entry.bytes -or
            (-not (Test-Hash -Path $path -Expected $entry.sha256))) {
            return $false
        }
    }
    return $true
}

function Assert-LockedTree {
    param(
        [string]$Root,
        [object[]]$Entries,
        [int]$ExpectedCount = -1
    )
    if ($ExpectedCount -ge 0) {
        $actualCount = @(
            Get-ChildItem -LiteralPath $Root -Recurse -File).Count
        if ($actualCount -ne $ExpectedCount) {
            throw "Locked file-count mismatch at $Root."
        }
    }
    foreach ($entry in $Entries) {
        $path = Join-Path $Root ($entry.path -replace "/", "\")
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-Item -LiteralPath $path).Length -ne [long]$entry.bytes) {
            throw "Missing or incorrectly sized locked file: $path"
        }
        Assert-Hash -Path $path -Expected $entry.sha256
    }
}

New-Item -ItemType Directory -Force -Path $downloads, $runtime, $models |
    Out-Null
$lock = Get-Content -Raw -LiteralPath $lockPath | ConvertFrom-Json
$runtimeLock = Get-Content -Raw -LiteralPath $runtimeLockPath |
    ConvertFrom-Json
$modelLock = Get-Content -Raw -LiteralPath $modelLockPath |
    ConvertFrom-Json

foreach ($entry in $lock.dependencies) {
    if ($entry.delivery -in @(
            "bundled-in-vosk", "git-checkout", "release-profile",
            "release-source")) {
        continue
    }
    switch ($entry.name) {
        "onnxruntime" {
            $expected = @($runtimeLock.files |
                Where-Object path -eq "onnxruntime/onnxruntime.dll")
            if (-not (Test-LockedTree -Root $runtime -Entries $expected)) {
                $download = Get-PinnedFile $entry
                $expand = Join-Path $downloads "onnxruntime-expand"
                Assert-RepoChild $expand
                Remove-Item -LiteralPath $expand -Recurse -Force `
                    -ErrorAction SilentlyContinue
                Expand-Archive -LiteralPath $download -DestinationPath $expand
                $root = Get-ChildItem -LiteralPath $expand -Directory |
                    Select-Object -First 1
                $headerTarget = Join-Path $repo "third_party\onnxruntime\include"
                $runtimeTarget = Join-Path $runtime "onnxruntime"
                New-Item -ItemType Directory -Force `
                    -Path $headerTarget, $runtimeTarget | Out-Null
                Copy-Item -Path (Join-Path $root.FullName "include\*") `
                    -Destination $headerTarget -Recurse -Force
                Copy-Item -LiteralPath (
                    Join-Path $root.FullName "lib\onnxruntime.dll") `
                    -Destination $runtimeTarget -Force
            }
        }
        "vosk" {
            $expected = @($runtimeLock.files |
                Where-Object path -like "vosk/*")
            if (-not (Test-LockedTree -Root $runtime -Entries $expected)) {
                $download = Get-PinnedFile $entry
                $expand = Join-Path $downloads "vosk-expand"
                Assert-RepoChild $expand
                Remove-Item -LiteralPath $expand -Recurse -Force `
                    -ErrorAction SilentlyContinue
                Expand-Archive -LiteralPath $download -DestinationPath $expand
                $root = Get-ChildItem -LiteralPath $expand -Directory |
                    Select-Object -First 1
                $target = Join-Path $runtime "vosk"
                New-Item -ItemType Directory -Force -Path $target |
                    Out-Null
                Copy-Item -Path (Join-Path $root.FullName "*") `
                    -Destination $target -Recurse -Force
            }
        }
        "vosk-model-fa" {
            $expected = @($modelLock.models |
                Where-Object path -like "vosk-model-fa-0.42/*")
            if (-not (Test-LockedTree -Root $models -Entries $expected)) {
                $download = Get-PinnedFile $entry
                $expand = Join-Path $downloads "vosk-model-fa-expand"
                Assert-RepoChild $expand
                Remove-Item -LiteralPath $expand -Recurse -Force `
                    -ErrorAction SilentlyContinue
                Expand-Archive -LiteralPath $download -DestinationPath $expand
                $root = Get-ChildItem -LiteralPath $expand -Directory |
                    Where-Object Name -eq "vosk-model-fa-0.42" |
                    Select-Object -First 1
                if ($null -eq $root) {
                    throw "The Persian Vosk archive has an unexpected layout."
                }
                $target = Join-Path $repo $entry.destination
                Assert-RepoChild $target
                Remove-Item -LiteralPath $target -Recurse -Force `
                    -ErrorAction SilentlyContinue
                New-Item -ItemType Directory -Force -Path $target | Out-Null
                Copy-Item -Path (Join-Path $root.FullName "*") `
                    -Destination $target -Recurse -Force
            }
        }
        default {
            $target = Join-Path $repo $entry.destination
            if (-not (Test-Hash -Path $target -Expected $entry.sha256)) {
                $download = Get-PinnedFile $entry
                Assert-RepoChild $target
                New-Item -ItemType Directory -Force -Path (
                    Split-Path -Parent $target) | Out-Null
                Copy-Item -LiteralPath $download -Destination $target -Force
            }
        }
    }
}

$uv = Get-Command uv -ErrorAction Stop
$piper = Join-Path $runtime "piper"
$piperPython = Join-Path $piper "python"
$piperPythonExe = Join-Path $piperPython "python.exe"
$piperLock = Join-Path $repo "metadata\piper-requirements.lock.txt"
$pythonEntries = @($runtimeLock.embedded_components | Where-Object {
        $_.name -eq "cpython-embedded-windows-x64"
    })
if ($pythonEntries.Count -ne 1) {
    throw "CPython embedded runtime lock is missing or ambiguous."
}
$pythonEntry = $pythonEntries[0]
$pythonVersion = $pythonEntry.version
$pythonArchive = Join-Path $downloads (
    [IO.Path]::GetFileName(([uri]$pythonEntry.url).AbsolutePath))
if (-not (Test-Path -LiteralPath $pythonArchive -PathType Leaf)) {
    Invoke-WebRequest -UseBasicParsing -Uri $pythonEntry.url `
        -OutFile $pythonArchive
}
if ((Get-Item -LiteralPath $pythonArchive).Length -ne [long]$pythonEntry.bytes) {
    throw "CPython embedded archive byte count differs from its lock."
}
Assert-Hash -Path $pythonArchive -Expected $pythonEntry.sha256
$rebuildPython = -not (Test-Path -LiteralPath $piperPythonExe -PathType Leaf)
if (-not $rebuildPython) {
    $actualPython = & $piperPythonExe -I -c `
        "import platform; print(platform.python_version())"
    $rebuildPython = $LASTEXITCODE -ne 0 -or $actualPython -ne $pythonVersion
}
if ($rebuildPython) {
    if (Test-Path -LiteralPath $piperPython) {
        Assert-RepoChild $piperPython
        Remove-Item -LiteralPath $piperPython -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $piperPython | Out-Null
    Expand-Archive -LiteralPath $pythonArchive -DestinationPath $piperPython
    New-Item -ItemType Directory -Force -Path (
        Join-Path $piperPython "Lib\site-packages") | Out-Null
    @(
        "python311.zip"
        "."
        "Lib\site-packages"
        "import site"
    ) | Set-Content -LiteralPath (
        Join-Path $piperPython "python311._pth") -Encoding ASCII
}

$piperSitePackages = Join-Path $piperPython "Lib\site-packages"
& $uv.Source pip sync --target $piperSitePackages `
    --python-version $pythonVersion --python-platform windows `
    --only-binary :all: --require-hashes --link-mode copy $piperLock
if ($LASTEXITCODE -ne 0) { throw "Pinned Piper installation failed." }
$expectedPiper = Get-Content -LiteralPath $piperLock |
    Where-Object { $_ -match "^[a-z0-9-]+==" } |
    ForEach-Object { ($_ -split "\s+", 2)[0] } |
    Sort-Object
$actualPiper = & $piperPythonExe -I -X utf8 -c (
    "import importlib.metadata as m;" +
    "print('\n'.join(sorted(d.metadata['Name'].lower()+'=='+d.version " +
    "for d in m.distributions())))")
if ($LASTEXITCODE -ne 0 -or
    (Compare-Object $expectedPiper @($actualPiper))) {
    throw "The Piper environment differs from metadata\piper-requirements.lock.txt."
}

# uv launchers embed the build machine's Python path, and RECORD hashes those
# launchers. The worker invokes app\server.py directly, so neither is needed.
$piperLaunchers = Join-Path $piperSitePackages "bin"
if (Test-Path -LiteralPath $piperLaunchers) {
    Assert-RepoChild $piperLaunchers
    Remove-Item -LiteralPath $piperLaunchers -Recurse -Force
}
Get-ChildItem -LiteralPath $piperSitePackages -Directory -Filter "*.dist-info" |
    ForEach-Object {
        $record = Join-Path $_.FullName "RECORD"
        if (Test-Path -LiteralPath $record) {
            Assert-RepoChild $record
            Remove-Item -LiteralPath $record -Force
        }
    }

$piperPackage = Join-Path $piperPython "Lib\site-packages\piper"
$piperApp = Join-Path $piper "app"
New-Item -ItemType Directory -Force -Path $piperApp | Out-Null
Copy-Item -LiteralPath (
    Join-Path $piperAssets "piper\audio.py") `
    -Destination $piperPackage -Force
Copy-Item -LiteralPath (
    Join-Path $piperAssets "server.py"),
    (Join-Path $piperAssets "SOURCES.md") `
    -Destination $piperApp -Force

foreach ($unused in @(
        "__main__.py",
        "audio_playback.py",
        "communication.py",
        "download_voices.py",
        "enhance_phonemizer",
        "http_server.py",
        "prosody.py",
        "train")) {
    $unusedPath = Join-Path $piperPackage $unused
    if (Test-Path -LiteralPath $unusedPath) {
        Assert-RepoChild $unusedPath
        Remove-Item -LiteralPath $unusedPath -Recurse -Force
    }
}
$tashkeel = Join-Path $piperPackage "tashkeel"
foreach ($unusedTashkeel in @(
        "hint_id_map.json",
        "input_id_map.json",
        "model.onnx",
        "target_id_map.json")) {
    $unusedTashkeelPath = Join-Path $tashkeel $unusedTashkeel
    if (Test-Path -LiteralPath $unusedTashkeelPath) {
        Assert-RepoChild $unusedTashkeelPath
        Remove-Item -LiteralPath $unusedTashkeelPath -Force
    }
}
$espeakData = Join-Path $piperPackage "espeak-ng-data"
Get-ChildItem -LiteralPath $espeakData -File -Filter "*_dict" |
    Where-Object Name -NotIn @("en_dict", "fa_dict") |
    ForEach-Object {
        Assert-RepoChild $_.FullName
        Remove-Item -LiteralPath $_.FullName -Force
    }
Get-ChildItem -LiteralPath $piperPython -Directory -Filter "__pycache__" `
    -Recurse -Force | ForEach-Object {
        Assert-RepoChild $_.FullName
        Remove-Item -LiteralPath $_.FullName -Recurse -Force
    }

$piperProbe = & $piperPythonExe -I -c (
    "import json,numpy,onnxruntime,piper,sys;" +
    "print(json.dumps({'base':sys.base_prefix,'piper':piper.__file__," +
    "'numpy':numpy.__file__,'ort':onnxruntime.__file__}))") |
    ConvertFrom-Json
$piperRootFull = [IO.Path]::GetFullPath($piperPython).TrimEnd("\")
if ($LASTEXITCODE -ne 0 -or
    -not [IO.Path]::GetFullPath($piperProbe.base).TrimEnd("\").Equals(
        $piperRootFull, [StringComparison]::OrdinalIgnoreCase) -or
    -not [IO.Path]::GetFullPath($piperProbe.piper).StartsWith(
        "$piperRootFull\", [StringComparison]::OrdinalIgnoreCase) -or
    -not [IO.Path]::GetFullPath($piperProbe.numpy).StartsWith(
        "$piperRootFull\", [StringComparison]::OrdinalIgnoreCase) -or
    -not [IO.Path]::GetFullPath($piperProbe.ort).StartsWith(
        "$piperRootFull\", [StringComparison]::OrdinalIgnoreCase)) {
    throw "Piper Python is not self-contained inside runtime\piper."
}
& $piperPythonExe -I (Join-Path $piperAssets "test_slim_runtime.py")
if ($LASTEXITCODE -ne 0) {
    throw "PiperVoice.load slim-runtime probe failed."
}
foreach ($forbidden in @(
        "edge_tts", "hazm", "optimum", "pandas", "pyarrow",
        "tokenizers", "torch", "transformers")) {
    if (Test-Path -LiteralPath (
            Join-Path $piperSitePackages $forbidden)) {
        throw "Heavy runtime dependency remains: $forbidden"
    }
}
foreach ($pth in Get-ChildItem -LiteralPath (
        Join-Path $piperPython "Lib\site-packages") -Filter "*.pth" -File) {
    foreach ($line in Get-Content -LiteralPath $pth.FullName) {
        $candidate = $line.Trim()
        if ($candidate -and
            -not $candidate.StartsWith("#") -and
            -not $candidate.StartsWith("import ") -and
            [IO.Path]::IsPathRooted($candidate)) {
            throw "Piper contains an external .pth path: $candidate"
        }
    }
}

foreach ($unusedModel in @("ezafe", "homorich")) {
    $unusedModelPath = Join-Path $piper "models\$unusedModel"
    if (Test-Path -LiteralPath $unusedModelPath) {
        Assert-RepoChild $unusedModelPath
        Remove-Item -LiteralPath $unusedModelPath -Recurse -Force
    }
}

foreach ($obsolete in @(
        "fa_IR-mana-medium.onnx",
        "fa_IR-mana-medium.onnx.json")) {
    Remove-Item -LiteralPath (Join-Path $models $obsolete) `
        -Force -ErrorAction SilentlyContinue
}

$obsoleteTts = Join-Path $runtime "tts"
if (Test-Path -LiteralPath $obsoleteTts) {
    Assert-RepoChild $obsoleteTts
    Remove-Item -LiteralPath $obsoleteTts -Recurse -Force
}

Get-ChildItem -LiteralPath $piperPython -Directory -Filter "__pycache__" `
    -Recurse -Force | ForEach-Object {
        Assert-RepoChild $_.FullName
        Remove-Item -LiteralPath $_.FullName -Recurse -Force
    }

$nonPiperRuntime = @($runtimeLock.files |
    Where-Object path -notlike "piper/*")
Assert-LockedTree -Root $runtime -Entries $nonPiperRuntime
Assert-LockedTree -Root $models -Entries $modelLock.models `
    -ExpectedCount $modelLock.file_count

$piperBytes = (Get-ChildItem -LiteralPath $piper -Recurse -File |
    Measure-Object Length -Sum).Sum
Write-Host "Pinned DIO Voice runtime is ready at $runtime"
Write-Host "Slim Piper bytes: $piperBytes"
