# SKILLS.md — How the agent should operate in this repo

This file describes preferred “working modes” for an AI agent.

## Mode 1: Codebase mapping
When asked to “find where X happens”, do:
- Identify the exact files/functions involved.
- Provide a short call-chain summary.
- Mention which buffers/threads are involved (audio thread vs UI vs background).

Deliverable: a concise map + pointers (file names + symbol names).

## Mode 2: Minimal patch implementation
When asked to implement:
- Touch the smallest number of files possible.
- Follow existing patterns (UI controls, parameter init, buffer conventions).
- Avoid formatting unrelated code.
- Never introduce audio-thread hazards.

Deliverable: a minimal diff + short rationale.

## Mode 3: Real-time safety review
When asked to review DSP changes:
- Search for allocations/locks/I/O/strings in audio-thread paths.
- Suggest safe alternatives (preallocation, lock-free swap, smoothing).

Deliverable: checklist of hazards (found/none) + fixes.

## Mode 4: Performance investigation (Release-only)
- Assume Debug results are misleading for realtime DSP.
- Suggest measurement approach (CPU usage, buffer size, profiling).
- If optimizing: prefer easy wins (avoid extra passes/copies, simplify loops, enable SIMD where already supported).

Deliverable: top suspected hotspots + low-risk optimizations.

## Communication style
- Be explicit about assumptions.
- Provide exact insertion points (function names + nearby code landmarks).
- If uncertain: propose 2-3 likely locations and tell how to confirm.
