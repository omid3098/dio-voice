"""One end-to-end probe for the release Piper environment."""

from __future__ import annotations

import importlib.util
from pathlib import Path

from piper import PiperVoice


def main() -> None:
    repo = Path(__file__).resolve().parents[2]
    bundle = repo / "runtime" / "piper"
    for package in (
        "edge_tts",
        "hazm",
        "optimum",
        "pandas",
        "pyarrow",
        "tokenizers",
        "torch",
        "transformers",
    ):
        assert importlib.util.find_spec(package) is None, package

    voice = PiperVoice.load(bundle / "models" / "mana" / "fa_IR-mana-medium.onnx")
    result = voice.phonemize("شیر را روی میز بگذار.")
    assert result and all(result) and all(isinstance(item, str) for item in result[0])
    english = voice.phonemize("hello world.")
    assert english and english[0]
    print("slim Piper probe passed")


if __name__ == "__main__":
    main()
