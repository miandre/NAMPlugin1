# NeuralAmpModeler `ProcessBlock` Signal Chain Map

This document maps the runtime audio path in `NeuralAmpModeler::ProcessBlock`:

`input -> staging -> gate -> model -> EQ -> IR -> HPF -> output`

Primary entry point: `NeuralAmpModeler/NeuralAmpModeler.cpp:284`

## Call Order Overview

1. `_PrepareBuffers(...)`
2. `_ProcessInput(...)`
3. `_ApplyDSPStaging()`
4. Noise gate trigger (optional): `mNoiseGateTrigger.Process(...)`
5. Model: `mModel->process(...)` or `_FallbackDSP(...)`
6. Noise gate gain (optional): `mNoiseGateGain.Process(...)`
7. Tone stack EQ (optional): `mToneStack->Process(...)`
8. IR (optional): `mIR->Process(...)`
9. HPF: `mHighPass.Process(...)`
10. `_ProcessOutput(...)`
11. `_UpdateMeters(...)` (meter side-path)

Reference: `NeuralAmpModeler/NeuralAmpModeler.cpp:297` through `NeuralAmpModeler/NeuralAmpModeler.cpp:359`

## Stage 1: Input

### What happens
- Host input is collapsed to internal mono and input gain is applied.
- Internal processing channel count is fixed at mono (`kNumChannelsInternal = 1`).

### Functions
- `_PrepareBuffers(...)` at `NeuralAmpModeler/NeuralAmpModeler.cpp:771`
- `_ProcessInput(...)` at `NeuralAmpModeler/NeuralAmpModeler.cpp:810`

### Buffers and pointers
- Read from host: `inputs[c][s]`
- Write to internal mono buffer: `mInputArray[0][s]`
- Pointer view used downstream: `mInputPointers[0] = mInputArray[0].data()`
  - Pointer assignment at `NeuralAmpModeler/NeuralAmpModeler.cpp:799`

### Parameters affecting this stage
- `kInputLevel`
- `kCalibrateInput`
- `kInputCalibrationLevel`
- These are folded into cached scalar `mInputGain` in `_SetInputGain()`
  - `NeuralAmpModeler/NeuralAmpModeler.cpp:644`
  - Calibration adjustment: `NeuralAmpModeler/NeuralAmpModeler.cpp:650`
  - Amplitude conversion: `NeuralAmpModeler/NeuralAmpModeler.cpp:652`

### Notes
- In DAW builds (`#ifndef APP_API`) input is averaged across external input channels via `gain /= nChansIn`.
  - `NeuralAmpModeler/NeuralAmpModeler.cpp:827`

## Stage 2: Staging

### What happens
- Safe handoff of staged DSP objects into live pointers and removal of flagged modules.
- No per-sample DSP transform in this stage.

### Function
- `_ApplyDSPStaging()` at `NeuralAmpModeler/NeuralAmpModeler.cpp:549`

### Buffers and pointers
- Object pointer swaps only:
  - `mStagedModel -> mModel`
  - `mStagedIR -> mIR`
  - optional clears (`mModel = nullptr`, `mIR = nullptr`)

### Parameters affecting this stage
- None directly.
- Driven by state flags from UI/messages:
  - `mShouldRemoveModel`, `mShouldRemoveIR`
  - message hooks: `NeuralAmpModeler/NeuralAmpModeler.cpp:505`, `NeuralAmpModeler/NeuralAmpModeler.cpp:506`

## Stage 3: Gate (Trigger)

### What happens
- Computes gain-reduction envelope from input signal.
- Trigger output audio is pass-through copy of input audio; gain curve is shared to listeners.

### Function calls
- In `ProcessBlock`: `mNoiseGateTrigger.Process(mInputPointers, ...)`
  - `NeuralAmpModeler/NeuralAmpModeler.cpp:317`
- Trigger implementation:
  - `NeuralAmpModeler/AudioDSPTools/dsp/NoiseGate.cpp:36`
  - Listener push: `NeuralAmpModeler/AudioDSPTools/dsp/NoiseGate.cpp:103`
  - Pass-through copy: `NeuralAmpModeler/AudioDSPTools/dsp/NoiseGate.cpp:108`

### Buffers and pointers
- Input pointer: `mInputPointers`
- Output pointer from trigger: `triggerOutput` (trigger-owned output buffers)

