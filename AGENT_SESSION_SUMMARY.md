Last updated: 2026-03-26

Purpose: concise handoff so a new agent can continue without replaying the full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md`
4. `FUTURE_PLAN.md`

## Current repository state
- Active branch: `reduce-amp-switch-artifacts`
- Working tree at handoff: clean
- Recent relevant history before this handover update:
  - `9d378e6` `Reduce IR switching artifacts`
  - `e2fed53` `Reduce amp switching artifacts`
  - `1eb2a65` `Add amp 1 character switch`
  - `c28ba29` `Add amp 2 voicing controls`
  - `6541126` `Refine amp UI behavior and resource loading`
  - `d723495` `Add amp UI bitmap assets`
  - `99e8f61` `New scaffolding for Amp layouts`
  - `f79f5e2` `Improve PingPong dealy effect for more balance`
  - `81fcf89` `Update default loaded assets`
  - `59df315` `Remove soft clipping to avoid unwanted noice from som IRs`
  - `501bab2` `Refine virtual doubler voicing`
  - `f969f5f` `Update handover`

## What is completed now

### 1) Metering overhaul is landed and stable
- Input/output metering is mono/stereo-aware
- Meter UI is vector-based
- Clip warning latch exists and clears by clicking either meter

### 2) Gate/stomp coupling bug is fixed
- Gate is decoupled from stomp bypass
- Right/Ctrl-click bypass on the Stomp section no longer disables the current gate path

### 3) Preset/session context restore is fixed
- Standalone restart restores preset context, name, and dirty `*` state
- Plugin/session restore restores preset context instead of always showing `Unsaved`
- `Input Stereo` remains session/setup state, not preset-owned state

### 4) Plugin preset asset restore is fixed
- Plugin preset/session reopen restores stomp NAM / boost NAM and cab IR state
- Relative preset-path handling was tightened during this work

### 5) Interactive Cab V1 is landed
Outcome:
- The Cab page is now a dual-slot interactive cab mixer instead of the old single-cab blend workflow

Landed work:
- Two always-visible mono cab slots: `Cab A` and `Cab B`
- Per-slot controls:
  - enable
  - source dropdown
  - position slider
  - level
  - pan
  - custom IR loader
- Curated mic mode with 1D interpolation between five aligned captures per mic
- Current curated mic set:
  - `57`
  - `121`
- Slider direction is mirrored correctly for the left slot
- Old single `Cab Blend` concept was removed

### 6) Curated cab assets are embedded for release mode
Outcome:
- Release mode no longer depends on runtime disk reads for the curated cab mic set

Important nuance:
- `Custom IR` loading is still allowed in release mode
- Only the curated mic set is embedded; user-loaded IRs still use the normal file path flow

### 7) Compressor stomp pedal v1 is landed
Outcome:
- A built-in compressor stomp now exists ahead of the amp path

Landed work:
- Controls:
  - `Amount`
  - `Level`
  - `Soft/Hard`
  - on/off
- Built-in compressor DSP was added instead of a NAM-based stomp model
- Control smoothing/state is preallocated and runs in the existing audio callback path

### 8) Boost v2 is landed
Outcome:
- The boost pedal supports two switchable model variants while sharing one control surface

Landed work:
- Separate boost model paths for `A` and `B`
- `A/B` switch in the stomp UI
- Boost drive control added
- Capability/latency/state handling follows the selected boost variant
- Preset/session restore includes both boost model paths

### 9) Amp model variants v1 is landed
Outcome:
- Each amp slot supports multiple model variants, starting with `A` and `B`

Landed work:
- Per-slot `A/B` model storage/loading/persistence
- Selected variant is stored and restored per amp slot
- Shared amp controls remain common to the slot, not per variant
- Release mode preload expects:
  - `Amp1A`
  - `Amp1B`
  - `Amp2A`
  - `Amp2B`
  - `Amp3A`
  - `Amp3B`

### 10) Tuner improvements are landed
Outcome:
- The tuner is much more usable than the old baseline and the major center-jump bug is fixed

Landed work:
- Analyzer now updates every idle tick instead of the old decimated cadence
- High-string acquisition was improved, especially on `B` and high `E`
- Re-picks and semitone-boundary rollovers behave much better
- Tuner now shows numeric signed cents offset
- Sharp notes now render with dual names:
  - `C#/Db`
  - `D#/Eb`
  - `F#/Gb`
  - `G#/Ab`
  - `A#/Bb`
