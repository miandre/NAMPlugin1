---
apply: by file patterns
patterns: NeuralAmpModeler/**
---

# Feature Implementation Rules (NeuralAmpModeler)

Use these rules whenever implementing a new feature under `NeuralAmpModeler/**`.
These rules extend `General.md` and `DSP.md`.

## Required planning format (before edits)

List:
1. Exact files/symbols to touch.
2. Chosen insertion stage in `ProcessBlock`:
   - input
   - staging
   - gate
   - model
   - EQ
   - IR
   - HPF
   - output
3. Why this insertion point is correct for the requested behavior.
4. Buffer/pointer handoff for the new stage (input pointer -> output pointer).
5. Parameter list that affects the new stage.

Reference docs:
- `NeuralAmpModeler/PROCESSBLOCK_SIGNAL_CHAIN.md`
- `NeuralAmpModeler/DSP_STAGE_INSERTION_GUIDE.md`

## Insertion-point discipline

- Add new DSP in the smallest valid insertion point.
- Preserve existing signal-chain order unless explicitly requested.
- Do not silently move existing stages.
- If stage order changes, call it out as a behavioral change and justify it.

## Buffer/pointer contract

- Keep `sample**` pointer flow explicit in code and review notes.
- Do not return pointers to temporary storage.
- Preallocate and reuse stage-owned buffers.
- Keep internal mono assumptions explicit unless task requires multi-channel changes.

## Real-time safety constraints

In `ProcessBlock()` paths and direct/indirect callees:
- No allocations.
- No locks/waits.
- No file/network/UI/logging I/O.
- No exception-throwing paths.

Heavy setup (models, IRs, parsing, file work):
- Must happen off audio thread.
- Must be swapped using staged/live handoff patterns.

## Parameter and UI wiring requirements

For new user controls:
- Add param ID and init with clear name/range/unit.
- Wire `OnParamChange` behavior at a single clear stage.
- Keep UI changes minimal and consistent with existing controls.
- Keep layout indexing decoupled from enum ordering.
- Consider smoothing for continuous controls.

## Latency and metering requirements

- If added stage introduces latency, update latency reporting path.
- If metering behavior should include new stage, state whether meter tap points changed.

## Required final response sections

When feature edits are complete, include:
1. Stage insertion summary (where and why).
2. Files/symbols changed.
3. Buffer/pointer flow summary.
4. Parameter impact summary.
5. Real-time safety note.
6. Verification steps (Release|x64, audible/behavior checks).

Include checklist footer:
- [ ] Release|x64 builds
- [ ] No allocations/locks/I/O/logging in audio thread paths
- [ ] UI changes minimal and consistent
- [ ] How to verify audibly / behaviorally

