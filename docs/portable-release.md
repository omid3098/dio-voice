# Portable release contract

The only user-facing release file is the Authenticode-signed
`dio-voice.exe` bootstrap. Its signed size must not exceed 999,999 bytes.
Runtime payloads are immutable release assets described by a version-specific
manifest whose SHA-256 is compiled into that bootstrap.

The bootstrap writes application-owned state only below `.dio` next to
itself:

```text
.dio/
  versions/<version>/
  models/
  data/
  cache/
  logs/
  staging/
```

Component downloads use HTTPS, a resumable `.part` file and `If-Range` when
an ETag is available. A component is published only after its exact byte size
and SHA-256 match the manifest. CAB paths are rejected when absolute, empty,
`.` or `..`, Windows-normalized with trailing dot/space, ADS-bearing, nested
under another publish target, or a reserved device name. Extraction occurs
below the version staging directory before a same-volume rename publishes it.
Completed payload CABs and their resume metadata are removed after the install
marker is committed; Repair redownloads them when needed. This keeps the total
`.dio` footprint within 1 GiB for Small and 3.5 GiB for Large.

Before downloading, the installer shows remaining download bytes, installed
bytes, peak free-space requirement and current free space. The peak calculation
includes missing cache bytes, the complete staging tree, replaced/backup data
and a 256 MiB reserve. Cancel is honored at download, extraction, publication,
marker and suspended-launch boundaries, and abandoned staging is removed.

`small` and `large` components are mutually selected speech-recognition
profiles. `all` components are installed for either profile. Large is only
recommended when the machine has at least 16 GiB physical RAM and 8 GiB free
on the installation volume. The user selection always wins.

The bootstrap can be exercised without a remote release only through the
non-interactive install test path:

```powershell
dio-voice.exe --install-only small --manifest-file .\release-manifest.json --repair
dio-voice.exe --self-test
```

Production startup never accepts a manifest URL or manifest SHA override from
the command line: both values are compiled into the signed executable. The
local-manifest path is rejected unless `--install-only` is also present. The
download CTest additionally uses `--allow-test-http`; that switch is rejected
outside the same local install-only path and accepts only loopback hosts.

Remote builds must set both `DIO_BOOTSTRAP_MANIFEST_URL` and
`DIO_BOOTSTRAP_MANIFEST_SHA256`. An unsigned binary is an RC artifact only;
the stable release job must sign first and apply the size gate afterwards.

Build and validate the native bootstrap without CUI or any prepared runtime:

```powershell
cmake -S . -B out/build/bootstrap -A x64 -DDIO_BOOTSTRAP_ONLY=ON
cmake --build out/build/bootstrap --config Release --target dio_voice_bootstrap
./scripts/build-release.ps1 -SelfTest
```

Developer builds resolve CUI only through the explicit `DIO_CUI_PREFIX`
cache variable (or an ordinary installed-package search); no workstation path
is stored in the preset. A complete internal staging run is:

```powershell
$env:DIO_CUI_PREFIX = "C:/src/cui/out/install/x64-release"
cmake --preset x64-release
cmake --build --preset x64-release
./scripts/build-release.ps1 -Version 0.1.0-rc.1 -Channel Internal `
  -BuildDirectory ./out/build/vs-x64-release/bin/Release `
  -HarnessPath ../dio-harness/out/build/vs-x64-release/Release/dio.exe
```

`RC` and `Stable` refuse unresolved redistribution or trademark gates.
`Stable` additionally requires `-SigningThumbprint`; size is checked after
Authenticode signing. The generated `SHA256SUMS.txt`, SPDX SBOM and manifest
pin every published payload byte.
