Last updated: 2026-03-21

Purpose: active roadmap for remaining work.
This file is mandatory onboarding context for every new agent.

## Development mode assumptions
- Project is in active development with a single tester
- Breaking changes are acceptable when they materially improve architecture or iteration speed
- Backward compatibility for old presets is not a hard requirement right now

## Completed milestones

### Done: Tempo/transport + synced delay foundation
- Host/manual tempo handling implemented
- Standalone manual tempo workflow implemented
- Delay time sync mode implemented

### Done: Delay upgrades
- Ping-pong behavior implemented and corrected for mono/stereo input behavior
- Delay ducker implemented with extended usable range
- Delay digital readout implemented and polished

### Done: EQ/FX UI transition work
- EQ moved to dedicated page
- FX/EQ navigation decoupled and fixed
- FX/EQ graphics and control layout polish completed

### Done: Milestone A - amp slot architecture completion
Outcome:
- Rig/Release split is landed
- Release slot model handling is deterministic and resilient to preset/path drift
- Slot model source abstraction is in place for future fixed/bundled assets

### Done: Release-mode fixed-asset scaffold v1
Outcome:
- `NAM_RELEASE_MODE=1` behaves like a first "release-feel" runtime path without full packaging yet

### Done: Milestone E - gate UX reshape
Outcome:
- Gate now behaves as a one-knob macro instead of a raw threshold-only feel

### Done: Milestone G - doubler v1
Outcome:
- Doubler works as a double-track preview rather than a simple widener

### Done: Milestone H - proper stereo/mono input and output metering
Outcome:
- Metering now clearly reflects the effective mono/stereo behavior of the current path

### Done: Preset/session restore stabilization follow-up
Outcome:
- Preset context now survives restart/session reopen much more clearly

### Done: Milestone C - Cab system v1
Outcome:
- Interactive dual-cab workflow is now the main cab architecture

### Done: Release-mode curated cab embedding v1
Outcome:
- Curated cab IRs for the current mic set are embedded in release builds

### Done: Milestone I - Compressor stomp pedal v1
Outcome:
- A built-in compressor stomp is now available with a minimal musical control set

Landed work:
- Controls:
  - `Amount`
  - `Level`
  - `Soft/Hard`
  - on/off
- Built-in DSP compressor, not a NAM model
- RT-safe state/smoothing integrated into the current signal path

### Done: Boost v2
Outcome:
- The boost pedal now supports switchable `A/B` model variants while sharing one control surface

Landed work:
- `A/B` boost model storage
- UI switch for boost voice selection
- boost drive control
- state/preset/session restore for both boost model paths

## Active milestones

### Milestone J: Amp model variants v1
Goal:
- Add per-slot amp model variants, starting with `A/B`, while keeping the rest of each amp slot's controls shared

Current intended product behavior:
- Each amp slot owns multiple models of the same amp family
- Only one variant is active at a time
- Shared controls such as gain and tonestack stay common to the amp slot
- Architecture should be future-proof for more than two variants later

Current implementation state:
- Backend storage/load/swap plumbing is being migrated from `slot` to `slot + variant`
- Serialization is being updated to store per-variant amp paths plus the selected variant per slot
- Settings UI now includes amp model browsers for `A` and `B`
- Amp 2 has a first working front-panel variant switch
- Amp 1 and Amp 3 are intentionally backend-ready only for now

Recommended v1 boundary:
- Commit only the current first slice:
  - per-slot `A/B` model storage and persistence
  - active-variant switching
  - Amp 2 front-panel switch
- Defer extra front-panel switches for Amp 1 / Amp 3 unless explicitly requested

Risk:
- Medium

RT safety watch-outs:
- No allocations or dynamic graph changes in the callback
- Keep model swap ownership handoff deterministic
- Preserve existing preset-recall mute/swap safety

### Milestone F: Transpose decision path
Goal:
- Decide whether transpose should stay, be simplified, or be removed based on latency and quality tradeoffs

Current state:
- Feature is hidden
- Idle off-path CPU issue was already reduced
- App-side "transpose seems on" behavior appears to have been old standalone-state restore rather than a changed default

Risk:
- Medium

### Milestone B: Release asset packaging and final variant strategy
Goal:
- Finish the remaining release-mode packaging strategy beyond the current curated cab embedding

Current state:
- Curated cab IR embedding exists
- Final amp/stomp embedded asset strategy is still incomplete
- Final model list is not known yet
- Hardware-switch mapping for amp variants is not finalized yet

What can wait:
- encryption/obfuscation decisions
- final asset-ID catalog
- hardware-switch variant mapping

Resume this milestone when:
- the bundled model set is clearer
- the amp hardware-switch behavior is specified

Risk:
- Medium-high

RT safety watch-outs:
- No decrypt/decompress/file I/O on audio thread

### Milestone D: Cab system v2
Goal:
- Add distance axis and more advanced routing after Cab v1 proves stable

Possible scope:
- distance control
- additional curated mic sets
- different cab/speaker options per side
- more advanced dual-cab level/pan workflows

Risk:
- High

RT safety watch-outs:
- Keep interpolation and routing deterministic and allocation-free in the callback

## Low-priority polish lane
- Continue cab/amp UI polish only if the user explicitly wants it
- Keep those changes UI-only and isolated from DSP/state work where possible

Examples:
- per-slot art tweaks
- hover-state tuning
- off-state visual language
- cab control alignment/padding cleanup

## Recommended execution order from now
1. Milestone J: amp model variants v1
2. Milestone F: transpose decision path only if the user returns to it
3. Resume Milestone B when final release asset set and hardware-switch behavior are clearer
4. Milestone D only after Cab v1 remains stable in regular use
5. Treat additional UI polish as a low-risk side lane, not the main roadmap

## Next-agent prompt
You are continuing work in `D:\Dev\NAMPlugin` on branch `main`.

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

Current direction:
1. Metering, gate/stomp decoupling, preset/session restore fixes, Cab V1, compressor stomp v1, and boost v2 are already landed
2. Do not reopen the metering or plugin preset-restore paths unless the user asks
3. Release mode embeds curated cab IRs but still allows custom IR loading
4. Prefer small, reviewable follow-ups from the current stable baseline

Suggested next task unless the user redirects:
1. Inspect the current dirty-tree amp variant work
2. Preserve the intended first slice:
  - per-slot `A/B` model storage/loading/persistence
  - Amp 2 front-panel switch
3. If making code changes, keep the patch RT-safe and reviewable

Constraints:
- Keep diffs minimal
- Keep audio thread RT-safe
- Do not run builds/tests unless explicitly requested
- Dev mode: breaking changes are allowed when they improve architecture and iteration speed