- Tuner UI was narrowed slightly and smoothed for a steadier feel
- Tuner monitor default is now `MUTE`

### 11) Doubler improvement pass is landed
Outcome:
- The virtual doubler now behaves more like a strong double-track preview and less like a generic widener

Landed work:
- Reduced phasey / roomy behavior compared with the earlier baseline
- Reduced take correlation and better preserved separation
- Final tuning favors a fixed recipe with the main control acting more like a taste/presentation control
- Right-side doubler soft clip was removed because curated IRs exposed audible crackle on that path

### 12) Startup/default asset baseline is updated
Outcome:
- Startup and the `Default` preset now load the current asset set instead of the old legacy files

Landed work:
- `NAM_STARTUP_TMPLOAD_DEFAULTS` now controls tmp-load startup defaults instead of `NAM_RELEASE_MODE`
- Default startup/load path expects:
  - `Amp1A/B`
  - `Amp2A/B`
  - `Amp3A/B`
  - `BoostA/B`
- Default cab state is now curated stereo:
  - `57` on one side
  - `121` on the other
  - both at position `40`
- Old runtime references to `Amp1/2/3`, `Boost1`, and `Cab1` were removed from live code paths

### 13) Delay ping-pong balance tweak is landed
Outcome:
- Mono ping-pong delay feels less left-skewed while still keeping the ping-pong identity

### 14) Amp-face scaffolding and slot-specific release UI work are landed
Outcome:
- Amp UI is now structurally separated enough to support different controls, labels, and tone-stack behavior per slot

Structural work:
- Added slot presentation / behavior / resolved-spec scaffolding
- Tone-stack creation is now spec-driven
- Slot-specific layout and control visibility can diverge without forking the whole editor

Current slot behavior:
- Amp 1:
  - `CHARACTER` switch added on the left side
  - switch toggles amp model `A/B`
  - caption is white to match that face
- Amp 2:
  - `Depth` knob removed
  - stacked `DEPTH` and `SCOOP` pushbuttons added
  - dedicated `Amp2ToneStack` exists for its slot-specific voicing behavior
- Amp 3:
  - custom on/off switch art
  - custom `BRIGHT/NORMAL` variant switch
  - `DEPTH` switch replaces the old depth knob behavior
  - `Presence` and `Depth` knobs are removed from that face
  - bright mode also applies a hidden presence boost

Asset/rendering updates:
- Amp knobs and amp on/off switches support `@2x` / `@3x` bitmap variants
- Amp knob rotation now uses direct bitmap rotation instead of rotating an intermediate layer
- Keep that direct-rotation path; it fixed the visible wobble problem with the corrected exports

### 15) Amp switching artifact reduction is landed on the current branch
Outcome:
- Amp variant switching in `Normalized` mode is now materially cleaner
- Amp slot switching now uses a short acceptable masked handoff instead of the earlier click/clonk behavior
- Hard amp/stomp path toggles were also cleaned up during the same work

Important landed details:
- The output-mode clue mattered: a large part of the remaining variant click was the normalization gain jump, not only the model swap
- The fix set that proved out was:
  - committed amp slot shadowing for tone stack / pre gain / master
  - normalized output-gain smoothing
  - same-slot variant crossfade
  - short slot-transition masking
  - path-toggle fade for hard amp/stomp section toggles
- Settled constants on the current branch:
  - `kAmpSlotSwitchDeClickSamples = 512`
  - `kAmpModelVariantCrossfadeSamples = 512`
  - `kPathToggleTransitionSamples = 512`
  - `kAmpSlotTransitionSamples = 3072`
  - `kOutputGainSmoothTimeSeconds = 0.02`

### 16) Cab / IR switching artifact reduction is landed on the current branch
Outcome:
- Cab enable/source/IR changes no longer rely on the failed whole-output mute path
- IR changes now use slot-local cab-stage old/new IR crossfades
- Curated slider boundary changes, curated source changes, and custom IR arrow changes are materially cleaner

Important landed details:
- The successful fix was not mute-around-swap; it was preserving the old IR branch and crossfading cab output against the new IR branch
- Curated IR pairs are now staged together so each branch has a complete primary/secondary pair during the blend
- The long IR crossfade is intentional because convolver history changes abruptly
- Settled constant on the current branch:
  - `kIRTransitionSamples = 12288`