### Parameters affecting this stage
- `kNoiseGateActive` (enables trigger path)
  - `NeuralAmpModeler/NeuralAmpModeler.cpp:301`
- `kNoiseGateThreshold`
  - read at `NeuralAmpModeler/NeuralAmpModeler.cpp:309`
- Additional gate dynamics are constants in `ProcessBlock`:
  - `time=0.01`, `ratio=0.1`, `open=0.005`, `hold=0.01`, `close=0.05`

## Stage 4: Model

### What happens
- Runs NAM model on mono stream.
- If no model loaded, fallback copies input to output.

### Function calls
- Model path: `mModel->process(triggerOutput[0], mOutputPointers[0], nFrames)`
  - `NeuralAmpModeler/NeuralAmpModeler.cpp:322`
- Fallback path: `_FallbackDSP(...)`
  - `NeuralAmpModeler/NeuralAmpModeler.cpp:326`
  - implementation: `NeuralAmpModeler/NeuralAmpModeler.cpp:603`

### Buffers and pointers
- Model input pointer: `triggerOutput[0]` (mono)
- Model output pointer: `mOutputPointers[0]` (backed by `mOutputArray[0]`)

### Parameters affecting this stage
- No direct plugin parameters in the process call.
- Model choice/state comes from staging (`mModel` pointer).

### Resampling note
- `ResamplingNAM` may run encapsulated model through `ResamplingContainer` if model SR differs from host SR:
  - `NeedToResample()` check: `NeuralAmpModeler/NeuralAmpModeler.h:171`
  - direct process: `NeuralAmpModeler/NeuralAmpModeler.h:144`
  - resampled process: `NeuralAmpModeler/NeuralAmpModeler.h:148`

## Stage 5: Gate (Gain Application)

### What happens
- Applies gain-reduction envelope (from trigger) to model output.

### Function call
- `mNoiseGateGain.Process(mOutputPointers, ...)` when gate active
  - call site: `NeuralAmpModeler/NeuralAmpModeler.cpp:330`
  - implementation: `NeuralAmpModeler/AudioDSPTools/dsp/NoiseGate.cpp:149`

### Buffers and pointers
- Input pointer: `mOutputPointers` (model output)
- Output pointer: `gateGainOutput` (gain-owned output buffers)

### Parameters affecting this stage
- `kNoiseGateActive` (if false, bypasses and uses `mOutputPointers`)

## Stage 6: EQ (Tone Stack)

### What happens
- 3-filter cascade: bass shelf -> mid peaking -> treble shelf.

### Function calls
- In `ProcessBlock`: `mToneStack->Process(gateGainOutput, ...)`
  - `NeuralAmpModeler/NeuralAmpModeler.cpp:333`
- Tone stack chain:
  - `mToneBass.Process(...)` -> `mToneMid.Process(...)` -> `mToneTreble.Process(...)`
  - `NeuralAmpModeler/ToneStack.cpp:6` to `NeuralAmpModeler/ToneStack.cpp:8`

### Buffers and pointers
- Input pointer: `gateGainOutput`
- Intermediate pointers: `bassPointers`, `midPointers`
- Output pointer: `toneStackOutPointers` (treble stage output)

### Parameters affecting this stage
- `kEQActive` toggle
  - `NeuralAmpModeler/NeuralAmpModeler.cpp:302`
- `kToneBass`, `kToneMid`, `kToneTreble`
  - set via `OnParamChange` at:
    - `NeuralAmpModeler/NeuralAmpModeler.cpp:476`
    - `NeuralAmpModeler/NeuralAmpModeler.cpp:477`
    - `NeuralAmpModeler/NeuralAmpModeler.cpp:478`
- Mappings in `ToneStack.cpp`:
  - Bass: `4.0 * (val - 5.0)` dB at 150 Hz
  - Mid: `3.0 * (val - 5.0)` dB at 425 Hz
  - Treble: `2.0 * (val - 5.0)` dB at 1800 Hz

## Stage 7: IR

### What happens
- Convolution IR processing (mono internal path).
- If disabled or absent, passes through previous stage pointer.

### Function call
- `mIR->Process(toneStackOutPointers, ...)` when `mIR != nullptr && kIRToggle`
  - call site: `NeuralAmpModeler/NeuralAmpModeler.cpp:338`
  - implementation: `NeuralAmpModeler/AudioDSPTools/dsp/ImpulseResponse.cpp:38`

