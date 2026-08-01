# Piper runtime provenance

The runtime uses the pinned `piper-tts` 1.3.0 Windows wheel. Its corresponding
source is Piper commit `fee9b9cefae4ebf9e196cfe994dea418f051506c` with
eSpeak-NG commit `212928b394a96e8fd2096616bfd54e17845c48f6`. Immutable source
archive URLs, byte counts and SHA-256 values are pinned in
`metadata/third-party.lock.json`. Both archives and DIO's exact HTTP worker,
audio-tail patch and slim-runtime test are shipped under `sources/` in the
core release payload.

Runtime assets are reconstructed from `metadata/third-party.lock.json` and
Python packages from `metadata/piper-requirements.lock.txt`. The unlicensed
third-party Ezafe classifier and its LCA path are intentionally excluded.

Accepted synthesis settings are `noise_scale=0.333`, `noise_w=0.4`,
`length_scale=1.0`, mono PCM16 at 22050 Hz, with conservative tail trimming.

Licenses: Piper and eSpeak-NG are GPL-3.0-or-later and the Mana voice model is
MIT. The release publishes exact corresponding source and DIO patches beside
the binary payload.
