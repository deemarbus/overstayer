#!/usr/bin/env python3
"""Plot Ivory Drive measurements; optionally compare user-licensed reference WAVs.

Requires numpy and matplotlib. Audio is processed locally and never uploaded.
Reported RMS matching and waveform differences do not establish sonic equivalence.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
from pathlib import Path
import tempfile
import wave

import numpy as np

# Cloud and CI home directories can be read-only. Keep optional plotting caches
# outside both the repository and the user's configuration files.
_cache = Path(tempfile.gettempdir()) / "ivory-analysis-cache"
_cache.mkdir(exist_ok=True)
os.environ.setdefault("MPLCONFIGDIR", str(_cache / "matplotlib"))
os.environ.setdefault("XDG_CACHE_HOME", str(_cache))
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def db(value):
    return 20.0 * np.log10(np.maximum(np.abs(value), 1e-15))


def load_pcm(path):
    """Read uncompressed PCM 16/24/32-bit WAV; reject ambiguous encodings."""
    with wave.open(str(path), "rb") as stream:
        width, channels, rate = stream.getsampwidth(), stream.getnchannels(), stream.getframerate()
        raw = stream.readframes(stream.getnframes())
        if stream.getcomptype() != "NONE":
            raise ValueError(f"{path}: use uncompressed PCM WAV")
    if width == 2:
        samples = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif width == 3:
        octets = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        values = octets[:, 0] | (octets[:, 1] << 8) | (octets[:, 2] << 16)
        values = (values ^ 0x800000) - 0x800000
        samples = values.astype(np.float64) / 8388608.0
    elif width == 4:
        samples = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"{path}: supported PCM widths are 16, 24, and 32 bits")
    return rate, samples.reshape(-1, channels)


def audio_statistics(audio):
    rms = np.sqrt(np.mean(audio * audio))
    peak = np.max(np.abs(audio))
    return {
        "frames": len(audio), "channels": audio.shape[1],
        "rms_dbfs": float(db(rms)), "peak_dbfs": float(db(peak)),
        "crest_factor_db": float(db(peak / max(rms, 1e-15))),
        "dc_mean": [float(v) for v in np.mean(audio, axis=0)],
        "max_sample_step": float(np.max(np.abs(np.diff(audio, axis=0)))),
    }


def spectrum(audio, rate, maximum=65536):
    data = audio[-min(len(audio), maximum):]
    if len(data) < 64:
        raise ValueError("at least 64 frames are required for spectral analysis")
    window = np.hanning(len(data))
    amplitude = 2.0 * np.abs(np.fft.rfft(data * window)) / np.sum(window)
    return np.fft.rfftfreq(len(data), 1.0 / rate), amplitude


def harmonic_statistics(audio, rate, frequency):
    frequencies, amplitude = spectrum(audio, rate)
    resolution = frequencies[1]
    values = []
    for harmonic in range(1, min(21, int((rate / 2) // frequency) + 1)):
        center = harmonic * frequency
        mask = np.abs(frequencies - center) <= 2.0 * resolution
        values.append(float(np.max(amplitude[mask])))
    if len(values) < 2:
        raise ValueError("fundamental leaves no harmonics below Nyquist")
    h = np.asarray(values)
    even = np.sum(h[1::2] ** 2)
    odd = np.sum(h[2::2] ** 2)
    return {
        "fundamental_hz": frequency,
        "harmonics_dbfs": [float(v) for v in db(h)],
        "thd_percent": float(100.0 * np.sqrt(np.sum(h[1:] ** 2)) / max(h[0], 1e-15)),
        "even_odd_energy_ratio": float(np.sqrt(even / max(odd, 1e-30))),
        "method": "Hann-window peak-bin amplitudes; use a stationary isolated sine; not THD+N",
    }


def plot_measurements(directory, output):
    summary = {}
    metrics = directory / "metrics.csv"
    if metrics.exists():
        with metrics.open(newline="") as stream:
            summary["measurements"] = [
                {**row, "value": float(row["value"])} for row in csv.DictReader(stream)
            ]
    harmonics = directory / "harmonics.csv"
    if harmonics.exists():
        h = np.genfromtxt(harmonics, delimiter=",", names=True)
        fig, ax = plt.subplots(figsize=(10, 4.6))
        for key, label, offset in [("normal_dbfs", "Normal", -0.23),
                                   ("second_dbfs", "2ND", 0.0), ("dual_dbfs", "DUAL", 0.23)]:
            ax.bar(h["harmonic"] + offset, np.maximum(h[key], -150.0) + 150.0,
                   width=0.23, bottom=-150.0, label=label)
        ax.set(xlabel="Harmonic number", ylabel="Amplitude (dBFS)", ylim=(-150, 5),
               title="Ivory Drive: measured coherent sine harmonics, 48 kHz, base-rate wet engine")
        ax.legend(); ax.grid(axis="y", alpha=0.25)
        fig.tight_layout(); fig.savefig(output / "harmonics.png", dpi=160); plt.close(fig)
    response = directory / "frequency_response.csv"
    if response.exists():
        data = np.genfromtxt(response, delimiter=",", names=True)
        fig, axes = plt.subplots(2, 1, figsize=(10, 6.5), sharex=True)
        axes[0].semilogx(data["frequency_hz"], data["compensated_relative_db"], "o-", color="#547557")
        axes[0].set(ylabel="COMP error (dB)", title="Small-signal measurements relative to flat engine")
        axes[0].axhline(0.05, color="gray", linestyle="--"); axes[0].axhline(-0.05, color="gray", linestyle="--")
        axes[1].semilogx(data["frequency_hz"], data["hp_db"], "o-", label="HP 500 Hz, resonance 0")
        axes[1].semilogx(data["frequency_hz"], data["lp_db"], "o-", label="LP 4 kHz, resonance 0")
        axes[1].set(xlabel="Frequency (Hz)", ylabel="Relative amplitude (dB)")
        axes[1].legend()
        for ax in axes:
            ax.grid(which="both", alpha=0.25)
        fig.tight_layout(); fig.savefig(output / "frequency_response.png", dpi=160); plt.close(fig)
    impulse = directory / "impulse.wav"
    if impulse.exists():
        rate, audio = load_pcm(impulse)
        index = int(np.argmax(np.abs(audio[:, 0])))
        begin, end = max(0, index - 10), min(len(audio), index + int(rate * 0.04))
        fig, ax = plt.subplots(figsize=(10, 3.5))
        ax.plot((np.arange(begin, end) - index) / rate * 1000.0, audio[begin:end, 0], color="#547557")
        ax.set(xlabel="Time after impulse peak (ms)", ylabel="Amplitude", title="Nonlinear wet-engine impulse response")
        ax.grid(alpha=0.25); fig.tight_layout(); fig.savefig(output / "impulse.png", dpi=160); plt.close(fig)
        summary["impulse_fixture"] = audio_statistics(audio)
    transient_input = directory / "transient_input.wav"
    transient_wet = directory / "transient_wet.wav"
    if transient_input.exists() and transient_wet.exists():
        rate, dry = load_pcm(transient_input)
        wet_rate, wet = load_pcm(transient_wet)
        if rate != wet_rate:
            raise ValueError("transient fixture sample rates differ")
        count = min(len(dry), len(wet), int(rate * 0.03))
        time = np.arange(count) / rate * 1000.0
        fig, ax = plt.subplots(figsize=(10, 4))
        for signal, label in [(dry, "Input"), (wet, "Saturated wet")]:
            level = np.sqrt(np.mean(signal * signal))
            ax.plot(time, signal[:count, 0] / max(level, 1e-15), label=label, alpha=0.8)
        ax.set(xlabel="Time (ms)", ylabel="Amplitude / full-fixture RMS", title="Transient peak rounding, independently RMS normalized")
        ax.legend(); ax.grid(alpha=0.25); fig.tight_layout()
        fig.savefig(output / "transient.png", dpi=160); plt.close(fig)
        summary["transient_input_fixture"] = audio_statistics(dry)
        summary["transient_wet_fixture"] = audio_statistics(wet)
    return summary


def align_arrays(reference, candidate, rate, maximum_lag_ms):
    """Estimate integer delay from the first two seconds; preserve channel count."""
    length = min(len(reference), len(candidate), rate * 2)
    a, b = reference[:length, 0].copy(), candidate[:length, 0].copy()
    a -= a.mean(); b -= b.mean()
    nfft = 1 << int(2 * length - 1).bit_length()
    correlation = np.fft.irfft(np.fft.rfft(a, nfft) * np.conj(np.fft.rfft(b, nfft)), nfft)
    limit = min(int(rate * maximum_lag_ms / 1000), length // 2)
    lags = np.arange(-limit, limit + 1)
    lag = int(lags[np.argmax(np.abs(correlation[lags % nfft]))])
    if lag > 0:
        reference = reference[lag:]
    elif lag < 0:
        candidate = candidate[-lag:]
    length = min(len(reference), len(candidate))
    return reference[:length], candidate[:length], lag


def compare_reference(reference_path, candidate_path, output, fundamental, maximum_lag_ms, dry_path=None):
    rate, reference = load_pcm(reference_path)
    other_rate, candidate = load_pcm(candidate_path)
    if rate != other_rate or reference.shape[1] != candidate.shape[1]:
        raise ValueError("reference and candidate must have the same sample rate and channel count")
    reference, candidate, lag = align_arrays(reference, candidate, rate, maximum_lag_ms)
    candidate_rms = np.sqrt(np.mean(candidate * candidate))
    if candidate_rms < 1e-15 or np.sqrt(np.mean(reference * reference)) < 1e-15:
        raise ValueError("reference comparison requires non-silent signals")
    gain = np.sqrt(np.mean(reference * reference)) / candidate_rms
    matched = candidate * gain
    residual = reference - matched
    report = {
        "notice": "Independent descriptive comparison; no validated 1:1 match. Same source, controls and processing level required.",
        "sample_rate_hz": rate, "alignment_reference_minus_candidate_samples": lag,
        "alignment_maximum_lag_ms": maximum_lag_ms,
        "reference": audio_statistics(reference), "candidate_original": audio_statistics(candidate),
        "candidate_rms_match_gain_db": float(db(gain)),
        "matched_residual_rms_dbfs": float(db(np.sqrt(np.mean(residual * residual)))),
        "gain_matching": "One global RMS scalar, no LUFS/perceptual normalization or frequency correction",
    }
    fig, axes = plt.subplots(2, 1, figsize=(11, 7))
    count = min(len(reference), int(rate * 0.04))
    time = np.arange(count) / rate * 1000.0
    axes[0].plot(time, reference[:count, 0], label="Licensed reference", alpha=0.8)
    axes[0].plot(time, matched[:count, 0], label="Ivory Drive, RMS matched", alpha=0.8)
    axes[0].set(xlabel="Aligned time (ms)", ylabel="Amplitude", title="First channel; integer-delay alignment")
    for signal, label in [(reference[:, 0], "Reference"), (matched[:, 0], "Candidate, RMS matched")]:
        frequency, amplitude = spectrum(signal, rate)
        axes[1].semilogx(frequency[1:], db(amplitude[1:]), label=label, alpha=0.8)
    axes[1].set(xlabel="Frequency (Hz)", ylabel="Hann amplitude (dBFS)", ylim=(-150, 10), xlim=(20, rate / 2))
    for ax in axes:
        ax.legend(); ax.grid(alpha=0.25)
    fig.tight_layout(); fig.savefig(output / "reference_comparison.png", dpi=160); plt.close(fig)
    if fundamental:
        report["reference_sine"] = harmonic_statistics(reference[:, 0], rate, fundamental)
        report["candidate_sine"] = harmonic_statistics(candidate[:, 0], rate, fundamental)
    if dry_path:
        dry_rate, dry = load_pcm(dry_path)
        if dry_rate != rate or dry.shape[1] != reference.shape[1]:
            raise ValueError("dry stimulus must share sample rate and channel count")
        report["dry_stimulus"] = audio_statistics(dry)
        for signal, key in [(reference, "reference"), (candidate, "candidate")]:
            source, processed, _ = align_arrays(dry, signal, rate, maximum_lag_ms)
            block = min(16384, len(source))
            if block < 128:
                raise ValueError("dry comparison needs at least 128 frames")
            window = np.hanning(block)
            cross = np.zeros(block // 2 + 1, dtype=np.complex128)
            source_power = np.zeros(block // 2 + 1)
            output_power = np.zeros(block // 2 + 1)
            for start in range(0, len(source) - block + 1, block // 2):
                x = np.fft.rfft(source[start:start + block, 0] * window)
                y = np.fft.rfft(processed[start:start + block, 0] * window)
                cross += y * np.conj(x); source_power += np.abs(x) ** 2; output_power += np.abs(y) ** 2
            transfer = cross / np.maximum(source_power, 1e-30)
            coherence = np.abs(cross) ** 2 / np.maximum(source_power * output_power, 1e-30)
            frequency = np.fft.rfftfreq(block, 1.0 / rate)
            np.savetxt(output / f"{key}_stimulus_transfer.csv", np.c_[frequency, db(transfer), coherence],
                       delimiter=",", header="frequency_hz,transfer_db,coherence", comments="")
        report["transfer_caution"] = "Cross-spectrum estimate depends on stimulus and level; nonlinear processing is not a single LTI frequency response. Inspect coherence."
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--measurements", type=Path, default=Path("validation"))
    parser.add_argument("--output", type=Path, default=Path("validation/plots"))
    parser.add_argument("--reference", type=Path, help="User-rendered output from licensed reference product")
    parser.add_argument("--candidate", type=Path, help="Ivory Drive render of the identical stimulus and controls")
    parser.add_argument("--dry", type=Path, help="Optional original stimulus for level-dependent transfer estimates")
    parser.add_argument("--fundamental", type=float, help="Sine fundamental Hz; omit for music, impulses and sweeps")
    parser.add_argument("--maximum-lag-ms", type=float, default=100.0)
    args = parser.parse_args()
    if bool(args.reference) != bool(args.candidate):
        parser.error("--reference and --candidate must be provided together")
    if args.dry and not args.reference:
        parser.error("--dry requires --reference and --candidate")
    if args.fundamental is not None and args.fundamental <= 0:
        parser.error("--fundamental must be positive")
    if args.maximum_lag_ms < 0:
        parser.error("--maximum-lag-ms must be non-negative")
    args.output.mkdir(parents=True, exist_ok=True)
    report = plot_measurements(args.measurements, args.output)
    if args.reference:
        report["reference_comparison"] = compare_reference(args.reference, args.candidate, args.output,
            args.fundamental, args.maximum_lag_ms, args.dry)
    (args.output / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(f"Saved numerical report and plots to {args.output}")


if __name__ == "__main__":
    main()
