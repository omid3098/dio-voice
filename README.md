# DIO Voice

DIO Voice is a portable Windows x64 voice client for DIO Harness. Speech
recognition and synthesis run locally; reasoning and tools stay behind the
OpenAI-compatible provider and MCP boundaries.

## Run

Public binaries are published only after the gates in `docs/validation.md`
pass. From a published release, download `dio-voice.exe` and run it from the
folder you want to keep. The bootstrapper downloads verified, version-pinned
payloads and keeps everything beside itself:

```text
dio-voice.exe
.dio/
  versions/<version>/
  models/
  data/
  cache/
  logs/
  staging/
```

It does not require Administrator access and does not install Python, modify
`PATH`, use the Registry, or write application data outside `.dio`. Copying
the folder preserves settings and the encrypted secret vault; the vault's
master password is still required on a new session.

The application does not start voice workers until an API base URL and model
are configured. Model IDs can be discovered from an OpenAI-compatible
`GET /models` endpoint or entered manually. Piper is the only text-to-speech
runtime. Alexa detection uses the pinned Porcupine runtime and remains subject
to the release quality and trademark gates.

## Build and test

From a Visual Studio x64 developer shell:

```powershell
pwsh -File scripts/bootstrap-runtime.ps1
cmake --preset x64-release
cmake --build --preset x64-release
ctest --preset x64-release
```

Generated runtimes and models are ignored by Git and reconstructed from the
checked-in SHA-256 locks. [CUI](https://github.com/omid3098/cui) and
[DIO Harness](https://github.com/omid3098/dio-harness) are separate pinned
source boundaries. The Persian Piper voice is pinned from
[dio-piper-fa](https://huggingface.co/omid3098/dio-piper-fa).

Useful targets:

- `dio-voice.exe`: native Win32/CUI application.
- `dio_voice_core_tests`: wake, VAD, ASR, lifecycle, and race checks.
- `dio_voice_probe`: local WAV or microphone recognition probe.
- `dio_agent_tests`: harness protocol and transport checks.

## Runtime boundary

DIO Voice owns capture, wake/VAD/STT, local Piper playback, settings, and the
portable vault. DIO Harness owns provider calls, context, prompts, and MCP
tools. The pipe is local-only and restricted to the current logon and spawned
child process.

See `docs/validation.md` for current evidence and remaining release gates.
