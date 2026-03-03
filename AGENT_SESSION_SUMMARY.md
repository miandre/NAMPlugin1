# AGENT_SESSION_SUMMARY.md

Last updated: 2026-03-02

Purpose: concise handoff for new agents so work can continue without replaying full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md` (this file)

## Current repository state
- Branch: `main`
- Remote:
  - `origin` = `https://github.com/miandre/NAMPlugin1.git`
  - `upstream` = `https://github.com/sdatkinson/NeuralAmpModelerPlugin.git` (push disabled)
- Current HEAD on `main`: `a375ec7`
- Recent commits on `main`:
  - `a375ec7` FX reverb: remove mode toggle and finalize room-only algorithm
  - `2e54238` FX reverb: raise wet level and tighten decay-size response
  - `ae84e79` FX reverb: lock single-room voicing baseline and width/diffusion polish
  - `5634f2c` FX reverb: add adaptive stereo decorrelator with stronger mix
  - `410ef64` FX reverb checkpoint: stereo width shaping and damping/diffusion tuning
  - `bc54c55` FX reverb baseline: room-size macro, decay/predelay/mix tuning
  - `11dbbc1` FX reverb tuning checkpoint: stable tail, stereo image, decay mapping
  - `aa97503` FX: mono-source stereo imaging for delay/reverb bus
  - `a449619` FX: add minimal stereo delay/reverb coupling pass
  - `0409fcc` Minor bug fixes (channel and preset issues)
  - `b9b87cc` Merge feature/standalone-presets-workflow: preset management 1.0
  - `97bd4b1` Preset menu 1.0: unified dropdown workflow and dirty tracking
  - `4dc756a` Optimize stereo mode for effective mono input

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

### 1) Stereo and host-compat behavior
- Effective-mono optimization in stereo input mode was added (`4dc756a`) to avoid redundant heavy dual-path processing when input is effectively mono.
- Channel layout ordering and host behavior were tuned so VST3 appears/loads correctly on stereo tracks across hosts.
- Auto input-mode defaulting was adjusted for host differences (LUNA/Reaper behavior), then stabilized.

### 2) Preset management 1.0 (standalone and plugin integration)
- Unified dropdown-style preset workflow implemented with structured menus and user/factory organization.
- Dirty-state indicator (`*`) implemented for changed presets.
- Preset label/menu styling and geometry improved.
- Plugin path was wired to the same intended preset behavior (not just generic host dropdown).
- Prompting bugs fixed:
  - loading presets no longer incorrectly opens `.nam` file dialogs (prompt is now UI-action gated).
- Active UI section is no longer serialized as preset state (loading presets does not force section switches).
- Dirty tracking now also reacts to top-nav bypass/section related changes that affect sound state.

### 3) FX (delay/reverb) major iteration
- Delay path reached good stereo behavior; ping-pong was deferred as a possible future switch/feature.
- Reverb went through many tuning passes:
  - early/late balance reshaping
  - damping/diffusion tuning
  - high-decay stabilization
  - stereo decorrelation and width shaping
  - wet loudness compensation and decay-span tuning
- Final direction:
  - single room-based reverb mode (room at low decay, hall-like at higher decay)
  - removed visible Room/Hall toggle from UI
  - removed hall-branch DSP logic and related smoothing state for cleaner room-only processing

### 4) Branch and merge flow completed
- FX work was finalized on `feature/stereo-fx-strategy`, pushed, then merged fast-forward into `main`.
- `origin/main` currently includes full preset 1.0 + stereo/host fixes + latest room-only reverb work.

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
- Preset workflow is much more complete and modern than the old load/save-file-only flow.
- Reverb currently sounds good per user feedback and is in a solid room-only baseline.

## Potential next work areas (not yet committed as a roadmap)
1. Reverb cleanup pass:
   - further factorization of long `ProcessBlock` reverb section for readability/maintenance without sound change.
2. Optional FX features:
   - ping-pong delay mode switch
   - additional stereo width/decorrelation control (if desired by user).
3. Productization:
   - naming/branding cleanup (plugin/manufacturer strings, assets) and consistency.
4. Cross-host UX polish:
   - verify behavior in LUNA/Reaper/other DAWs around defaults and plugin listing labels.

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
- `main` includes full stereo routing/core work, preset management 1.0, and room-only reverb finalization.
- Latest head: `a375ec7`.
- `iPlug2` is pinned to `b54bf7af6` from `https://github.com/miandre/iPlug2.git`.

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
