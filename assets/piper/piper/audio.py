"""Small output cleanup for the Persian Piper voice."""

import numpy as np


def _fade_to_zero(audio: np.ndarray, samples: int) -> np.ndarray:
    if samples <= 1 or audio.size == 0:
        return audio

    samples = min(samples, audio.size)
    result = audio.copy()
    fade = 0.5 * (
        1.0
        + np.cos(np.linspace(0.0, np.pi, samples, dtype=np.float64))
    )
    result[-samples:] *= fade.astype(result.dtype, copy=False)
    return result


def trim_tail(audio: np.ndarray, sample_rate: int) -> np.ndarray:
    """Remove a sustained low-energy tail and fade safely to zero."""
    if sample_rate <= 0:
        raise ValueError("sample_rate must be positive")
    if audio.ndim != 1:
        raise ValueError("audio must be mono")
    if audio.size == 0:
        return audio

    window = max(1, round(sample_rate * 0.020))
    hop = max(1, round(sample_rate * 0.010))
    hangover = round(sample_rate * 0.040)
    minimum_trim = round(sample_rate * 0.040)
    trim_fade = round(sample_rate * 0.020)
    final_fade = round(sample_rate * 0.012)

    if audio.size < window:
        return _fade_to_zero(audio, final_fade)

    starts = np.arange(0, audio.size - window + 1, hop)
    frame_db = np.empty(starts.size, dtype=np.float64)
    for index, start in enumerate(starts):
        frame = audio[start : start + window].astype(np.float64)
        rms = np.sqrt(np.mean(frame * frame))
        frame_db[index] = 20.0 * np.log10(max(rms, 1e-9))

    threshold_db = max(-42.0, float(np.percentile(frame_db, 90)) - 28.0)
    active = frame_db >= threshold_db
    persistent = (
        np.convolve(active.astype(np.int8), np.ones(5, dtype=np.int8), "same")
        >= 3
    )

    # ponytail: one-speaker endpoint heuristic; use a trained VAD if it ever
    # cuts a real final consonant.
    active_indices = np.flatnonzero(persistent)
    if active_indices.size:
        run_starts = active_indices[
            np.r_[True, np.diff(active_indices) > 1]
        ]
        run_ends = active_indices[
            np.r_[np.diff(active_indices) > 1, True]
        ]
        for index in range(1, len(run_starts)):
            run_duration = (
                int(starts[run_ends[index]])
                - int(starts[run_starts[index]])
                + window
            )
            preceding_gap = (
                int(starts[run_starts[index]])
                - int(starts[run_ends[index - 1]])
                - window
            )
            if (
                run_duration < round(sample_rate * 0.050)
                and preceding_gap >= round(sample_rate * 0.040)
            ):
                persistent[run_starts[index] : run_ends[index] + 1] = False

    active_indices = np.flatnonzero(persistent)
    if active_indices.size == 0:
        return _fade_to_zero(audio, final_fade)

    cut = min(
        audio.size,
        int(starts[active_indices[-1]]) + window + hangover,
    )
    if audio.size - cut < minimum_trim:
        return _fade_to_zero(audio, final_fade)

    return _fade_to_zero(audio[:cut], trim_fade)
