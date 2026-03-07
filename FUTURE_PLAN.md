Last updated: 2026-03-07

Purpose: active roadmap for remaining work.
This file is mandatory onboarding context for every new agent.

## Development mode assumptions
- Project is in active development with a single tester.
- Breaking changes are acceptable when they materially improve architecture or iteration speed.
- Backward compatibility for old presets is not a hard requirement right now.

## Completed milestones

### Done: Tempo/transport + synced delay foundation
- Host/manual tempo handling implemented.
- Standalone manual tempo workflow implemented.
- Delay time sync mode implemented.

### Done: Delay upgrades
- Ping-pong behavior implemented and corrected for mono/stereo input behavior.
- Delay ducker implemented with extended usable range.
- Delay digital readout implemented and polished.

### Done: EQ/FX UI transition work
- EQ moved to dedicated page.
- FX/EQ navigation decoupled and fixed.
- FX/EQ graphics and control layout polish completed.

### Done: Milestone A - amp slot architecture completion
Outcome:
- Rig/Release split is landed.
- Release slot model handling is deterministic and resilient to preset/path drift.
- Slot model source abstraction is in place for future fixed/bundled assets.

Landed work:
- Rig/Release workflow mode scaffold.
- Slot edit lock policy and user-initiated edit gating.
- Fixed-slot path handling and restore protection.
- Slot model source abstraction:
  - `ExternalPath`
  - `EmbeddedModelId`
- Safer slot switching:
  - defer swap until target model is ready
  - de-click on actual swap
- Safer preset recall:
  - deterministic mute instead of hybrid old-model/new-parameter rendering

### Done: Release-mode fixed-asset scaffold v1
Outcome:
- `NAM_RELEASE_MODE=1` now behaves like a first "release-feel" runtime path without full packaging yet.

Landed work:
- Fixed amp/stomp/IR asset manifest scaffold.
- Controlled asset resolution from known `tmpLoad` locations.
- Release-mode startup/default/preset restore applies those fixed assets.
- Release-mode stomp/IR browsing is disabled or ignored.

Current limitation:
- This is still controlled-path loading, not true embedded payload packaging.

## Active milestones

### Milestone E: Gate UX reshape (next suggested focus)
Goal:
- Replace the current gate feel with a more musical one-knob behavior.

Desired outcome:
- One primary gate control that drives threshold plus hold/release contour in a predictable curve.
- Better feel on palm mutes, sustained notes, and noisy pickups.
- Minimal UI churn.

Suggested scope:
1. Keep the control surface small.
2. Implement an internal macro mapping from knob value to threshold and timing.
3. Keep DSP cost trivial and RT-safe.
4. Tune by ear in Rig mode first.

Risk:
- Medium. Mostly tuning risk, low architectural risk.

RT safety watch-outs:
- No allocations, locks, or expensive curve machinery in the callback.
- Prefer simple math and precomputed/clamped parameter mapping.

### Milestone F: Transpose decision path
Goal:
- Decide whether transpose should stay, be simplified, or be removed based on latency and quality tradeoffs.

Why it matters:
- It affects perceived responsiveness and product scope.
- Better to decide before more feature work depends on it.

Risk:
- Medium. User-facing behavior change likely.

### Milestone B: Release asset packaging and final variant strategy
Goal:
- Evolve the current Release-mode scaffold into the final fixed/bundled asset system.

Current state:
- Runtime scaffold exists.
- Packaging pipeline does not.
- Final model list is not known yet.
- Hardware-switch mapping for amp variants is not finalized yet.

What can wait:
- true embedded payload format
- encryption/obfuscation decisions
- final asset-ID catalog
- hardware-switch variant mapping

Resume this milestone when:
- the bundled model set is clearer
- the amp hardware-switch behavior is specified

Risk:
- Medium-high. Tooling and runtime integration risk.

RT safety watch-outs:
- No decrypt/decompress/file I/O on audio thread.

### Milestone C: Cab system v1 (1D interpolation)
Goal:
- Introduce musically useful mic-position interpolation with manageable complexity.

Scope:
- 1D cone-position interpolation first.
- Curated bundled IR support.
- External IR fallback can remain in Rig mode.

Dependency note:
- Better started once the curated cab/IR content is clearer.

Risk:
- High CPU/complexity if not phased carefully.

RT safety watch-outs:
- Precompute interpolation metadata.
- No dynamic allocation in the callback.

### Milestone G: Doubler
Goal:
- Add an audition-oriented doubler using bounded micro pitch/time modulation and width control.

Risk:
- Medium.

RT safety watch-outs:
- Bounded modulation work and preallocated buffers only.

### Milestone D: Cab system v2 (distance + advanced routing)
Goal:
- Add distance axis and optional dual-cab pan/level workflows.

Risk:
- High. State/UI/DSP complexity.

RT safety watch-outs:
- Keep interpolation and routing deterministic and allocation-free in the callback.

## Recommended execution order from now
1. Milestone E: gate UX reshape.
2. Milestone F: transpose decision path.
3. Resume Milestone B when final Release assets and hardware-switch behavior are clearer.
4. Milestone C after the curated cab direction is better defined.
5. Milestone G as a parallel feature track if desired.
6. Milestone D only after cab v1 proves stable.

## Next-agent prompt
You are continuing work in `D:\Dev\NAMPlugin` on branch `amp-slot-architecture`.

Read first, in this exact order:
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md`
4. `FUTURE_PLAN.md`

Then confirm repo/submodule state:
- `git status --short`
- `git branch --show-current`
- `git rev-parse --short HEAD`
- `git submodule status iPlug2`
- `git -C iPlug2 remote -v`

Task:
1. Inspect the current gate implementation and UI control wiring.
2. Propose a minimal one-knob gate remap with clear RT-safety constraints.
3. Implement the smallest reviewable patch.

Constraints:
- Keep diffs minimal.
- Keep audio thread RT-safe.
- Do not run builds/tests unless explicitly requested.
- Dev mode: breaking changes are allowed when they improve architecture and iteration speed.
