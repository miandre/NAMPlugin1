# AGENT_SESSION_SUMMARY.md

Last updated: 2026-02-26

Purpose: concise handoff for new agents so work can continue without replaying full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md` (this file)

## Current repository state
- Branch: `main`
- Remote: `origin` = `https://github.com/miandre/NAMPlugin1.git`
- `main` is synced to `origin/main` at commit `6eeb81e`.
- Working tree note: untracked local folder exists: `NeuralAmpModeler/resources/tmpLoad/` (not committed, not part of product features).

## Merged work summary
- `28d950c`: FX section v1 (EQ/Delay/Reverb UI + DSP).
- `4d867e5`: FX improvements and reverb/delay voicing refinements.
- `f38e7d3`: registered new amp background bitmaps (`Amp1/2/3Background`).
- `d64edad` + `89bb8c2`: settings page/layout migration + 3-slot amp UI behavior.
- `6eeb81e`: amp-slot state separation and switch/model activation fixes.

## Handover A: FX section changes
Files:
- `NeuralAmpModeler/NeuralAmpModeler.h`
- `NeuralAmpModeler/NeuralAmpModeler.cpp`

Current FX params (append-only enum additions are already in place):
- EQ: `kFXEQActive`, `kFXEQBand31Hz..kFXEQBand16kHz`, `kFXEQOutputGain`.
- Delay: `kFXDelayActive`, `kFXDelayMix`, `kFXDelayTimeMs`, `kFXDelayFeedback`, `kFXDelayLowCutHz`, `kFXDelayHighCutHz`.
- Reverb: `kFXReverbActive`, `kFXReverbMix`, `kFXReverbDecay`, `kFXReverbPreDelayMs`, `kFXReverbTone`, `kFXReverbLowCutHz`, `kFXReverbHighCutHz`, `kFXReverbMode` (room/hall style switch).

FX chain location in `ProcessBlock()` (mono internal path):
1. user HPF/LPF (post-cab filters)
2. FX EQ
3. FX Delay
4. FX Reverb
5. DC blocker HPF

Implementation notes:
- Delay and reverb allocate buffers/state in `OnReset()` only.
- No heap allocation/locks/I/O/logging in audio callback paths.
- Delay uses fractional reads and smoothing.
- Reverb is an algorithmic mono design with early reflections, pre-diffusion, FDN-style late network, damping/modulation, and room/hall-dependent voicing constants.

## Handover B: UI layout changes
Files:
- `NeuralAmpModeler/NeuralAmpModeler.cpp`
- `NeuralAmpModeler/NeuralAmpModeler.h`
- `NeuralAmpModeler/NeuralAmpModelerControls.h`
- `NeuralAmpModeler/config.h`
- `NeuralAmpModeler/resources/main.rc`

What changed:
- Settings page now hosts model pickers for Amp 1, Amp 2, Amp 3, and Stomp.
- Amp/stomp model picker controls were moved out of page-local placement into settings page child controls.
- Calibration controls were repositioned in settings layout (under output mode area).
- Footer amp selector now has 3 slots (`kCtrlTagAmpSlot1/2/3`) on amp page.
- Main amp background switches by active slot via `kCtrlTagMainBackground`:
  - slot 1 -> `Amp1Background`
  - slot 2 -> `Amp2Background` (default startup)
  - slot 3 -> `Amp3Background`
- File browser UX cleanup: clear (`X`) behavior is retained and globe/open behavior was removed from the picker UX.

## Handover C: amp separation (runtime behavior)
Files:
- `NeuralAmpModeler/NeuralAmpModeler.h`
- `NeuralAmpModeler/NeuralAmpModeler.cpp`

What is separated per amp slot at runtime:
- Slot model path (`mAmpNAMPaths[3]`).
- Slot amp state (`AmpSlotState`): model toggle, EQ active, pre gain, bass/mid/treble/presence/depth, master.
- Slot tone stack instance (`mToneStacks[3]`) and per-slot tone params.

Key methods:
- `_SelectAmpSlot(int)`
- `_CaptureAmpSlotState(int)`
- `_ApplyAmpSlotState(int)`
- `_ApplyAmpSlotStateToToneStack(int)`
- `_ApplyCurrentAmpParamsToActiveToneStack()`

Behavior details:
- Slot switch captures old slot state, stages/clears incoming slot model, then applies incoming slot state.
- This order avoids audible old-model/new-knob mismatch during switch.
- `modelToggleTouched` logic prevents unintended bypass for slots with model paths.
- Small de-click smoothing on slot switch is enabled (`kAmpSlotSwitchDeClickSamples`).

Important limitation:
- Runtime slot separation works for live switching.
- Full per-slot persistence in plugin serialized state is not fully implemented yet; serialization still stores legacy single `NAMPath` (`mNAMPath`) plus IR paths.

## Verification reminders
- Validate in `Release|x64` only.
- Run standalone via `Ctrl+F5` in Visual Studio (not under debugger for DSP judgments).
- User preference: do not run builds from agent unless explicitly asked.

## Starter prompt for a new agent (copy/paste)
You are continuing work in `D:\\Dev\\NAMPlugin` on branch `main`.

Read first, in this exact order:
1) `AGENTS.md`
2) `SKILLS.md`
3) `AGENT_SESSION_SUMMARY.md`

Then read these implementation files:
4) `NeuralAmpModeler/NeuralAmpModeler.h` (params, ctrl tags, slot state structs)
5) `NeuralAmpModeler/NeuralAmpModeler.cpp` (UI layout, slot switching, DSP chain)
6) `NeuralAmpModeler/NeuralAmpModelerControls.h` (file browser/picker behavior)
7) `NeuralAmpModeler/Unserialization.cpp` (current serialization limits)
8) `NeuralAmpModeler/config.h` and `NeuralAmpModeler/resources/main.rc` (bitmap/resource registration)

Current status:
- FX section is implemented and tuned (EQ/Delay/Reverb, room/hall mode, low/high cuts).
- Settings page contains 3 amp pickers + stomp picker.
- Amp footer selects slot, background, and corresponding model.
- Runtime per-slot amp controls are separated and switching behavior is stabilized.

Task style requirements:
- Propose minimal diffs first.
- Keep parameter enum additions append-only.
- Preserve audio-thread safety (no alloc/locks/I/O/logging in callback path).
- Do not run builds unless explicitly requested by user.
