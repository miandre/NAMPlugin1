Last updated: 2026-03-07

Purpose: concise handoff so a new agent can continue without replaying the full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md`
4. `FUTURE_PLAN.md`

## Current repository state
- Active branch: `amp-slot-architecture`
- Branch HEAD: `e84f5c3`
- Recent relevant history:
  - `e84f5c3` `feat(release-mode): scaffold fixed assets and safe preset recall`
  - `d8ae1df` `fix(preset-load): avoid clearing active slot before replacement model`
  - `75de4bf` `fix(amp-slot): defer switch until model ready to avoid clicks and dropouts`
  - `71a8b8d` `feat(amp-slot): scaffold slot model source abstraction`
  - `7d64372` `feat(amp-slot): preserve fixed release slot model paths on restore`
  - `8c7a03b` `refactor(amp-slots): add rig/release slot edit policy foundation`
- `main` changes are rebased in through `fa7a264`

## Working tree status at handover
- Current non-code local changes are untracked image assets:
  - `NeuralAmpModeler/resources/img/Amp3Knob.png`
  - `NeuralAmpModeler/resources/img/Amp3KnobBackground.png`
- Treat those as user-owned unless explicitly told otherwise.

## What is completed now

### 1) Delay and FX work from `main`
- Delay sync/manual tempo foundation is in.
- Ping-pong, ducker, and FX polish are in.
- EQ/FX page split and related UI cleanup are in.

### 2) Amp-slot architecture milestone is functionally landed
- Rig/Release workflow split exists.
- Per-slot edit gating and fixed-slot path handling exist.
- Release-mode preset restore no longer clears locked slots from stale preset paths.
- Slot model source abstraction exists:
  - `ExternalPath` for Rig mode
  - `EmbeddedModelId` for Release mode scaffold

### 3) Safer slot/preset transitions are in
- Slot switching defers audio-thread swap until the target model is ready.
- Preset recall prefers deterministic mute over old-model/new-parameter hybrid audio.
- The active scene is not allowed to keep sounding under mismatched preset state.

### 4) First Release-mode "release-feel" scaffold is in
- `NAM_RELEASE_MODE=0`:
  - manual dev workflow
  - manual model/IR loading remains available
- `NAM_RELEASE_MODE=1`:
  - fixed amp/stomp/cab asset behavior from controlled `tmpLoad` asset files
  - amp slots resolve through embedded asset IDs internally
  - manual browsing/clearing of those fixed assets is disabled or ignored
- User manually confirmed in Release mode:
  - Amp 1-3 load
  - Boost loads
  - Cab loads
  - Those assets cannot currently be changed by the user

## Important behavioral conclusion
- Do not optimize further for seamless preset recall in the current external-loader path unless there is a very clear payoff.
- The product direction is fixed/preloaded Release assets, not arbitrary user loading.
- Long preset mute is currently accepted as the safer temporary behavior.

## Build/config baseline to preserve
- Audio/performance validation: `Release | x64` only.
- Standalone audio test: run without debugger (`Ctrl+F5`).
- Do not evaluate DSP performance in Debug.
- Do not run builds/tests unless the user explicitly asks.
- Do not assume local `config.h` matches the committed default; the user may toggle mode locally while testing.

## Current policy notes
- Audio-thread rules still apply:
  - no allocations
  - no locks or waits
  - no file/network/UI/logging in callback
  - no exceptions across the callback
- Keep diffs small and reviewable.
- Dev-mode policy is active:
  - breaking changes are acceptable if they improve architecture or iteration speed

## Suggested next coding step
1. Update roadmap docs if they drift again after future commits.
2. Shift focus away from Release-mode asset plumbing for now.
3. Start `Milestone E`: gate UX reshape (one-knob behavior).
4. Revisit Release-mode asset variants later, once the final bundled model set and hardware-switch mapping are known.

## Starter prompt for the next agent
You are continuing work in `D:\Dev\NAMPlugin` on branch `amp-slot-architecture`.

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
1. Do not resume old Milestone A cleanup; it is already landed.
2. Treat Release mode as sufficiently scaffolded for now.
3. Focus next on the gate UX remake unless the user explicitly redirects you.

Task request:
1. Inspect the current gate implementation and UI wiring.
2. Propose a minimal one-knob gate remap that keeps DSP RT-safe.
3. Implement the smallest reviewable patch for that gate behavior.