## Important current conclusions
- Do not reopen tuner work unless the user explicitly asks
- The doubler is now on a good-enough baseline; treat further changes as product-taste tuning, not urgent cleanup
- The amp-face spec scaffolding is intentional and should be used for future slot/model divergence instead of adding more hardcoded one-offs
- `Custom IR` support in release mode is intentional and should remain
- `Input Stereo` should remain session/setup state, not preset-owned state
- Do not reopen amp/IR artifact work immediately unless the user hears a specific remaining regression; the current branch is the tuned baseline for that investigation

## Current plugin/app state
- The current branch now includes:
  - Interactive Cab V1
  - embedded curated cab IRs
  - built-in compressor stomp v1
  - boost v2 with `A/B` model selection
  - amp model variants v1
  - improved virtual doubler baseline
  - updated startup/default asset loading
  - amp-face scaffolding and slot-specific amp UI controls
  - tuner improvements and tuner UI polish
- `reduce-amp-switch-artifacts` additionally includes:
  - amp variant/slot artifact reduction
  - cab/IR switching artifact reduction
- Release mode currently supports:
  - embedded curated cab IRs
  - preloaded amp variant assets `Amp1A/B`, `Amp2A/B`, `Amp3A/B`
  - user-loaded custom IRs

## Build/config baseline to preserve
- Audio/performance validation: `Release | x64` only
- Standalone audio test: run without debugger (`Ctrl+F5`)
- Do not evaluate DSP performance in Debug
- No `Release | x64` build was run from this shell for the amp/IR artifact commits because `msbuild` / `devenv` were not available on PATH here
- Do not assume local `config.h` matches committed defaults during user testing

## Current policy notes
- Audio-thread rules still apply:
  - no allocations
  - no locks or waits
  - no file/network/UI/logging in callback
  - no exceptions across the callback
- Keep diffs small and reviewable
- Dev-mode policy is active:
  - breaking changes are acceptable if they improve architecture or iteration speed

## Suggested next coding step
1. Build and validate the current branch in `Release | x64`
2. If the user is satisfied, merge or cherry-pick:
  - `e2fed53` `Reduce amp switching artifacts`
  - `9d378e6` `Reduce IR switching artifacts`
3. Otherwise, move on to the next user-directed task from the new stable baseline instead of continuing artifact experimentation

## Starter prompt for the next agent
You are continuing work in `D:\\Dev\\NAMPlugin` on branch `reduce-amp-switch-artifacts`.

Read first, in this exact order:
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md`
4. `FUTURE_PLAN.md`

Then confirm current repo/submodule state:
- `git status --short`
- `git branch --show-current`
- `git rev-parse --short HEAD`
- `git submodule status iPlug2`
- `git -C iPlug2 remote -v`

Current baseline:
1. Metering overhaul is landed and stable
2. Gate is decoupled from stomp bypass
3. Standalone/plugin preset context restore is fixed
4. Plugin preset stomp NAM and cab IR restore is fixed
5. Interactive Cab V1 is merged to `main`
6. Release mode embeds curated cab IRs and amp variant assets while still allowing custom IR loading
7. Compressor stomp pedal v1 is merged to `main`
8. Boost v2 with `A/B` model selection is merged to `main`
9. Amp model variants v1 is merged to `main`
10. Doubler improvements are merged to `main`
11. Startup/default asset loading now uses `Amp1A/B ... Amp3A/B`, `BoostA/B`, and curated `57/121` stereo cabs
12. Amp-face scaffolding and slot-specific amp UI updates are merged to `main`
13. Tuner improvements and tuner UI polish are merged to `main`
14. Branch `reduce-amp-switch-artifacts` contains:
  - `e2fed53` `Reduce amp switching artifacts`
  - `9d378e6` `Reduce IR switching artifacts`
15. Settled artifact-tuning constants on that branch:
  - `kAmpSlotSwitchDeClickSamples = 512`
  - `kAmpModelVariantCrossfadeSamples = 512`
  - `kPathToggleTransitionSamples = 512`
  - `kAmpSlotTransitionSamples = 3072`
  - `kOutputGainSmoothTimeSeconds = 0.02`
  - `kIRTransitionSamples = 12288`

Current working tree status:
1. The tree should be clean
2. Do not reopen tuner work unless the user explicitly asks

Suggested next task unless the user redirects:
1. First verify the current branch in `Release | x64` if that has not already been done locally
2. If the user is happy with the behavior, prepare merge/cherry-pick of:
  - `e2fed53`
  - `9d378e6`
3. Otherwise continue from this branch baseline; do not restart the old amp/IR click investigation from scratch
