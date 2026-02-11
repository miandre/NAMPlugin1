# NeuralAmpModeler DSP Stage Insertion Guide

This guide explains where to insert new DSP stages in `NeuralAmpModeler::ProcessBlock`, and how to do it safely.

Primary flow reference: `NeuralAmpModeler/NeuralAmpModeler.cpp:284`  
Detailed chain map: `NeuralAmpModeler/PROCESSBLOCK_SIGNAL_CHAIN.md`

## Goals

- Add new DSP with minimal regression risk.
- Preserve real-time safety on the audio thread.
- Keep stage ordering intentional and easy to review.

## Audio-Thread Safety Rules (Non-Negotiable)

Inside `ProcessBlock` and anything it calls:

- No heap allocation (`new`, `std::vector` growth, `std::string` building, etc.).
- No locks, waits, sleeps, or blocking synchronization.
- No file/network I/O, no logging/printing, no UI calls.
- No exception paths that can throw across callback execution.

Repository policy references:
- `AGENTS.md` (root)
- `NeuralAmpModeler/AGENTS.md`
- `.aiassistant/rules/DSP.md`

## Current Stage Anchors in `ProcessBlock`

Reference block: `NeuralAmpModeler/NeuralAmpModeler.cpp:297` to `NeuralAmpModeler/NeuralAmpModeler.cpp:359`

1. `_PrepareBuffers(...)`
2. `_ProcessInput(...)`
3. `_ApplyDSPStaging()`
4. Noise gate trigger (optional)
5. Model
6. Noise gate gain (optional)
7. Tone stack EQ (optional)
8. IR (optional)
9. HPF (always)
10. `_ProcessOutput(...)`
11. `_UpdateMeters(...)`

## Where to Insert New Stages

## 1) Pre-Model Insertion (between input and model)

Best for:
- Input conditioning
- Dynamics/keying preprocessors
- Feature extraction that must affect model drive

Insert point:
- After `_ProcessInput(...)` / staging, before `mModel->process(...)`

Buffer contract:
- Input should be internal mono pointer stream (`mInputPointers` or trigger output path).
- Output should be a stable pointer (`sample**`) used as model input.

Key caution:
- Any level-dependent behavior changes model excitation significantly.

## 2) Post-Model / Pre-EQ Insertion

Best for:
- Saturation trim stages
- Model post-correction before user EQ

Insert point:
- After model/fallback and before tone stack call.

Buffer contract:
- Input likely `mOutputPointers` or `gateGainOutput`.
- Return pointer should feed the next stage (`toneStackOutPointers` path).

Key caution:
- If noise gate is active, remember gate gain is currently applied after model.

## 3) Between EQ and IR

Best for:
- Cabinet pre-emphasis/de-emphasis
- Tonal shaping intended to feed convolution

Insert point:
- Between `mToneStack->Process(...)` result and `mIR->Process(...)`.

Key caution:
- This position changes what enters IR convolution, often highly audible.

## 4) Post-IR / Pre-HPF

Best for:
- Convolution output shaping
- Cabinet post-processing before DC block

Insert point:
- Between IR output pointer and `mHighPass.Process(...)`.

Key caution:
- Ensure DC handling expectations stay valid if introducing nonlinear ops.

## 5) Post-HPF / Pre-Output Gain

Best for:
- Final protection/limiting (if needed)
- Last-mile conditioning before output scaling/broadcast

Insert point:
- Between `hpfPointers` and `_ProcessOutput(...)`.

Key caution:
- Keep latency and host compensation implications explicit.

## Preferred Integration Pattern

Use this pattern to keep diffs reviewable and deterministic:

1. Add new DSP object as a member (preallocated/owned, no per-block allocations).
2. Initialize/reset in existing lifecycle paths (`OnReset`, setup methods).
3. Add/update params in `EParams`, constructor init, and `OnParamChange`.
4. In `ProcessBlock`, insert a single pointer handoff line:
   - `sample** stageOut = mNewStage.Process(stageIn, numChannelsInternal, numFrames);`
5. Pass `stageOut` to the next existing stage.

## Real-Time Safe State/Threading Pattern

For heavy resources (models, IRs, tables):

- Load/build off audio thread.
- Store into staged pointer/object.
- Swap at `_ApplyDSPStaging()` boundary in `ProcessBlock`.

Reference staging method:
- `NeuralAmpModeler/NeuralAmpModeler.cpp:549`

Do not:
- Parse files in `ProcessBlock`
- Reallocate large buffers in `ProcessBlock`
- Reconfigure heavy internals every sample/block unless already preallocated and bounded

## Parameter Wiring Checklist for New Stage

When adding a new control parameter:

1. Add enum ID in `EParams` (`NeuralAmpModeler/NeuralAmpModeler.h:30`).
2. Initialize in constructor (`GetParam(...)->Init...`) in `NeuralAmpModeler.cpp`.
3. Handle updates in `OnParamChange(...)` (`NeuralAmpModeler/NeuralAmpModeler.cpp:464`).
4. If UI toggle/state affects enablement, wire `OnParamChangeUI(...)`.
5. If continuous, consider smoothing strategy to avoid zipper noise.

## Buffer and Pointer Rules for New Stage

- Treat `sample**` as non-owning views over stage-owned buffers.
- Stage `Process(...)` should return pointers that remain valid through the call chain for that block.
- Do not return pointers to temporary/local stack storage.
- Keep channel/frame assumptions explicit (current internal path is mono).

## Latency and Reporting

If your new stage adds latency:

1. Add stage latency query in `_UpdateLatency()`.
2. Sum with existing model latency.
3. Ensure host-visible latency updates when stage/model state changes.

Reference:
- `NeuralAmpModeler/NeuralAmpModeler.cpp:886`

## Metering Considerations

Current metering taps:
- Input meter source: `mInputPointers`
- Output meter source: final host `outputs`

Reference:
- `NeuralAmpModeler/NeuralAmpModeler.cpp:359`
- `NeuralAmpModeler/NeuralAmpModeler.cpp:902`

If you need meter visibility for a new stage:
- Add explicit meter tap after that stage.
- Keep it read-only and avoid heavy operations.

## Recommended Review Template (for PRs)

Include this in change descriptions:

1. Insertion point (exact line neighborhood in `ProcessBlock`)
2. Input/output pointer variables for the new stage
3. Parameters affecting the stage
4. Real-time safety statement (alloc/lock/I/O free in audio thread)
5. Latency impact (none / added with value and compensation path)
6. Audible verification plan (A/B and expected behavioral delta)

## Verification Steps (Manual)

1. Build `Release|x64`.
2. Run standalone without debugger (`Ctrl+F5` in VS).
3. Load a known model + IR and confirm baseline.
4. Toggle/adjust new stage params across extremes.
5. Confirm:
   - no crackles/dropouts under normal buffer sizes
   - expected tonal/dynamics behavior
   - no broken bypass ordering vs existing chain
6. Re-check host latency reporting if stage can add latency.

