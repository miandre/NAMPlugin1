Last updated: 2026-03-30

Purpose: active roadmap for remaining work.
This file is mandatory onboarding context for every new agent.

## Development mode assumptions
- Project is in active development with a single tester
- Breaking changes are acceptable when they materially improve architecture or iteration speed
- Backward compatibility for old presets is not a hard requirement right now

## Completed milestones

### Done: Tempo/transport + synced delay foundation
Outcome:
- Host/manual tempo handling implemented
- Standalone manual tempo workflow implemented
- Delay time sync mode implemented

### Done: Delay upgrades
Outcome:
- Ping-pong behavior implemented and retuned for more balanced mono ping-pong repeats
- Delay ducker implemented with extended usable range
- Delay digital readout implemented and polished

### Done: EQ/FX UI transition work
Outcome:
- EQ moved to dedicated page
- FX/EQ navigation decoupled and fixed
- FX/EQ graphics and control layout polish completed

### Done: Amp slot architecture completion
Outcome:
- Rig/Release split is landed
- Release slot model handling is deterministic and resilient to preset/path drift
- Slot model source abstraction is in place for future fixed/bundled assets

### Done: Cab system v1 + curated cab embedding
Outcome:
- Interactive dual-cab workflow is now the main cab architecture
- Release builds embed the curated cab IR set

### Done: Gate UX reshape
Outcome:
- Gate now behaves as a one-knob macro instead of a raw threshold-only feel

### Done: Doubler improvement pass
Outcome:
- Doubler now works as a convincing double-track preview baseline for jamming / tone evaluation

### Done: Proper stereo/mono input and output metering
Outcome:
- Metering now clearly reflects the effective mono/stereo behavior of the current path

### Done: Preset/session restore stabilization follow-up
Outcome:
- Preset context now survives restart/session reopen much more clearly

### Done: Compressor stomp pedal v1
Outcome:
- A built-in compressor stomp is now available with a minimal musical control set

### Done: Boost v2
Outcome:
- The boost pedal supports switchable `A/B` model variants while sharing one control surface

### Done: Amp model variants v1
Outcome:
- Each amp slot supports `A/B` model variants with shared per-slot controls

### Done: Default asset baseline refresh
Outcome:
- Startup and the `Default` preset now use the current `Amp1A/B ... Amp3A/B`, `BoostA/B`, and curated stereo cab baseline

### Done: Amp-face scaffolding + release amp UI v1
Outcome:
- Amp presentation and behavior can now diverge per slot without rewriting the whole editor

### Done: Tuner improvement pass
Outcome:
- The tuner is much more usable and the major center-jump bug is fixed

Important note:
- Do not casually reopen tuner behavior

### Done: Amp/IR switching artifact reduction
Outcome:
- Amp variant switching, amp slot switching, cab source changes, and IR changes are materially cleaner on the merged `main` baseline

Settled constants:
- `kAmpSlotSwitchDeClickSamples = 512`
- `kAmpModelVariantCrossfadeSamples = 512`
- `kPathToggleTransitionSamples = 512`
- `kAmpSlotTransitionSamples = 3072`
- `kOutputGainSmoothTimeSeconds = 0.02`
- `kIRTransitionSamples = 12288`

### Done: External-facing `RE-AMP` rebrand
Outcome:
- Standalone/product naming, metadata, icons, and packaging now present as `RE-AMP`

Important nuance:
- Internal `NeuralAmpModeler` code/project naming remains for now

### Done: Amp bypass routing/state follow-up
Outcome:
- AMP section bypass and amp-face on/off now bypass the expected full amp stage cleanly
- `Normalized` output mode no longer scales dry audio when the model stage is bypassed

### Done: Curated `MD-421` cab addon
Outcome:
- Curated cab source list now includes `M-421`
- Release-mode embedded IRs and runtime bitmap/resource wiring are in place

## Active milestones

### Milestone L: Custom tone stack for one amp slot
Goal:
- Add a dedicated custom tone stack behavior for one amp while preserving the current slot-specific amp architecture

Why now:
- The slot presentation / behavior / resolved-spec scaffolding is already in place
- This is the next likely user-directed feature

Recommended implementation shape:
- Start by mapping the current tone-stack creation path and slot-specific amp spec flow
- Add or adapt one tone-stack implementation for the chosen amp slot
- Reuse the existing slot-specific wiring instead of creating a separate parallel path
- Keep control ownership, bypass behavior, and master/gain staging consistent with the current amp-stage baseline

Risk:
- Medium

RT safety watch-outs:
- No allocations/locks/I/O/logging in audio-thread paths
- If controls change coefficients continuously, smooth them if needed to avoid zipper noise

### Milestone B: Release asset packaging and final variant strategy
Goal:
- Finish the remaining release-mode packaging strategy beyond the current curated cab and amp variant embedding

Current state:
- Curated cab IR embedding exists
- Release amp variant preload exists
- Final stomp/amp embedded asset strategy is still incomplete
- Final bundled model list is still not fully settled

What can wait:
- encryption/obfuscation decisions
- final asset-ID catalog
- broader packaging hardening

Risk:
- Medium-high

RT safety watch-outs:
- No decrypt/decompress/file I/O on audio thread

### Milestone F: Transpose decision path
Goal:
- Decide whether transpose should stay, be simplified, or be removed based on latency and quality tradeoffs

Current state:
- Feature is hidden
- The earlier "transpose seems on" report appeared to have been old standalone-state restore rather than a changed default

Risk:
- Medium

### Milestone D: Cab system v2
Goal:
- Add a distance axis and more advanced routing after Cab v1 remains stable

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
- Internal rename cleanup from `NeuralAmpModeler` to `RE-AMP` can wait unless the user asks
- Continue amp/cab UI polish only if the user explicitly wants it
- Keep those changes UI-only and isolated from DSP/state work where possible

Examples:
- per-slot art tweaks
- label alignment/padding cleanup
- hover-state tuning
- off-state visual language

## Recommended execution order from now
1. Milestone L: custom tone stack for one amp slot
2. Resume Milestone B when the final release asset set is clearer
3. Milestone F only if the user returns to transpose
4. Milestone D only after Cab v1 remains stable in regular use
5. Treat additional UI polish as a low-risk side lane, not the main roadmap

## Next-agent note
Use the starter prompt in `AGENT_SESSION_SUMMARY.md`.
