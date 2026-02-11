---
apply: by file patterns
patterns: NeuralAmpModeler/**
---

# NeuralAmpModeler/AGENTS.md — DSP / audio-thread stricter rules

These rules apply to changes under the NeuralAmpModeler/ directory.
They are stricter than the repo root rules and take precedence for DSP code.

## Assume audio thread by default
- Treat all DSP-related code here as audio-thread-adjacent unless clearly proven otherwise.
- If uncertain, do not add anything that could block or allocate.

## Hard bans (stronger emphasis)
In `ProcessBlock()` and any DSP path:
- NO heap allocation, including hidden allocations (std::string concatenation, vector growth, iostreams, fmt, regex).
- NO locks, atomics used incorrectly, or any waiting.
- NO disk access, config reads, environment lookups, or logging.
- NO exception throwing; avoid any code that might throw.

## Buffer & state rules
- Preallocate buffers (members) and reuse them; never resize in ProcessBlock.
- If you need dynamic sizing, do it in prepare/reset paths (e.g., sample rate/block size changes), not per block.
- Keep per-sample/per-frame loops simple and branch-light.

## Thread-safe swapping for models/IRs
- Loading/creating models or IR convolution must occur off the audio thread.
- Swapping in new objects must be lock-free or use safe patterns (atomic pointer swap / double-buffering).
- If you must coordinate, do it on the UI thread and keep audio thread reads wait-free.

## Change discipline
- Touch the minimum number of DSP files.
- Do not change UI/layout code from this folder unless the task explicitly requires it.
- If a change could affect sound/latency/CPU, provide a short “impact note” and verification guidance.
