Last updated: 2026-06-12

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

### Done: Modeled boost pedals
Outcome:
- The live boost pedal now uses two built-in modeled voices with a shared control surface

Important nuances:
- The boost mode switch now compares `TS` and `PD` voices in the live path
- User-facing boost controls now display `0..10` with `5` at noon and no unit text
- The old boost NAM slot plumbing still exists in code/settings for possible future development use, but it is no longer the live boost signal path

### Done: Amp model variants v1
Outcome:
- Each amp slot supports `A/B` model variants with shared per-slot controls

### Done: Default asset baseline refresh
Outcome:
- Startup and the `Default` preset now use the current `Amp1A/B ... Amp3A/B`, `BoostA/B`, and curated stereo cab baseline

### Done: Amp-face scaffolding + release amp UI v1
Outcome:
- Amp presentation and behavior can now diverge per slot without rewriting the whole editor

### Done: Tuner 2.0 baseline + BYP monitor fix
Outcome:
- The tuner detection path was rebuilt around a cleaner MPM/NSDF-style baseline
- Guitar tuning is materially more stable across standard 7-string range and tested alternate tunings
- 5-string bass low B and C-tuned bass low C are now handled after moving to an `8192` analysis frame
- Tuner BYP monitor output no longer depends on the amp model active when opening the tuner

Important nuances:
- Current tuner detection is considered the accepted baseline; avoid rewriting it unless new test evidence justifies it
- Keep any future tuner UI changes isolated from detector/DSP changes where practical

### Done: Tuner smoothing and in-tune UI overview
Outcome:
- Tuner motion was retuned for a calmer response without restoring the previous heavy display lag
- The in-tune state now gives the user a clear target instead of requiring exact overlap with a same-width center line
- The final graphics were evaluated through several user-tested iterations and approved

Important nuances:
- Analyzer-side smoothing remains the single source of displayed cents motion
- In-tune entry is `+/-1.0` cent with exit hysteresis at `+/-1.2` cents
- The center uses a two-marker gate, while note/cents/needle turn green together in tune
- The baseline uses mirrored red-to-amber-to-green gradients and keeps neutral ticks
- The needle dot is intentionally larger than the gate-sizing reference; future needle art changes should not silently alter tuning tolerance
- The `MUTE` / `BYP` / `ON` workflow and existing panel layout were preserved
- Landed commits:
  - `8b918ee` `Polish tuner response feel`
  - `4670e51` `Improve tuner in-tune guidance`

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

### Done: Amp 2 custom voicing pass
Outcome:
- Amp 2 now has a slot-specific tuned tone stack, slot-specific `Pre Gain` taper, and a saturating `Master` behavior while keeping the same control surface

Important nuances:
- Amp 2 `A/B` variants still share one tone stack/master behavior; the variant switch remains model-only
- Amp 2 `Pre Gain` is intentionally `-20 dB / -5 dB / +10 dB` at min / noon / max

### Done: Dev diagnostics overlay
Outcome:
- A dev diagnostics overlay is available for both standalone and plugin builds

Important nuances:
- Controlled by `NAM_DEV_DIAGNOSTICS` in `NeuralAmpModeler/config.h`
- Standalone shows DSP stats plus process CPU `current/average/peak` and RAM
- Plugin builds intentionally omit CPU/RAM and show only DSP/buffer/latency stats

### Done: Reverb output normalization UX pass
Outcome:
- Reverb output now uses an upper-half mix-dependent post-mix compensation curve to keep perceived loudness more consistent as the dry anchor fades out.

Important nuances:
- The landed implementation is a small DSP-only follow-up in `_ProcessFXReverbStage()` inside `NeuralAmpModeler/NeuralAmpModelerFX.cpp`.
- The current dry/wet blend law was intentionally preserved in this pass; compensation is applied after the existing dry/wet sum.
- The current user-tuned values on `main` are:
  - `postMixCompStart = 0.49`
  - `postMixCompMaxGain = 2.4`
  - `postMixCompCurve = pow(postMixCompNorm, 1.40)`
- If future tuning is needed, start with those constants before changing the wet-path voicing or blend law.
- The main residual risk to watch is headroom pressure at high `Mix` plus long `Decay`, because the post-mix gain stacks with the pre-existing wet makeup.

## Active milestones

### Future candidate: Amp 1 tone-stack and control redesign
Goal:
- Explore a more intentional Amp 1 voicing and control surface instead of leaving Amp 1 on the generic tone-stack behavior

Current state:
- Amp 1 uses `ToneStackKind::BasicNam`
- Amp 1 uses `MasterBehaviorKind::Standard`
- Its current shared controls are `Pre Gain`, `Bass`, `Mid`, `Treble`, `Presence`, `Depth`, and `Master`
- Amp 1 A/B remains a model-only `CHARACTER` switch with shared controls
- Amp 2 already provides the established pattern for slot-specific tone-stack, master, controls, and presentation behavior

Possible scope:
- Define the intended Amp 1 archetype and musical role before choosing DSP
- Evaluate a dedicated `Amp1ToneStack` and slot-specific pre-gain/master behavior
- Decide whether `Presence` and `Depth` remain continuous controls or whether one/both should be replaced or reshaped
- Consider slot-specific labels, ranges, tapers, switches, and control placement
- Keep A/B variants model-only unless there is a deliberate reason to make controls variant-specific
- Reuse existing parameter IDs where practical; discuss any parameter or serialization changes before implementation

Likely implementation points:
- `NeuralAmpModeler/ToneStack.h` and `ToneStack.cpp`
- `NeuralAmpModeler/NeuralAmpModeler.h`
- `_GetAmpSlotPresentationSpec()`, `_GetAmpSlotBehaviorSpec()`, `_CreateToneStack()`, and master/pre-gain mapping in `NeuralAmpModeler.cpp`
- `NeuralAmpModelerControls.h` and amp artwork/resources only if the agreed control surface requires UI changes

Decision gate:
- Do not code this from the generic idea alone
- First agree on target voicing, control names/types, ranges, defaults, and whether preset/state behavior may change

Risk:
- Medium

RT safety watch-outs:
- Tone-stack filters and master behavior must be prepared outside the callback and process deterministically
- No allocations, locks, string construction, I/O, or logging in `ProcessBlock()` paths
- Avoid adding callback-time slot branching beyond the established resolved behavior/state pattern

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
- Decide later whether the old boost NAM slot plumbing should be removed, hidden more deeply, or kept as a dev-only capability

Examples:
- per-slot art tweaks
- label alignment/padding cleanup
- hover-state tuning
- off-state visual language

## Recommended execution order from now
1. If Amp 1 is selected next, complete the voicing/control design discussion before implementation
2. Resume Milestone B when the final release asset set is clearer
3. Milestone F only if the user returns to transpose
4. Milestone D only after Cab v1 remains stable in regular use
5. Treat additional UI polish as a low-risk side lane, not the main roadmap
6. If reverb loudness is revisited, treat it as tuning on the landed post-mix compensation curve rather than a new milestone

## Next-agent note
Use the starter prompt in `AGENT_SESSION_SUMMARY.md`.
