Last updated: 2026-03-22

Purpose: concise handoff so a new agent can continue without replaying the full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md`
4. `FUTURE_PLAN.md`

## Current repository state
- Active branch: `main`
- Branch HEAD: `328c1ba`
- Working tree at handoff: clean
- Recent relevant history:
  - `328c1ba` `Polish tuner note display and rollover`
  - `a8b8ffe` `Improve tuner tracking and UI`
  - `6e9cbd6` `Make dev mode default`
  - `1edb497` `Update handover for amp variants baseline`
  - `6fda324` `Add amp model variants v1`
  - `1ef0be1` `Merge boost v2`
  - `795c931` `Add boost A/B model selection`
  - `084f695` `Add boost drive control`
  - `13b3063` `Merge compressor stomp pedal`

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
- Each amp slot now supports multiple model variants, starting with `A` and `B`

Landed work:
- Per-slot `A/B` model storage/loading/persistence
- Selected variant is stored and restored per amp slot
- Shared amp controls remain common to the slot, not per variant
- Amp 2 has the first front-panel variant switch
- Amp 1 and Amp 3 are backend-ready for variants but intentionally do not expose front-panel switches yet
- Release mode preload now expects:
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
- The core bug was that note changes reset cents to `0`; removing that fixed the worst UX issue
- Tuner now shows numeric signed cents offset
- Sharp notes now render with dual names:
  - `C#/Db`
  - `D#/Eb`
  - `F#/Gb`
  - `G#/Ab`
  - `A#/Bb`
- Tuner UI was narrowed slightly and smoothed for a steadier feel
- Tuner monitor default is now `MUTE`

### 11) Transpose is still hidden and undecided
- Hidden transpose feature still exists
- Current transpose default is still `0`
- Do not expand it unless the user explicitly wants that

## Important tuner conclusions
- The main tuner bug was not the display layer by itself; it was the analyzer resetting cents to center on note change
- Several attempted simplifications were tested and rejected because they reintroduced bad behavior
- Keep the current tuner behavior as-is unless the user explicitly wants further tuner work
- If cleanup is desired later, prefer non-behavioral cleanup only:
  - extract constants
  - improve comments
  - group related state

## Important doubler findings
- The virtual doubler path is more complex and more subjective than the tuner
- Main insertion is post-cab and pre-delay/reverb in:
  - `NeuralAmpModeler/NeuralAmpModeler.cpp`
- Main DSP implementation is in:
  - `NeuralAmpModeler/NeuralAmpModelerFX.cpp`
- Supporting state is in:
  - `NeuralAmpModeler/NeuralAmpModeler.h`

Current doubler behavior:
- Mono-source-only availability
- Short-delay-based "other take" synthesis
- Onset-driven retargeting/jitter
- Asymmetric left/right voicing
- Tone shaping, width processing, and output compensation
- Only one exposed control: amount

Current concerns:
- A lot of hidden behavior is packed behind one control
- It is difficult to form a simple user mental model for what the doubler is doing
- It may sound good in one case and odd or phasey in another without an obvious control to fix it
- Any changes here are likely to be more subjective and iterative than tuner work

## Important behavioral conclusions
- Interactive Cab V1 is a good baseline and should not be reworked casually
- Keep the dual-slot cab architecture
- `Custom IR` support in release mode is intentional and should remain
- `Input Stereo` should remain session/setup state, not preset-owned state
- Boost `A/B` established the pattern of "multiple model variants, one shared control surface"
- Amp variants now follow that same product direction
- Amp 2 variant UI is the current first slice; Amp 1 and Amp 3 UI can wait
- The tuner is now in a good usable state; avoid reopening it casually

## Current plugin/app state
- `main` already includes:
  - Interactive Cab V1
  - embedded curated cab IRs
  - built-in compressor stomp v1
  - boost v2 with `A/B` model selection
  - amp model variants v1
  - tuner improvements and tuner UI polish
- The current worktree is clean
- Release mode currently supports:
  - embedded curated cab IRs
  - preloaded amp variant assets `Amp1A/B`, `Amp2A/B`, `Amp3A/B`
  - user-loaded custom IRs

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
1. Inspect the virtual doubler for product-level improvement opportunities
2. Start with a code-reading/review pass, not immediate DSP changes
3. Preserve RT safety and keep the first doubler patch minimal and reviewable
4. Prefer improving predictability and user mental model over adding many new controls immediately

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
6. Release mode embeds curated cab IRs and amp variant assets while still allowing custom IR loading
7. Compressor stomp pedal v1 is merged to `main`
8. Boost v2 with `A/B` model selection is merged to `main`
9. Amp model variants v1 is merged to `main`
10. Tuner improvements and tuner UI polish are merged to `main`

Current working tree status:
1. The tree should be clean
2. Do not reopen tuner work unless the user asks

Suggested next task unless the user redirects:
1. Inspect the virtual doubler implementation and summarize its current behavior, risks, and likely improvement points
2. Main files:
  - `NeuralAmpModeler/NeuralAmpModeler.cpp`
  - `NeuralAmpModeler/NeuralAmpModelerFX.cpp`
  - `NeuralAmpModeler/NeuralAmpModeler.h`
3. Keep the first doubler improvement slice minimal, RT-safe, and reviewable