### Buffers and pointers
- Input pointer: `toneStackOutPointers`
- Output pointer: `irPointers` (IR-owned output buffers when active)

### Parameters affecting this stage
- `kIRToggle`
  - `NeuralAmpModeler/NeuralAmpModeler.cpp:337`
- IR file content/path is staged state (`mIR`), not a continuous parameter.

## Stage 8: HPF (DC Blocker)

### What happens
- Always applies post-IR high-pass filter for DC offset cleanup.

### Function calls
- `mHighPass.SetParams(...)` and `mHighPass.Process(irPointers, ...)`
  - `NeuralAmpModeler/NeuralAmpModeler.cpp:345`
  - `NeuralAmpModeler/NeuralAmpModeler.cpp:347`

### Buffers and pointers
- Input pointer: `irPointers`
- Output pointer: `hpfPointers` (filter-owned output buffers)

### Parameters affecting this stage
- No user-facing parameter.
- Fixed cutoff constant: `kDCBlockerFrequency = 5.0`
  - `NeuralAmpModeler/NeuralAmpModeler.cpp:23`

## Stage 9: Output

### What happens
- Applies output gain and broadcasts internal mono to all connected output channels.

### Function
- `_ProcessOutput(hpfPointers, outputs, ...)`
  - call site: `NeuralAmpModeler/NeuralAmpModeler.cpp:355`
  - implementation: `NeuralAmpModeler/NeuralAmpModeler.cpp:838`

### Buffers and pointers
- Read: `hpfPointers[0]` (mono internal)
- Write: `outputs[cout][s]` for each external output channel

### Parameters affecting this stage
- `kOutputLevel`
- `kOutputMode` (`Raw`, `Normalized`, `Calibrated`)
  - mode handling in `_SetOutputGain()`:
    - `NeuralAmpModeler/NeuralAmpModeler.cpp:660`
    - normalized loudness adjust: `NeuralAmpModeler/NeuralAmpModeler.cpp:668`
    - calibrated adjust: `NeuralAmpModeler/NeuralAmpModeler.cpp:676`
- Final cached gain: `mOutputGain = DBToAmp(gainDB)` at `NeuralAmpModeler/NeuralAmpModeler.cpp:683`

### Notes
- In app build (`APP_API`), output is clamped to `[-1.0, 1.0]`.
  - `NeuralAmpModeler/NeuralAmpModeler.cpp:850`

## Meter Side-Path (Not in Main Audio Chain)

- `_UpdateMeters(mInputPointers, outputs, ...)` at `NeuralAmpModeler/NeuralAmpModeler.cpp:359`
- Uses:
  - input meter source: `mInputPointers` (post input-stage signal)
  - output meter source: final `outputs`
- Implementation: `NeuralAmpModeler/NeuralAmpModeler.cpp:902`

## Parameter-to-Stage Quick Map

| Parameter | Stage(s) | Effect |
|---|---|---|
| `kInputLevel` | Input | Base input gain (`mInputGain`) |
| `kCalibrateInput` | Input | Enables model-based input calibration offset |
| `kInputCalibrationLevel` | Input, Output (calibrated mode) | Input calibration target and calibrated output mode reference |
| `kNoiseGateActive` | Gate | Enables trigger + gain application path |
| `kNoiseGateThreshold` | Gate | Trigger threshold |
| `kEQActive` | EQ | Enables tone stack |
| `kToneBass` | EQ | Bass shelf gain |
| `kToneMid` | EQ | Mid peak gain/Q behavior |
| `kToneTreble` | EQ | Treble shelf gain |
| `kIRToggle` | IR | Enables/disables IR processing |
| `kOutputLevel` | Output | Base output gain |
| `kOutputMode` | Output | Raw/Normalized/Calibrated gain behavior |

Parameter enum reference: `NeuralAmpModeler/NeuralAmpModeler.h:30`

## Main Pointer Handoff Summary

1. `inputs` -> `_ProcessInput` -> `mInputPointers`
2. `mInputPointers` -> gate trigger -> `triggerOutput`
3. `triggerOutput[0]` -> model -> `mOutputPointers[0]`
4. `mOutputPointers` -> gate gain -> `gateGainOutput`
5. `gateGainOutput` -> tone stack -> `toneStackOutPointers`
6. `toneStackOutPointers` -> IR -> `irPointers`
7. `irPointers` -> HPF -> `hpfPointers`
8. `hpfPointers` -> `_ProcessOutput` -> external `outputs`

