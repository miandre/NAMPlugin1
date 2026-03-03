# AGENT_SESSION_SUMMARY.md

Last updated: 2026-03-03

Purpose: concise handoff so a new agent can continue work without replaying full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md` (this file)

## Current repository state
- Branch: `main`
- Remote:
  - `origin` = `https://github.com/miandre/NAMPlugin1.git`
  - `upstream` = `https://github.com/sdatkinson/NeuralAmpModelerPlugin.git` (push disabled)
- Current HEAD on `main`: `25a9c25`
- Working tree baseline: clean on `main` at `25a9c25` before editing this handover file
- Recent commits on `main`:
  - `25a9c25` Optimize stereo core with robust effective-mono collapse
  - `3a6e643` ui(presets): polish strip layout and menu/text-entry consistency
  - `b2bb902` presets: add inline rename flow for user presets
  - `c0d20ff` presets: keep save-as name entry active after overwrite cancel
  - `1dc0030` presets: add default preset and inline save-as/delete flow
  - `e928f0a` ui(file-browser): handle explicit clear messages
  - `ac9ca1e` Update handover
  - `a375ec7` FX reverb: remove mode toggle and finalize room-only algorithm

## Submodule state (important)
- `iPlug2` remote:
  - `origin` = `https://github.com/miandre/iPlug2.git`
- Superproject currently pins `iPlug2` to:
  - `b54bf7af6b15f84b9bd7593e63bdf75e70d658db`

## Build/config baseline to preserve
- `Release | x64` is the only valid mode for DSP/perf judgment.
- Standalone should be tested via `Ctrl+F5` (no debugger for DSP judgments).
- Current `config.h` highlights:
  - `PLUG_NAME "MND-AMP"` (temporary naming in progress)
  - `BUNDLE_NAME "MND-AMPS"`
  - Plugin channel layouts (`APP_API` off): `PLUG_CHANNEL_IO "2-2 1-2 1-1"`
  - Standalone channel layouts (`APP_API` on): `PLUG_CHANNEL_IO "1-2 2-2"`
  - `NAM_APP_STEREO_CORE_TEST 1`

## What was completed in this cycle

### 1) Preset workflow UX and authoring flow
- Built-in standalone preset authoring flow is now in place (menu-driven, no Windows file picker for normal preset ops).
- Added inline Save As text-entry popup (same UI style as menu).
- Added preset delete flow from preset menu.
- Added preset rename flow from preset menu.
- Save As collision behavior changed:
  - prompt asks to overwrite existing preset name instead of auto suffixing.
  - if overwrite is declined, text entry reopens with focus retained for quick rename.
- Added and integrated `Default` preset:
  - shown at top of preset menu.
  - selected by default on first startup/new track if no restored state.
- Preset strip/menu polish:
  - larger/more readable typography (font 18 where applicable).
  - arrow alignment fixes.
  - removed numeric prefix in displayed preset label.
  - submenu order adjusted: Factory first, User below.

### 2) Default preset/state behavior fixes
- Startup tuner default corrected to inactive.
- Default preset load path fixed so selected startup assets are actually active, not only visually selected.
- Fixed staged/default recall behavior so amp models are correctly re-armed/toggled during default recall.
- Fixed missing Boost/Cab default load behavior in default preset flow.
- Cab default load is now confirmed working for `Default` preset.

### 3) Stereo CPU optimization hardening
- Effective-mono optimization was strengthened in `ProcessBlock`:
  - strict near-identical stereo detection.
  - one-sided input detection (L silent/R active or R silent/L active) treated as effective mono.
  - hysteresis windows to avoid rapid mono/stereo flapping.
- Collapsed-mono path still re-expands before IR stage, so stereo IR behavior remains intact.
- Committed and merged:
  - `25a9c25` Optimize stereo core with robust effective-mono collapse
  - branch used: `feature/effective-mono-collapse-hardening` (already merged to `main`)

### 4) What was tried but intentionally not kept
- A prototype smoothing pass for mono/stereo FX transition behavior (delay/reverb strategy interpolation) was implemented and auditioned.
- User judged it unnecessary for real-world usage and asked to remove it.
- That patch was reverted before commit; `main` does not include it.

## Non-negotiable guardrails (must follow)
- Audio-thread (`ProcessBlock` and callees) rules:
  - no heap allocation
  - no locks/waits/sleeps
  - no file/network/UI/logging
  - no throwing exceptions across callback
- Keep diffs minimal and reviewable.
- Keep style consistent; avoid unrelated refactors/reformatting.
- Prefer append-only param enum changes unless explicitly approved otherwise.
- Do not run builds/tests unless user explicitly requests it.
- If uncertain whether code is on audio thread, assume it is and keep it RT-safe.

## Known practical status
- Stereo path and dual-mono core behavior are in place and working.
- Effective-mono CPU optimization is robust for:
  - dual-identical mono on stereo input
  - one-sided mono on stereo input
  - while avoiding false mono on tightly matched but real stereo content.
- Preset menu/workflow is significantly improved (save-as inline, rename, delete, default preset, cleaner strip UX).
- Reverb is still on room-only finalized baseline from `a375ec7`.
- Output mode preset linkage was discussed as low-priority and left unchanged for now.

## Potential next work areas (not committed roadmap)
1. Reverb code cleanup:
   - factor/organize long `ProcessBlock` reverb section without changing sound.
2. Optional FX features:
   - ping-pong delay mode switch.
3. Productization polish:
   - naming/branding cleanup (plugin/manufacturer strings/assets).
4. Cross-host UX validation:
   - verify defaults/listing behavior in LUNA/Reaper/other hosts.

## Starter prompt for the next agent (copy/paste)
You are continuing work in `D:\\Dev\\NAMPlugin` on branch `main`.

Read first, in this exact order:
1) `AGENTS.md`
2) `SKILLS.md`
3) `AGENT_SESSION_SUMMARY.md`

Then confirm current git/submodule state before editing:
- `git status --short`
- `git branch --show-current`
- `git submodule status iPlug2`
- `git -C iPlug2 remote -v`

After reading instructions, briefly summarize the most important guardrails you will follow:
- RT safety in audio thread
- minimal-diff policy
- parameter/index compatibility policy
- build/test policy (no builds unless explicitly requested)

Current baseline to preserve:
- `main` includes full stereo routing/core work, preset management improvements (inline save-as/rename/delete/default preset), and room-only reverb finalization.
- Latest head: `25a9c25`.
- `iPlug2` is pinned to `b54bf7af6` from `https://github.com/miandre/iPlug2.git`.
- Robust effective-mono CPU optimization is merged and should not regress.
- FX mono/stereo transition smoothing prototype is intentionally not on `main`.

Task request:
1) Inspect current code state and identify the top 3-5 highest-value next steps.
2) For each step, provide:
   - expected user impact
   - technical risk
   - files/symbols likely touched
   - RT-safety concerns to watch.
3) Recommend an execution order (smallest/lowest-risk first).

Style requirements:
- Propose minimal diffs first.
- Keep changes RT-safe.
- Keep enum/serialization stability in mind.
- Do not run builds unless explicitly asked by user.
