Last updated: 2026-03-19

Purpose: concise handoff so a new agent can continue without replaying the full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md`
4. `FUTURE_PLAN.md`

## Current repository state
- Active branch: `main`
- Branch HEAD: `869fa37`
- Working tree at handoff: clean
- Recent relevant history:
  - `869fa37` `Merge feature/interactive-cab-v1`
  - `cb678d0` `Wire embedded curated cab IRs into release mode`
  - `59abf22` `Add embedded curated cab IR assets`
  - `5a1e4a2` `Implement interactive cab v1`
  - `3412e9a` `Add interactive cab graphics`
  - `a25d4e8` `Improved graphics for Amp2 and Cab`
  - `816fbc6` `Transpose should not affect CPU when disabled.`

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
- Cab UI was significantly redesigned:
  - per-slot header rows
  - `MIC` labeling
  - dedicated cab picker styling
  - mic bitmap slider handles
- Old single `Cab Blend` concept was removed

### 6) Curated cab assets are embedded for release mode
Outcome:
- Release mode no longer depends on runtime disk reads for the curated cab mic set

Landed work:
- Generated embedded curated IR asset files:
  - `NeuralAmpModeler/EmbeddedCabIRAssets.h`
  - `NeuralAmpModeler/EmbeddedCabIRAssets.cpp`
- Generator script:
  - `tools/generate_embedded_cab_ir_assets.py`
- Release-mode curated cab staging now loads from embedded PCM data instead of WAV files on disk
- Visual Studio project files were updated to compile the generated asset source

Important nuance:
- `Custom IR` loading is still allowed in release mode
- Only the curated mic set is embedded; user-loaded IRs still use the normal file path flow

### 7) Transpose is still hidden and undecided
- Hidden transpose feature still exists
- A small RT-safe idle-path fix already landed:
  - transpose should not keep doing steady-state off-path work when disabled
- Recent app-side “transpose seems on” behavior was inspected
- Most likely cause was standalone saved state restoring a nonzero transpose value, not a changed default
- Current transpose default is still `0`

## Important tweak points and files

### Cab system
- Main cab DSP/state/UI path:
  - `NeuralAmpModeler/NeuralAmpModeler.cpp`
  - `NeuralAmpModeler/NeuralAmpModeler.h`
- Cab-specific controls:
  - `NeuralAmpModeler/NeuralAmpModelerControls.h`
- Embedded curated cab assets:
  - `NeuralAmpModeler/EmbeddedCabIRAssets.h`
  - `NeuralAmpModeler/EmbeddedCabIRAssets.cpp`
- Embedded asset generator:
  - `tools/generate_embedded_cab_ir_assets.py`

### Cab UI assets
- New mic images:
  - `NeuralAmpModeler/resources/img/Mic/57.png`
  - `NeuralAmpModeler/resources/img/Mic/121.png`
- Resource registration:
  - `NeuralAmpModeler/config.h`
  - `NeuralAmpModeler/resources/main.rc`

## Important behavioral conclusions
- Interactive Cab V1 is considered a good baseline and should not be reworked casually
- Keep the dual-slot “mono source, then pan” architecture
- Curated mic alignment is expected to come from the IR assets themselves, not from app-side phase correction
- `Custom IR` support in release mode is intentional and should remain
- `Input Stereo` should remain session/setup state, not preset-owned state
- Plugin preset asset restore is sensitive; keep future changes there minimal and reviewable
- Transpose is still not a settled product feature; avoid expanding it unless the user explicitly wants that

## Current plugin/app state
- `main` contains Interactive Cab V1 and the embedded curated IR path
- Working tree is clean at handoff
- Release mode now supports:
  - embedded curated cab IRs
  - user-loaded custom IRs
- No new build/test results were produced during the final merge pass

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
1. Unless the user redirects, inspect a built-in compressor stomp pedal v1
2. Keep scope small:
  - amount
  - level / makeup gain
  - soft / hard switch
3. Prefer a simple built-in DSP compressor over a large stomp-architecture rewrite
4. Keep transpose as a lower-priority decision path unless the user explicitly returns to it

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

Suggested next task unless the user redirects:
1. Inspect whether a built-in compressor stomp pedal v1 can be added with minimal architecture churn
2. Propose the smallest RT-safe signal-chain insertion point and control set
3. If the user wants code, implement only the smallest reviewable first slice

Compressor idea currently under consideration:
- controls:
  - `Amount`
  - `Level`
  - `Soft/Hard` switch
- likely behavior:
  - built-in compressor DSP, not a NAM model
  - soft/hard maps to different compression character, likely ratio/knee and possibly timing presets

Constraints:
- Keep diffs minimal
- Keep audio-thread RT-safe
- Do not run builds/tests unless explicitly requested
- Dev mode: breaking changes are allowed when they improve architecture and iteration speed
