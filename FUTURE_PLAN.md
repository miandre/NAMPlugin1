# FUTURE_PLAN.md

Last updated: 2026-03-04

Purpose: structured roadmap draft from user ideas, with technical feedback and sequencing.

## Development mode assumptions (important)
- Project is currently in 100% development mode with a single tester.
- Breaking changes are acceptable when they materially improve architecture or iteration speed.
- Backward compatibility for old presets/params is not a hard requirement at this stage.
- Long-term intent:
  - this plugin can remain a "test rig" for model/IR evaluation,
  - future productized amp sims can use fixed bundled sounds/models derived from rig learnings.

## Product direction
- Near-term: keep high-velocity test rig workflows (model/IR loading, diagnostics, flexible routing).
- Mid/long-term: support release-style fixed amp slots with bundled assets and controlled UX.
- Recommended architecture target: one DSP core, two operating profiles:
  - Rig Mode (flexible pickers, diagnostics)
  - Release Mode (fixed bundled amp/cab sets)

## Idea backlog rewritten with feedback

### FX and timing
- FX graphics refresh and control repositioning (UI asset-dependent).
- Add digital display for parameter readout.
  - Feasible in current UI framework; low risk.
- Add DAW tempo sync foundation.
  - High value prerequisite for synced delay and time-based FX.
- Add delay sync to host tempo.
  - Depends on tempo foundation.
- Add ping-pong delay behavior.
  - Feasible; medium DSP tuning risk.
- Add delay ducker.
  - Feasible; needs smooth envelope/gain handling to avoid pumping/clicks.

### EQ and gate UX
- Move HP/LP controls from General to EQ section.
  - Low risk UI/organization change.
- Gate threshold should also shape hold/release behavior (one-knob gate behavior curve).
  - Feasible; medium tuning risk.
- If gate becomes one-knob stable, move gate control to top-level bar (independent from stomp bypass).
  - Good UX simplification.

### Cab system (major feature)
- Build interactive cab UI:
  - microphone selection,
  - mic cone position,
  - optional mic distance.
- Implement IR interpolation strategy:
  - 1D (cone position): blend between adjacent IR captures.
  - 2D (position + distance): blend up to 4 neighboring IRs.
- Keep dual-cab behavior and blend options:
  - global blend mode,
  - per-cab pan + level mode.
- Bundle curated IR sets in build while retaining optional external IR loading.
- Feedback:
  - High value but high complexity and CPU risk.
  - Best shipped in phases (1D first, then 2D, then advanced dual-cab placement).

### Transpose
- Current latency is still problematic.
- Decision path:
  - investigate lower-latency mode tradeoffs,
  - if unacceptable quality/latency tradeoff remains, remove or demote feature.
- Feedback:
  - likely constrained by algorithmic tradeoff; treat as quality-mode decision, not only bug fix.

### Doubler
- Add audition-oriented doubler to mimic double-tracked guitar feel.
- Better than simple L/R delay (e.g., micro pitch/time modulation with width control).
- Initial mono-compatibility can be lower priority.
- Feedback:
  - high musical value for audition workflow; medium implementation risk.

### Amp architecture and model workflow
- Improve per-amp independent control behavior/tone-stack ownership.
- Long-term product direction:
  - fixed model(s) per amp slot,
  - optional slot-specific model switching for hi/lo gain variants.
- Feedback:
  - highly aligned with release goals.
  - in dev mode, can break current picker workflows if needed.
  - still recommended to keep a Rig Mode path for rapid auditioning.

### Cleanup and optimization
- Review all custom changes since fork baseline.
- Refactor structure where needed (FX/sections already started).
- Identify code smells and optimization opportunities.
- Feedback:
  - run continuously per milestone, not one giant final cleanup.

## Proposed implementation order
1. Tempo/transport foundation (host sync + internal timing state).
2. Delay upgrades (tempo sync, ping-pong, ducker).
3. Cab system v1 (1D mic-position blending with bundled IR asset pipeline).
4. Amp slot architecture pass (fixed-slot model mapping + Rig Mode/Release Mode boundaries).
5. Cab system v2 (distance/2D interpolation, optional per-cab pan/level mode).
6. Ongoing cleanup/perf pass after each milestone (not deferred to the end).

## Additional recommendations
- Add explicit feature flags/modes to isolate heavy features during host-stability testing.
- Keep host validation matrix lightweight but consistent (LUNA, Reaper, standalone).
- Maintain RT-safe diagnostics only (no callback logging/I/O).

## Next-agent prompt (copy/paste)
You are continuing work in `D:\Dev\NAMPlugin` on branch `main`.

Read first, in this exact order:
1) `AGENTS.md`
2) `SKILLS.md`
3) `AGENT_SESSION_SUMMARY.md`
4) `FUTURE_PLAN.md`

Then confirm repo/submodule state:
- `git status --short`
- `git branch --show-current`
- `git rev-parse --short HEAD`
- `git submodule status iPlug2`
- `git -C iPlug2 remote -v`

Task:
1) Convert `FUTURE_PLAN.md` into milestone-sized implementation tickets.
2) Recommend smallest/lowest-risk first patch in milestone 1.
3) For each ticket, list expected impact, technical risk, files/symbols touched, and RT-safety concerns.

Constraints:
- Keep diffs minimal.
- Keep audio thread RT-safe.
- Do not run builds/tests unless explicitly requested.
- Dev mode: breaking changes are allowed when they improve architecture/iteration speed.
