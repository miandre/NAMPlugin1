Last updated: 2026-03-26

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
- Ping-pong behavior implemented and then retuned for more balanced mono ping-pong repeats
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

### Done: Milestone C - Cab system v1
Outcome:
- Interactive dual-cab workflow is now the main cab architecture

### Done: Release-mode curated cab embedding v1
Outcome:
- Curated cab IRs for the current mic set are embedded in release builds

### Done: Milestone E - gate UX reshape
Outcome:
- Gate now behaves as a one-knob macro instead of a raw threshold-only feel

### Done: Milestone G - doubler improvement pass
Outcome:
- Doubler now works as a convincing double-track preview baseline for jamming / tone evaluation

Important landed detail:
- The right-side soft clip experiment was ultimately removed because it caused audible crackle with some curated IR combinations

### Done: Milestone H - proper stereo/mono input and output metering
Outcome:
- Metering now clearly reflects the effective mono/stereo behavior of the current path

### Done: Preset/session restore stabilization follow-up
Outcome:
- Preset context now survives restart/session reopen much more clearly

### Done: Milestone I - Compressor stomp pedal v1
Outcome:
- A built-in compressor stomp is now available with a minimal musical control set

### Done: Boost v2
Outcome:
- The boost pedal supports switchable `A/B` model variants while sharing one control surface

### Done: Milestone J - Amp model variants v1
Outcome:
- Each amp slot supports `A/B` model variants with shared per-slot controls

### Done: Default asset baseline refresh
Outcome:
- Startup and the `Default` preset now use the current `Amp1A/B ... Amp3A/B`, `BoostA/B`, and curated stereo cab baseline

### Done: Amp-face scaffolding + release amp UI v1
Outcome:
- Amp presentation and behavior can now diverge per slot without rewriting the whole editor

Landed work:
- Slot presentation / behavior / resolved-spec scaffolding
- Spec-driven tone-stack creation
- Slot 1 `CHARACTER` variant switch
- Slot 2 `DEPTH` / `SCOOP` buttons with `Amp2ToneStack`
- Slot 3 `BRIGHT` / `DEPTH` switch-driven face
- High-resolution amp knob/switch resource loading
- Direct bitmap rotation for amp knobs to avoid wobble

### Done: Tuner improvement pass
Outcome:
- The tuner is much more usable and the major center-jump bug is fixed

Important note:
- The key tuner fix was removing the forced cents reset to `0` on committed note changes
- Several attempted simplifications were tested and rejected; do not casually reopen tuner behavior

### Done on branch `reduce-amp-switch-artifacts`: Milestone K - amp variant/slot switch artifact reduction
Outcome:
- Amp variant switching is materially cleaner, especially in `Normalized` mode
- Amp slot switching now trades the old click/clonk for a short acceptable masked handoff
- Hard amp/stomp section toggles were also cleaned up as part of the same investigation

Settled implementation points:
- committed audio-side amp slot shadowing for tone stack / pre gain / master
- normalized output-gain smoothing
- same-slot amp variant crossfade
- short masked slot transition

Settled constants:
- `kAmpSlotSwitchDeClickSamples = 512`
- `kAmpModelVariantCrossfadeSamples = 512`
- `kPathToggleTransitionSamples = 512`
- `kAmpSlotTransitionSamples = 3072`
- `kOutputGainSmoothTimeSeconds = 0.02`

### Done on branch `reduce-amp-switch-artifacts`: Cab / IR switch artifact reduction
Outcome:
- Cab section, curated IR changes, custom IR arrow changes, and curated slider boundary swaps are materially cleaner

Settled implementation points:
- the failed whole-output IR mute path was replaced with slot-local cab-stage old/new IR crossfades
- curated IR pairs are staged together so each branch has a complete primary/secondary pair during the blend

Settled constant:
- `kIRTransitionSamples = 12288`

## Active milestones

### Milestone B: Release asset packaging and final variant strategy
Goal:
- Finish the remaining release-mode packaging strategy beyond the current curated cab and amp variant embedding

Current state:
- Curated cab IR embedding exists
- Release amp variant preload exists
- Final stomp/amp embedded asset strategy is still incomplete
- Final model list is not known yet

What can wait:
- encryption/obfuscation decisions
- final asset-ID catalog
- any broader packaging hardening

Resume this milestone when:
- the bundled model set is clearer

Risk:
- Medium-high

RT safety watch-outs:
- No decrypt/decompress/file I/O on audio thread

### Milestone F: Transpose decision path
Goal:
- Decide whether transpose should stay, be simplified, or be removed based on latency and quality tradeoffs

Current state:
- Feature is hidden
- App-side "transpose seems on" behavior appears to have been old standalone-state restore rather than a changed default

Risk:
- Medium

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
- Continue amp/cab UI polish only if the user explicitly wants it
- Keep those changes UI-only and isolated from DSP/state work where possible

Examples:
- per-slot art tweaks
- label alignment/padding cleanup
- hover-state tuning
- off-state visual language

## Recommended execution order from now
1. Validate the current `reduce-amp-switch-artifacts` branch in `Release | x64` and merge/cherry-pick it if the user is satisfied
2. Resume Milestone B when final release asset set is clearer
3. Milestone F: transpose decision path only if the user returns to it
4. Milestone D only after Cab v1 remains stable in regular use
5. Treat additional UI polish as a low-risk side lane, not the main roadmap

## Next-agent prompt
You are continuing work in `D:\Dev\NAMPlugin` on branch `reduce-amp-switch-artifacts`.

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
1. Metering, gate/stomp decoupling, preset/session restore fixes, Cab V1, compressor stomp v1, boost v2, amp variants v1, doubler improvements, startup/default asset refresh, and slot-specific amp UI work are already landed
2. Do not reopen tuner work unless the user explicitly asks
3. Release mode embeds curated cab IRs and amp variant assets while still allowing custom IR loading
4. Prefer small, reviewable follow-ups from the current stable baseline

Suggested next task unless the user redirects:
1. Look into minor polishing. Change name and Icon for the app. "RE-AMP" is the new name
