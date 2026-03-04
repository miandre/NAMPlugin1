# AGENT_SESSION_SUMMARY.md

Last updated: 2026-03-04

Purpose: concise handoff so a new agent can continue without replaying full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md` (this file)
4. `FUTURE_PLAN.md`

## Current repository state
- Branch: `main`
- HEAD on `main`: `c2baa44`
- Working tree baseline: clean at `c2baa44`
- Remote:
  - `origin` = `https://github.com/miandre/NAMPlugin1.git`
  - `upstream` = `https://github.com/sdatkinson/NeuralAmpModelerPlugin.git` (push disabled)
- Recent commits on `main`:
  - `c2baa44` merge: fx refactor split processing units
  - `f87da57` chore(project): add split FX sources to legacy project targets
  - `50fd25d` refactor(fx): extract post-EQ and delay/reverb processing units
  - `3f769cc` dsp: fix input metering and avoid one-sided stereo over-processing
  - `beb92af` submodule: bump AudioDSPTools for noise gate RT-safe fallback
  - `9c4528e` rt: forbid callback-time buffer growth in ProcessBlock
  - `47f74a7` Merge branch 'feature/ir-staging-path-atomic-declick'
  - `768de79` ir: stage paths atomically and de-click IR swaps
  - `e0374ab` Merge branch 'feature/model-metadata-indicators'
  - `ee6e6ed` ui: add per-model metadata capability indicators

## Submodule state (important)
- `iPlug2` remote:
  - `origin` = `https://github.com/miandre/iPlug2.git`
- Superproject pin:
  - `b54bf7af6b15f84b9bd7593e63bdf75e70d658db`

## Build/config baseline to preserve
- Audio/performance validation: `Release | x64` only.
- Standalone audio test: run without debugger (`Ctrl+F5` in VS).
- Do not evaluate DSP performance in Debug.
- Do not run builds/tests unless user explicitly asks.

## What was completed in this cycle

### 1) Per-slot model metadata indicators (UI clarity)
- Added non-editable capability indicators under each model picker so metadata display is no longer ambiguous when multiple models are loaded.
- Indicators reflect metadata capabilities like loudness/calibration per slot.

### 2) IR swap de-click and safer staging
- IR change path now stages data atomically and applies de-click handling.
- Reduced audible click risk when switching IRs.

### 3) RT stability hardening and crash mitigation
- Prevented callback-time buffer growth in `ProcessBlock` paths.
- Bumped `AudioDSPTools` for a noise gate fallback that avoids crashing behavior when gate processing hits exceptional states.
- LUNA crash investigation captured host-side evidence:
  - one class of crash in `nvwgf2umx.dll` with stack overflow (GPU driver/user-space stack)
  - one class showing unhandled `std::runtime_error` entering noise gate processing path.
- Reaper remained stable during those investigations.

### 4) DSP behavior fixes
- Input metering corrected to reflect real input path expectation (post input gain, before downstream amp/gate coloration).
- One-sided stereo feed no longer forces unnecessary dual-channel heavy processing.
- Effective mono optimization now handles one-sided stereo source cases better and reduces crackle risk under heavy models.

### 5) FX code refactor (structure only, no intentional sound change)
- Split large `ProcessBlock` FX sections into dedicated files:
  - `NeuralAmpModelerPostEQ.cpp/.h`
  - `NeuralAmpModelerFX.cpp/.h`
- `NeuralAmpModeler.cpp` now dispatches to focused helper methods for post-cab EQ, delay, and reverb processing.
- Build/link safety fix: `GetNAMSampleRate(...)` in header was made `inline` to avoid multi-definition after multi-TU split.
- Added new source files to relevant project files (app/vst3 and legacy targets where touched).

## Current policy notes
- Non-negotiable RT rules still apply in audio thread:
  - no allocations, locks, waits, file/network/UI/logging, or thrown exceptions across callback.
- Keep diffs minimal and reviewable.
- Keep style consistent with surrounding code.
- Current product stage is dev-only single-user testing.
  - Breaking changes are acceptable if they materially improve architecture/speed of iteration.
  - Model picker and external IR picker are considered development-rig capabilities.

## Known practical status
- Mainline includes:
  - preset workflow improvements,
  - robust effective-mono optimization,
  - metadata slot indicators,
  - IR de-click staging,
  - RT hardening and noise-gate fallback,
  - FX code split.
- LUNA host instability is not fully closed as a root-cause story; plugin-side crash vectors were reduced, but external host/driver instability evidence exists.

## Next planning source
- Use `FUTURE_PLAN.md` as the canonical roadmap draft for upcoming feature work and sequencing.

## Starter prompt for the next agent (copy/paste)
You are continuing work in `D:\Dev\NAMPlugin` on branch `main`.

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
1) Propose milestone-sized implementation plan based on `FUTURE_PLAN.md`.
2) Keep scope small per patch and prioritize lowest-risk/highest-value items first.
3) For each proposed milestone, list user impact, technical risk, likely files/symbols, and RT-safety watch-outs.
