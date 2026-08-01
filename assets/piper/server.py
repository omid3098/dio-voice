"""Tray-owned, loopback-only Piper speech worker."""

from __future__ import annotations

import argparse
import json
import logging
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from typing import Any

import numpy as np
from piper import PiperVoice, SynthesisConfig
from piper.audio import trim_tail


_LOGGER = logging.getLogger("dio_piper")
_MAX_REQUEST_BYTES = 256 * 1024
_SAMPLE_RATE = 22050
_MAX_PCM_BYTES = _SAMPLE_RATE * 120 * 2


class DioPiperServer(HTTPServer):
    allow_reuse_address = False

    def __init__(self, address: tuple[str, int], voice: PiperVoice) -> None:
        self.voice = voice
        self.synthesis = SynthesisConfig(
            length_scale=1.0,
            noise_scale=0.333,
            noise_w_scale=0.4,
            normalize_audio=True,
        )
        super().__init__(address, DioPiperHandler)


class DioPiperHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "DIO-Piper/2"

    @property
    def dio_server(self) -> DioPiperServer:
        return self.server  # type: ignore[return-value]

    def _reply(
        self,
        status: int,
        body: bytes,
        content_type: str,
        **headers: str,
    ) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        for name, value in headers.items():
            self.send_header(name.replace("_", "-"), value)
        self.end_headers()
        if body:
            self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        if self.path != "/health":
            self._reply(404, b"not found", "text/plain; charset=utf-8")
            return
        self._reply(
            200,
            b'{"status":"ok","backend":"piper"}',
            "application/json",
        )

    def do_POST(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
        if self.path != "/v1/audio/speech":
            self._reply(404, b"not found", "text/plain; charset=utf-8")
            return
        try:
            content_length = int(self.headers.get("Content-Length", "0"))
            if content_length <= 0 or content_length > _MAX_REQUEST_BYTES:
                raise ValueError("invalid request size")
            request: Any = json.loads(
                self.rfile.read(content_length).decode("utf-8", errors="strict")
            )
            text = request.get("input") if isinstance(request, dict) else None
            if not isinstance(text, str) or not text.strip():
                raise ValueError("input must be non-empty text")

            pcm = bytearray()
            for chunk in self.dio_server.voice.synthesize(
                text,
                self.dio_server.synthesis,
            ):
                if (
                    chunk.sample_rate != _SAMPLE_RATE
                    or chunk.sample_width != 2
                    or chunk.sample_channels != 1
                ):
                    raise RuntimeError("unexpected Piper audio format")
                audio = trim_tail(chunk.audio_float_array, chunk.sample_rate)
                encoded = (
                    np.clip(audio, -1.0, 1.0) * np.float32(32767.0)
                ).astype(np.int16).tobytes()
                if not encoded or len(pcm) + len(encoded) > _MAX_PCM_BYTES:
                    raise RuntimeError("invalid Piper audio length")
                pcm.extend(encoded)

            if not pcm or len(pcm) % 2:
                raise RuntimeError("Piper returned invalid PCM")
            self._reply(
                200,
                bytes(pcm),
                "audio/pcm; rate=22050; channels=1",
                X_Audio_Sample_Rate=str(_SAMPLE_RATE),
            )
        except Exception as error:  # request boundary: fail closed
            _LOGGER.exception("Piper synthesis failed: %s", error)
            self._reply(503, b"synthesis failed", "text/plain; charset=utf-8")

    def log_message(self, format: str, *args: object) -> None:
        _LOGGER.debug(format, *args)


def _load_voice(model: Path) -> PiperVoice:
    voice = PiperVoice.load(model)
    if voice.config.sample_rate != _SAMPLE_RATE:
        raise RuntimeError(
            f"unexpected Piper sample rate: {voice.config.sample_rate}"
        )
    return voice


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18767)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()
    if args.host != "127.0.0.1":
        parser.error("Piper may only bind to 127.0.0.1")
    args.model = args.model.resolve()
    for path in (
        args.model,
        Path(f"{args.model}.json"),
    ):
        if not path.is_file():
            parser.error(f"missing runtime asset: {path}")

    logging.basicConfig(
        level=logging.DEBUG if args.debug else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    voice = _load_voice(args.model)
    server = DioPiperServer((args.host, args.port), voice)
    try:
        _LOGGER.info("Piper ready on %s:%d", args.host, args.port)
        server.serve_forever(poll_interval=0.25)
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
