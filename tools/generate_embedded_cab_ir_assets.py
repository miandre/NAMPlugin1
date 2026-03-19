#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import struct
import wave


ROOT = pathlib.Path(__file__).resolve().parents[1]
INPUT_ROOT = ROOT / "NeuralAmpModeler" / "resources" / "tmpLoad" / "IR"
OUTPUT_HEADER = ROOT / "NeuralAmpModeler" / "EmbeddedCabIRAssets.h"
OUTPUT_SOURCE = ROOT / "NeuralAmpModeler" / "EmbeddedCabIRAssets.cpp"
MIC_FOLDERS = ("57", "121")
CAPTURE_COUNT = 5


def _read_wav_as_floats(path: pathlib.Path) -> tuple[list[float], int]:
    with wave.open(str(path), "rb") as wav_file:
        channels = wav_file.getnchannels()
        sample_width = wav_file.getsampwidth()
        sample_rate = wav_file.getframerate()
        frame_count = wav_file.getnframes()
        frames = wav_file.readframes(frame_count)

    if channels != 1:
        raise ValueError(f"{path} must be mono, got {channels} channels")

    samples: list[float] = []
    if sample_width == 1:
        for value in frames:
            samples.append((value - 128) / 128.0)
    elif sample_width == 2:
        for (value,) in struct.iter_unpack("<h", frames):
            samples.append(value / 32768.0)
    elif sample_width == 3:
        for i in range(0, len(frames), 3):
            chunk = frames[i : i + 3]
            value = int.from_bytes(chunk, byteorder="little", signed=False)
            if value & 0x800000:
                value -= 0x1000000
            samples.append(value / 8388608.0)
    elif sample_width == 4:
        for (value,) in struct.iter_unpack("<f", frames):
            samples.append(float(value))
    else:
        raise ValueError(f"{path} uses unsupported sample width {sample_width}")

    return samples, sample_rate


def _format_samples(samples: list[float]) -> str:
    lines: list[str] = []
    row: list[str] = []
    for index, sample in enumerate(samples, start=1):
        row.append(f"{sample:.9e}f")
        if index % 8 == 0:
            lines.append("  " + ", ".join(row))
            row = []
    if row:
        lines.append("  " + ", ".join(row))
    return ",\n".join(lines)


def _write_header() -> None:
    OUTPUT_HEADER.write_text(
        """#pragma once

#include <cstddef>

namespace embedded_cab_ir
{
struct EmbeddedCabIRAsset
{
  const float* samples;
  size_t numSamples;
  double sampleRate;
};

const EmbeddedCabIRAsset* GetCuratedCabIRAsset(int sourceChoice, int captureIndex);
} // namespace embedded_cab_ir
""",
        encoding="ascii",
    )


def _write_source(assets: list[tuple[str, int, list[float], int]]) -> None:
    lines: list[str] = [
        '#include "EmbeddedCabIRAssets.h"',
        "",
        "#include <array>",
        "",
        "namespace embedded_cab_ir",
        "{",
        "namespace",
        "{",
    ]

    for mic_name, capture_index, samples, sample_rate in assets:
        symbol = f"kMic{mic_name}Capture{capture_index}"
        lines.append(f"const float {symbol}[] = {{")
        lines.append(_format_samples(samples))
        lines.append("};")
        lines.append(
            f"const EmbeddedCabIRAsset {symbol}Asset = {{{symbol}, {len(samples)}, {float(sample_rate):.1f}}};"
        )
        lines.append("")

    lines.append("const std::array<std::array<const EmbeddedCabIRAsset*, 5>, 2> kCuratedCabIRAssets = {{")
    for mic_name in MIC_FOLDERS:
        asset_refs = ", ".join(f"&kMic{mic_name}Capture{capture_index}Asset" for capture_index in range(CAPTURE_COUNT))
        lines.append(f"  std::array<const EmbeddedCabIRAsset*, 5>{{{asset_refs}}},")
    lines.append("};")
    lines.append("} // namespace")
    lines.append("")
    lines.append("const EmbeddedCabIRAsset* GetCuratedCabIRAsset(const int sourceChoice, const int captureIndex)")
    lines.append("{")
    lines.append("  const int micIndex = sourceChoice - 1;")
    lines.append("  if (micIndex < 0 || micIndex >= static_cast<int>(kCuratedCabIRAssets.size()))")
    lines.append("    return nullptr;")
    lines.append("  if (captureIndex < 0 || captureIndex >= static_cast<int>(kCuratedCabIRAssets[static_cast<size_t>(micIndex)].size()))")
    lines.append("    return nullptr;")
    lines.append("  return kCuratedCabIRAssets[static_cast<size_t>(micIndex)][static_cast<size_t>(captureIndex)];")
    lines.append("}")
    lines.append("} // namespace embedded_cab_ir")

    OUTPUT_SOURCE.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> None:
    assets: list[tuple[str, int, list[float], int]] = []
    for mic_name in MIC_FOLDERS:
        for capture_index in range(CAPTURE_COUNT):
            wav_path = INPUT_ROOT / mic_name / f"{capture_index}.wav"
            if not wav_path.exists():
                raise FileNotFoundError(f"Missing curated IR asset: {wav_path}")
            samples, sample_rate = _read_wav_as_floats(wav_path)
            assets.append((mic_name, capture_index, samples, sample_rate))

    _write_header()
    _write_source(assets)


if __name__ == "__main__":
    main()
