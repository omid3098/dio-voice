# Validation record

## Current portable runtime

- Piper is the only TTS path. Edge/Farid, Chatterbox, and their fallback
  settings/tests have been removed.
- The application resolves mutable data under project-local `.dio`; saved
  settings cannot override runtime/model paths with locations elsewhere.
- Wake uses the pinned Porcupine Alexa model directly at 16 kHz in 512-sample
  frames. The legacy openWakeWord path and Vosk wake verifier are absent.
- Source builds use MSVC C17 with `/W4 /WX`.
- Native bootstrap CTest covers manifest/path/topology guards, publication
  rollback/cancel boundaries, and the installer UI smoke path.
- The local TCP download fixture covers the production HTTP rejection gate,
  an empty-cache download, interrupted transfer with persisted ETag and
  `Range`/`If-Range` resume, and rejection/cleanup of a same-size payload with
  the wrong SHA-256. The fixture also installs below a Unicode path.
- Release metadata gates the exact bundled Vazirmatn v33.003 font bytes and
  the CUI-transitive Lucide 1.16.0 source/generated geometry. Both must appear
  as separately licensed, checksummed SPDX packages.

## Release gates still required

- Signed bootstrap size at most 999,999 bytes.
- Clean Windows 10/11 x64 install, Repair, malicious-CAB traversal, low-space,
  offline, and Process Monitor checks. Low-space and offline remain manual
  until a deterministic production-equivalent seam exists.
- Provider discovery, portable-vault transfer/tamper, prompt byte equality,
  and MCP transport/approval/loop-limit checks.
- Wake corpus: at least 500 real positives from 20 speakers and 24 hours of
  Persian negatives, with FAR below 0.5/hour, FRR below 5%, and CPU below 2%
  of one core.
- UI matrix at 100/150/200% DPI, high contrast, keyboard-only, RTL/LTR, and
  provider-not-configured state.
- Piper model quality/license review, public model repository, SBOM, secret
  scan, and pinned release hashes.

Historical LocalAppData, Edge, Chatterbox, openWakeWord, and Pi canary evidence
was removed because it no longer validates the release architecture.
