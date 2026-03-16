Last updated: 2026-03-16

Purpose: concise handoff so a new agent can continue without replaying the full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md`
4. `FUTURE_PLAN.md`

## Current repository state
- Active branch: `main`
- Branch HEAD: `10ce311`
- Working tree at handoff: clean
- Recent relevant history:
  - `10ce311` `Minor UI improvements`
  - `6f885eb` `Add visual on/off effect for amps`
  - `b206a4b` `Add graphics for on/off amp visuals`
  - `8f076e4` `Add Amp 1 switch and allow independent switch scale for amps.`
  - `35d9035` `Dim deactivated sections`
  - `ce91d9f` `fix(preset): restore plugin stomp and cab assets`
  - `f256cf7` `Minor fixes of doubler`
  - `34a4838` `tune(ui): refine tempo field hover states`
  - `7cec790` `fix(preset): restore preset context and ignore input mode`
  - `0b1a893` `Do not inactivate gate when inactivating stomp section.`
  - `5d4d8cb` `tune(meter): tighten decay timing`
  - `611f0f5` `feat(meter): latch and clear clip warnings`
  - `6caf212` `feat(meter): add stereo-aware metering and UI cleanup`

## What is completed now

### 1) Metering overhaul is landed
- Input/output metering is now mono/stereo-aware
- Meter UI was restyled away from the old bitmap/striped look
- Stereo meters render as two clean lanes with a small gap
- Clip warning latch exists and clears by clicking either meter
- Meter decay/peak timing was tightened to feel faster and more readable

### 2) Gate/stomp coupling bug is fixed
- Gate was moved to the top row earlier
- Remaining stomp-bypass coupling was removed
- Right/Ctrl-click bypass on the Stomp section no longer disables the current gate path

### 3) Preset/session context restore is much better
- Standalone restart now restores:
  - last preset name
  - dirty `*` state
  - fallback to `Unsaved` only if the preset file is missing
- Plugin/session restore now also restores preset context instead of always showing `Unsaved`
- `Input Stereo` is no longer treated like preset-owned state:
  - preset loads do not change it
  - changing it does not mark the preset dirty
- Session/last-state restore still keeps `Input Stereo`

### 4) Plugin preset asset restore is fixed
- Plugin preset/session reopen now restores:
  - stomp NAM / boost NAM
  - cab IR
- The final fix was removing the IR pre-clear during preset recall
- Relative preset-path handling was also tightened during this work

### 5) Doubler is stable enough for now
- Doubler bypass no longer jumps through a brief centered processed-left state when switched off
- Current doubler tuning was adjusted by the user and is accepted as the current baseline

### 6) Top-level and amp-face UI polish moved forward a lot
- Deactivated section dimming exists
- Tuner now renders above the dimmed section background
- Utility knobs got a slightly clearer hover highlight
- Mono/stereo switch now uses the same subtle hover-pop language as the other top-row SVG icons
- Tempo/BPM readout hover states were refined
- AMP slot 1 knob labels can use a separate label color path
- AMP slot 1 has its own switch bitmaps
- Amp model switch scale is independently adjustable per slot
- Amp backgrounds now have `_OFF` variants and the amp page background follows the same on/off state as the amp switch

## Important tweak points added recently
- AMP model switch per-slot scale:
  - `NeuralAmpModeler.cpp`
  - `kAmpModelSwitchScales`
- AMP on/off background swap:
  - `config.h` for `_OFF` asset defines
  - `resources/main.rc` for bitmap registration
  - `NeuralAmpModeler.cpp`
  - `GetAmpBackgroundResourceName(...)`
- AMP1-specific switch art:
  - `config.h`
  - `NeuralAmpModelerControls.h`
  - `NeuralAmpModeler.cpp`

## Important behavioral conclusions
- Do not reopen the old preset-transition optimization work unless there is a very clear reason
- Metering is in a good state now; do not churn it unless the user asks
- `Input Stereo` should remain session/setup state, not preset-owned state
- Plugin preset asset restore is sensitive; keep future changes there minimal and reviewable
- Doubler is good enough to keep stable unless the user explicitly wants more tuning

## Current plugin/app state
- Release-mode scaffold still exists from earlier work, but final embedded asset packaging is not done
- Metering, gate, doubler, preset-context restore, and plugin asset restore are all in a much safer place than before
- Recent work has shifted into small targeted UX/UI polish on top of that baseline

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
1. Unless the user redirects, move to the transpose decision path next
2. If the user stays in polish mode, prefer small UI-only fixes over deeper DSP changes
3. Keep Release asset packaging paused until the final model/variant set is clearer

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
1. Metering overhaul is landed and considered stable
2. Gate is decoupled from stomp bypass
3. Standalone/plugin preset context restore is fixed
4. Plugin preset stomp NAM and cab IR restore is fixed
5. Recent work is mostly small UI/UX polish on the amp section and top controls

Suggested next task unless the user redirects:
1. Inspect the transpose feature path and current tradeoffs
2. Propose whether it should stay, be simplified, or be removed
3. Implement only the smallest reviewable change if the user wants code

Constraints:
- Keep diffs minimal
- Keep audio-thread RT-safe
- Do not run builds/tests unless explicitly requested
- Dev mode: breaking changes are allowed when they improve architecture and iteration speed
