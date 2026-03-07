# AGENT_SESSION_SUMMARY.md

Last updated: 2026-03-06

Purpose: concise handoff so a new agent can continue without replaying full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md` (this file)
4. `FUTURE_PLAN.md`

## Current repository state
- Active branch: `amp-slot-architecture`
- Branch HEAD: `439d946`
- `main` HEAD (remote): `fa7a264`
- Submodule `iPlug2` pin:
  - `b54bf7af6b15f84b9bd7593e63bdf75e70d658db`
- `iPlug2` remote:
  - `origin` = `https://github.com/miandre/iPlug2.git`

## Working tree status at handover (important)
- There is active uncommitted Milestone-6 WIP in:
  - `NeuralAmpModeler/NeuralAmpModeler.cpp`
  - `NeuralAmpModeler/NeuralAmpModeler.h`
  - `NeuralAmpModeler/Unserialization.cpp`
  - `NeuralAmpModeler/config.h` (`NAM_RELEASE_MODE` currently set to `1`)
- This WIP is intentionally not committed yet and should be reviewed before merge.

## What was completed recently

### 1) EQ move + UI restructuring
- Dedicated EQ page added and connected in top nav.
- EQ/FX coupling bug fixed (independent section bypass/control behavior).
- EQ and FX layout/artwork updated; obsolete overlay assets removed.

### 2) Delay feature completion
- Host/manual tempo foundation added.
- Delay sync mode implemented (note division vs ms behavior).
- Ping-pong mode implemented and corrected:
  - ping-pong off keeps stereo behavior,
  - ping-pong on does true cross feedback,
  - stereo input behavior adjusted so first repeat starts on opposite side.
- Delay ducker added with user knob and expanded range.
- Delay digital readout implemented and polished (updates in realtime; order/mode UX fixes).

### 3) FX UI/preset behavior polish
- Delay/reverb UI refinements and control graphics updates.
- Delay/reverb off-state visual behavior improved.
- HP/LP defaults tuned (min/max).
- Preset restore issues for FX control positions were fixed.

### 4) Amp-slot architecture foundation (committed)
- Commit `439d946` added:
  - Rig/Release workflow mode scaffold,
  - slot edit lock policy,
  - user-initiated edit gating,
  - initial slot-path mutation helper centralization.

### 5) Amp-slot architecture follow-up (uncommitted WIP)
- Added fixed-slot path manifest scaffolding:
  - per-slot fixed path storage,
  - mode-aware effective-path resolver,
  - startup/default slot mapping hookup,
  - unserialization guards to avoid clearing locked release slots from empty preset paths.
- Needs final code review + build validation before commit.

## Build/config baseline to preserve
- Audio/performance validation: `Release | x64` only.
- Standalone audio test: run without debugger (`Ctrl+F5`).
- Do not evaluate DSP behavior/perf in Debug.
- Do not run builds/tests unless user explicitly asks.

## Current policy notes
- Non-negotiable RT rules still apply in audio thread:
  - no allocations, locks, waits, file/network/UI/logging, or exceptions across callback.
- Keep diffs small and reviewable.
- Dev-mode policy is active:
  - breaking changes are allowed when they improve architecture/iteration speed.

## Suggested next coding step
1. Finalize and commit current uncommitted Milestone-6 WIP on `amp-slot-architecture`.
2. Validate Rig mode (`NAM_RELEASE_MODE=0`) remains unchanged.
3. Validate Release mode (`NAM_RELEASE_MODE=1`) fixed-slot behavior on preset restore.
4. Then continue with model bundling/packaging milestone described in `FUTURE_PLAN.md`.

## Starter prompt for the next agent (copy/paste)
You are continuing work in `D:\Dev\NAMPlugin` on branch `amp-slot-architecture`.

Read first, in this exact order:
1) `AGENTS.md`
2) `SKILLS.md`
3) `AGENT_SESSION_SUMMARY.md`
4) `FUTURE_PLAN.md`

Then confirm current git/submodule state before editing:
- `git status --short`
- `git branch --show-current`
- `git rev-parse --short HEAD`
- `git submodule status iPlug2`
- `git -C iPlug2 remote -v`

After reading instructions, summarize the guardrails you will follow:
- RT safety in audio thread
- minimal-diff policy
- build/test policy (no builds unless explicitly requested)
- dev-mode policy: breaking changes are allowed when they improve architecture/iteration speed

Task request:
1) Review the uncommitted amp-slot architecture WIP and convert it into one minimal, safe commit.
2) Verify Release-mode slot-lock behavior against preset restore edge cases.
3) Propose and start the first minimal patch from the model-bundling strategy in `FUTURE_PLAN.md`.
