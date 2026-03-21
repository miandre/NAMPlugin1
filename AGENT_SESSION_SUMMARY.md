Last updated: 2026-03-21

Purpose: concise handoff so a new agent can continue without replaying the full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md`
4. `FUTURE_PLAN.md`

## Current repository state
- Active branch: `main`
- Branch HEAD: `1ef0be1`
- Working tree at handoff: dirty
- Current staged-only changes:
  - `NeuralAmpModeler/resources/img/Hardware/VerticalSwitch_DOWN.png`
  - `NeuralAmpModeler/resources/img/Hardware/VerticalSwitch_UP.png`
- Current unstaged changes:
  - `NeuralAmpModeler/NeuralAmpModeler.cpp`
  - `NeuralAmpModeler/NeuralAmpModeler.h`
  - `NeuralAmpModeler/NeuralAmpModelerControls.h`
  - `NeuralAmpModeler/Unserialization.cpp`
  - `NeuralAmpModeler/config.h`
  - `NeuralAmpModeler/resources/img/Hardware/HorizonalSwitch_L.png`
  - `NeuralAmpModeler/resources/img/Hardware/HorizonalSwitch_R.png`
  - `NeuralAmpModeler/resources/img/Hardware/VerticalSwitch_UP.png`
  - `NeuralAmpModeler/resources/main.rc`
- Recent relevant history:
  - `1ef0be1` `Merge boost v2`
  - `795c931` `Add boost A/B model selection`
  - `084f695` `Add boost drive control`
  - `13b3063` `Merge compressor stomp pedal`
  - `302ac86` `Add built-in compressor algorithm`
  - `bccee0b` `Enable compressor stomp controls`
  - `7f779f0` `Updated graphics for Stomp section`
  - `b08b093` `Update handover`
  - `869fa37` `Merge feature/interactive-cab-v1`

## What is completed now

### 1) Metering overhaul is landed and stable
- Input/output metering is mono/stereo-aware
- Meter UI is now vector-based and cleaner than the old striped/bitmap look
- Clip warning latch exists and clears by clicking either meter
- Meter decay/peak timing was tightened and accepted

### 2) Gate/stomp coupling bug is fixed
- Gate is decoupled from stomp bypass
- Right/Ctrl-click bypass on the Stomp section no longer disables the current gate path

### 3) Preset/session context restore is fixed
- Standalone restart restores preset context, name, and dirty `*` state
- Plugin/session restore also restores preset context instead of always showing `Unsaved`
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
- Cab UI was significantly redesigned
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
- Stomp section graphics and control layout were updated to expose the compressor

### 8) Boost v2 is landed
Outcome:
- The boost pedal now supports two switchable model variants while sharing one control surface

Landed work:
- Separate boost model paths for `A` and `B`
- `A/B` switch in the stomp UI
- Boost drive control added
- Capability/latency/state handling follows the selected boost variant
- Preset/session restore includes both boost model paths

### 9) Transpose is still hidden and undecided
- Hidden transpose feature still exists
- A small RT-safe idle-path fix already landed
- Most recent investigation suggested apparent "transpose on" behavior was restored saved state, not a changed default
- Current transpose default is still `0`

## Current in-progress worktree state

### Amp model variants v1 is partially implemented but not committed
Intent:
- Each amp slot should own multiple model variants, starting with `A` and `B`
- Only one variant is active at a time per amp slot
- Other controls for that amp slot remain shared across variants
- The storage/load/swap architecture should be future-proof for more than two variants later

Current dirty-tree scope:
- Backend scaffolding changed from `slot` to `slot + variant` for amp model storage, loading, pending swaps, and UI event state
- State serialization now stores per-variant amp paths plus the selected variant per slot
- Settings UI now exposes amp model browser rows for `1B`, `2B`, and `3B`
- Amp 2 has a first working on-face variant switch with vertical switch art and `LEAD` / `CRUNCH` labels
- Amp 1 and Amp 3 are scaffolded for future UI switches but intentionally do not expose them yet

Current behavioral status:
- User-reported status is that Amp 2 variant switching is working as intended in both UI and DSP
- Staged image additions belong to this feature
- No fresh build/test run was performed in this recovery session

## Important tweak points and files

### Amp variant system
- Main amp DSP/state/UI path:
  - `NeuralAmpModeler/NeuralAmpModeler.cpp`
  - `NeuralAmpModeler/NeuralAmpModeler.h`
- Amp-specific controls:
  - `NeuralAmpModeler/NeuralAmpModelerControls.h`
- Legacy/JSON unserialization path:
  - `NeuralAmpModeler/Unserialization.cpp`
- Resource registration:
  - `NeuralAmpModeler/config.h`
  - `NeuralAmpModeler/resources/main.rc`
- Switch art:
  - `NeuralAmpModeler/resources/img/Hardware/HorizonalSwitch_L.png`
  - `NeuralAmpModeler/resources/img/Hardware/HorizonalSwitch_R.png`
  - `NeuralAmpModeler/resources/img/Hardware/VerticalSwitch_UP.png`
  - `NeuralAmpModeler/resources/img/Hardware/VerticalSwitch_DOWN.png`

### Stomp compressor / boost v2
- Main stomp DSP/state/UI path:
  - `NeuralAmpModeler/NeuralAmpModeler.cpp`
  - `NeuralAmpModeler/NeuralAmpModeler.h`

## Important behavioral conclusions
- Interactive Cab V1 is a good baseline and should not be reworked casually
- Keep the dual-slot cab architecture
- `Custom IR` support in release mode is intentional and should remain
- `Input Stereo` should remain session/setup state, not preset-owned state
- Boost `A/B` established the pattern of "multiple model variants, one shared control surface"
- Amp variants should follow that same product direction
- Amp 2 variant UI is the current first slice; Amp 1 and Amp 3 UI can wait
- Transpose is still not a settled product feature; avoid expanding it unless the user explicitly wants that

## Current plugin/app state
- `main` already includes:
  - Interactive Cab V1
  - embedded curated cab IRs
  - built-in compressor stomp v1
  - boost v2 with `A/B` model selection
- The current worktree is not clean because amp model variants v1 is mid-implementation
- Release mode currently supports:
  - embedded curated cab IRs
  - user-loaded custom IRs
- No new build/test results were produced during this handover recovery pass

## Build/config baseline to preserve
- Audio/performance validation: `Release | x64` only
- Standalone audio test: run without debugger (`Ctrl+F5`)
- Do not evaluate DSP performance in Debug
- Do not run builds/tests unless the user explicitly asks
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
1. Review and commit amp model variants v1 as a small, coherent slice
2. Keep scope to the current intended feature:
  - per-slot `A/B` model storage/loading
  - state restore of selected variant
  - Amp 2 UI switch only
3. Do not expand Amp 1 / Amp 3 front-panel variant UI unless the user explicitly wants that next
4. After the amp variant slice is stable, return to lower-priority roadmap items such as transpose or release packaging

## Starter prompt for the next agent
You are continuing work in `D:\Dev\NAMPlugin` on branch `main`.

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
6. Release mode embeds curated cab IRs but still allows custom IR loading
7. Compressor stomp pedal v1 is merged to `main`
8. Boost v2 with `A/B` model selection is merged to `main`

Current working tree status:
1. The tree is dirty with in-progress amp model variants v1 work
2. Amp 2 variant switching is reported working in both UI and DSP
3. Amp 1 and Amp 3 are backend-ready for variants but intentionally do not expose front-panel switches yet

Suggested next task unless the user redirects:
1. Inspect the current amp model variants diff and preserve the intended architecture
2. Keep the current first slice minimal:
  - per-slot `A/B` model loading/storage/persistence
  - Amp 2 front-panel variant switch
3. If making code changes, stay RT-safe and keep the patch reviewable
