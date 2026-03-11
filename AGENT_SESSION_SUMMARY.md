Last updated: 2026-03-11

Purpose: concise handoff so a new agent can continue without replaying the full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md`
4. `FUTURE_PLAN.md`

## Current repository state
- Active branch: `main`
- Branch HEAD: `737df19`
- Recent relevant history:
  - `737df19` `Improve top level knobs`
  - `b4c718e` `feat(doubler): finalize double-track control and tuning`
  - `7d46a0f` `feat(doubler): add switch and tighten usable range`
  - `3215a87` `tune(doubler): refine other-player balance`
  - `c262aaa` `tune(doubler): increase take separation slightly`
  - `abcf1fe` `fix(doubler): tame output jump across amount range`
  - `586fdd6` `feat(ui): refine amp-face layout and artwork`
  - `2277754` `feat(release-mode): scaffold fixed assets and safe preset recall`

## Working tree status at handover
- Expected state at handoff: clean worktree on `main`
- If local testing changes exist, treat them as user-owned unless explicitly told otherwise

## What is completed now

### 1) Amp-slot architecture + safer preset behavior are landed
- Rig/Release workflow split exists
- Per-slot edit gating and fixed-slot path handling exist
- Release-mode preset restore no longer clears locked slots from stale preset paths
- Slot model source abstraction exists:
  - `ExternalPath`
  - `EmbeddedModelId`
- Slot switching defers swap until target model is ready
- Preset recall prefers deterministic mute over old-model/new-parameter hybrid audio

### 2) Release-mode fixed-asset scaffold v1 is landed
- `NAM_RELEASE_MODE=0`:
  - manual dev workflow
  - manual model/IR loading remains available
- `NAM_RELEASE_MODE=1`:
  - fixed amp/stomp/cab asset behavior from controlled `tmpLoad` asset files
  - manual browsing of those fixed assets is disabled or ignored
- This is still controlled-path loading, not final embedded packaging

### 3) Gate remake is landed
- Gate is now a one-knob macro control
- Threshold/timing behavior is remapped internally for a more musical feel
- Gate moved to the top control row
- Gate now uses:
  - regular knob
  - separate mini slider on/off control
  - separate attenuation indicator circle
- Old stomp-page gate controls were removed

### 4) Doubler v1 is landed
- Doubler is now a take-based double-track preview, not a center-dry widener
- Signal model is closer to:
  - `Take A -> left`
  - `Take B -> right`
- Doubler is:
  - post-cab
  - pre-delay/reverb
  - intended for the normal mono-guitar path
  - disabled when the true stereo-core path is active
- Current UX:
  - dedicated top-row `DOUBLE` knob
  - dedicated mini slider on/off control
  - default state is `OFF`
  - knob range is remapped into the currently useful internal span
- Loudness jump from mono -> heavy doubler is compensated somewhat

### 5) Top-level control/UI polish is in a much better state
- Top row currently centers around:
  - `INPUT`
  - `GATE`
  - `DOUBLE`
  - `OUTPUT`
- `GATE` and `DOUBLE` use matching mini-slider + knob patterns
- Top-row labels/offsets have been refactored enough that future tuning is easier
- Amp-face knobs now use rotating bitmap art and per-amp layout data

## Important behavioral conclusions
- Do not optimize further for seamless preset recall in the current external-loader path unless there is a very clear payoff
- Long preset mute is currently accepted as the safer temporary behavior
- Release mode is scaffolded enough for now; do not keep investing in asset packaging until the final model/variant set is clearer
- The doubler should be treated as a stereo preview / double-track audition tool, not as a mono-safe widener

## Current plugin/app state
- Release-mode scaffold works, but final bundled asset packaging is not done
- Gate and doubler are both feature-complete enough for normal use, though future tuning is still possible
- Doubler currently sounds good enough to keep; it is not “final-final” but it is a valid baseline
- Top-row control language is now more cohesive than before

## Build/config baseline to preserve
- Audio/performance validation: `Release | x64` only
- Standalone audio test: run without debugger (`Ctrl+F5`)
- Do not evaluate DSP performance in Debug
- Do not run builds/tests unless the user explicitly asks
- Do not assume local `config.h` matches the committed default; the user may toggle mode locally while testing

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
1. Keep the current gate and doubler state stable unless the user explicitly wants more tuning
2. Consider a small metering pass next:
   - proper mono/stereo-aware input metering
   - proper mono/stereo-aware output metering
3. After that, revisit the transpose decision path
4. Resume Release asset packaging only when the final model list / hardware-switch mapping is clearer

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

Current direction:
1. Amp-slot architecture, Release-mode scaffold, gate remake, and doubler v1 are already landed
2. Do not reopen old preset-transition optimization work unless explicitly asked
3. Prefer small, reviewable follow-ups from the current stable baseline

Suggested next task unless the user redirects:
1. Inspect current input/output metering behavior
2. Propose a minimal mono/stereo-aware metering improvement
3. Implement the smallest RT-safe patch

Constraints:
- Keep diffs minimal
- Keep audio-thread RT-safe
- Do not run builds/tests unless explicitly requested
- Dev mode: breaking changes are allowed when they improve architecture and iteration speed
