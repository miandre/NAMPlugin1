Last updated: 2026-03-22

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

### Done: Boost v2
Outcome:
- The boost pedal supports switchable `A/B` model variants while sharing one control surface

### Done: Milestone J - Amp model variants v1
Outcome:
- Each amp slot now supports `A/B` model variants with shared per-slot controls

Landed work:
- Per-slot `A/B` amp model storage/loading/persistence
- Selected variant restored per amp slot
- Amp 2 front-panel variant switch
- Amp 1 and Amp 3 backend-ready but no front-panel switch yet
- Release mode preload updated to `Amp1A/B`, `Amp2A/B`, `Amp3A/B`

### Done: Tuner improvement pass
Outcome:
- The tuner is much more usable and the major center-jump bug is fixed

Landed work:
- Faster analyzer updates
- Improved high-string acquisition
- Correct semitone-boundary rollover behavior
- Numeric signed cents readout
- Dual accidental note labels such as `A#/Bb`
- Narrower, steadier tuner UI
- Tuner monitor default set to `MUTE`

Important note:
- The key tuner fix was removing the forced cents reset to `0` on committed note changes
- Several attempted simplifications were tested and rejected; do not casually reopen tuner behavior

## Active milestones

### Milestone K: Doubler improvement pass
Goal:
- Improve the virtual/fake doubler so it feels more predictable and more musical in real use

Current implementation shape:
- Main insertion is post-cab and pre-delay/reverb
- Main DSP lives in:
  - `NeuralAmpModeler/NeuralAmpModelerFX.cpp`
- Main state/plumbing lives in:
  - `NeuralAmpModeler/NeuralAmpModeler.cpp`
  - `NeuralAmpModeler/NeuralAmpModeler.h`
- Mono-source-only availability
- Short-delay-based "other take" synthesis
- Onset-driven retargeting/jitter
- Asymmetric left/right voicing
- Tone shaping, width processing, and output compensation
- Only one exposed user control: amount

Current concerns:
- Too much hidden behavior behind one control
- Hard for the user to build a simple mental model of what the doubler is doing
- Likely to sound good in some cases and phasey or synthetic in others without an obvious fix
- Changes here will likely be more subjective and iterative than tuner changes

Recommended first step:
- Do a focused read/review pass and summarize the current algorithm before changing DSP
- Identify the smallest change that improves predictability without adding much architecture churn

Risk:
- Medium-high

RT safety watch-outs:
- Keep all processing allocation-free in the callback
- No random heap use, locks, file access, or dynamic graph changes

### Milestone F: Transpose decision path
Goal:
- Decide whether transpose should stay, be simplified, or be removed based on latency and quality tradeoffs

Current state:
- Feature is hidden
- App-side "transpose seems on" behavior appears to have been old standalone-state restore rather than a changed default

Risk:
- Medium

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
1. Milestone K: doubler improvement pass
2. Milestone F: transpose decision path only if the user returns to it
3. Resume Milestone B when final release asset set is clearer
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
1. Metering, gate/stomp decoupling, preset/session restore fixes, Cab V1, compressor stomp v1, boost v2, amp variants v1, and tuner improvements are already landed
2. Do not reopen tuner work unless the user explicitly asks
3. Release mode embeds curated cab IRs and amp variant assets while still allowing custom IR loading
4. Prefer small, reviewable follow-ups from the current stable baseline

Suggested next task unless the user redirects:
1. Inspect the current doubler implementation and summarize what it is doing today
2. Focus files:
  - `NeuralAmpModeler/NeuralAmpModeler.cpp`
  - `NeuralAmpModeler/NeuralAmpModelerFX.cpp`
  - `NeuralAmpModeler/NeuralAmpModeler.h`
3. Identify the smallest RT-safe first improvement that makes the doubler feel more predictable or controllable

Constraints:
- Keep diffs minimal
- Keep audio thread RT-safe
- Do not run builds/tests unless explicitly requested
- Dev mode: breaking changes are allowed when they improve architecture and iteration speed
