#!/usr/bin/env python3

from __future__ import annotations

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]
INPUT_ROOT = ROOT / "NeuralAmpModeler" / "resources" / "tmpLoad"
OUTPUT_HEADER = ROOT / "NeuralAmpModeler" / "EmbeddedModelAssets.h"
OUTPUT_SOURCE = ROOT / "NeuralAmpModeler" / "EmbeddedModelAssets.cpp"
RAW_STRING_DELIMITER = "NAMMDL"
AMP_ASSETS = (
    ("Amp1A", "Amp1A.nam"),
    ("Amp1B", "Amp1B.nam"),
    ("Amp2A", "Amp2A.nam"),
    ("Amp2B", "Amp2B.nam"),
    ("Amp3A", "Amp3A.nam"),
    ("Amp3B", "Amp3B.nam"),
)
STOMP_ASSETS = (
    ("BoostA", "BoostA.nam"),
    ("BoostB", "BoostB.nam"),
)


def _read_model_json(path: pathlib.Path) -> str:
    text = path.read_text(encoding="utf-8")
    if RAW_STRING_DELIMITER in text:
        raise ValueError(f"Embedded model delimiter collision in {path}")
    text.encode("ascii")
    return text


def _write_header() -> None:
    OUTPUT_HEADER.write_text(
        """#pragma once

#include <cstddef>

namespace embedded_model
{
struct EmbeddedModelAsset
{
  const char* token;
  const char* json;
  size_t jsonSize;
};

const EmbeddedModelAsset* GetAmpModelAsset(const char* token);
const EmbeddedModelAsset* GetStompModelAsset(const char* token);
} // namespace embedded_model
""",
        encoding="ascii",
    )


def _write_source() -> None:
    lines: list[str] = [
        '#include "EmbeddedModelAssets.h"',
        "",
        "#include <array>",
        "#include <cstring>",
        "",
        "namespace embedded_model",
        "{",
        "namespace",
        "{",
    ]

    def append_asset(token: str, file_name: str) -> str:
        json_text = _read_model_json(INPUT_ROOT / file_name)
        json_symbol = f"k{token}Json"
        asset_symbol = f"k{token}Asset"
        lines.append(f"const char {json_symbol}[] =")
        for start in range(0, len(json_text), 2048):
            chunk = json_text[start : start + 2048]
            lines.append(f"  R\"{RAW_STRING_DELIMITER}({chunk}){RAW_STRING_DELIMITER}\"")
        lines.append(";")
        lines.append(f'const EmbeddedModelAsset {asset_symbol} = {{"{token}", {json_symbol}, sizeof({json_symbol}) - 1}};')
        lines.append("")
        return asset_symbol

    amp_symbols = [append_asset(token, file_name) for token, file_name in AMP_ASSETS]
    stomp_symbols = [append_asset(token, file_name) for token, file_name in STOMP_ASSETS]

    lines.append(f"const std::array<const EmbeddedModelAsset*, {len(amp_symbols)}> kAmpAssets = {{{{")
    for symbol in amp_symbols:
        lines.append(f"  &{symbol},")
    lines.append("}};")
    lines.append("")
    lines.append(f"const std::array<const EmbeddedModelAsset*, {len(stomp_symbols)}> kStompAssets = {{{{")
    for symbol in stomp_symbols:
        lines.append(f"  &{symbol},")
    lines.append("}};")
    lines.append("} // namespace")
    lines.append("")
    lines.append("const EmbeddedModelAsset* GetAmpModelAsset(const char* token)")
    lines.append("{")
    lines.append("  if (token == nullptr || *token == '\\0')")
    lines.append("    return nullptr;")
    lines.append("  for (const auto* asset : kAmpAssets)")
    lines.append("  {")
    lines.append("    if (std::strcmp(asset->token, token) == 0)")
    lines.append("      return asset;")
    lines.append("  }")
    lines.append("  return nullptr;")
    lines.append("}")
    lines.append("")
    lines.append("const EmbeddedModelAsset* GetStompModelAsset(const char* token)")
    lines.append("{")
    lines.append("  if (token == nullptr || *token == '\\0')")
    lines.append("    return nullptr;")
    lines.append("  for (const auto* asset : kStompAssets)")
    lines.append("  {")
    lines.append("    if (std::strcmp(asset->token, token) == 0)")
    lines.append("      return asset;")
    lines.append("  }")
    lines.append("  return nullptr;")
    lines.append("}")
    lines.append("} // namespace embedded_model")

    OUTPUT_SOURCE.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> None:
    _write_header()
    _write_source()


if __name__ == "__main__":
    main()
