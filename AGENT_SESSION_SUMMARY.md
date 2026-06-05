Last updated: 2026-06-05

Purpose: concise handoff so a new agent can continue from the current stable baseline without replaying the full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md`
4. `FUTURE_PLAN.md`

## Current repository state
- Active branch: `main`
- HEAD at handoff: `dab31ad`
- Working tree at handoff:
  - clean
- Current product name for user-facing outputs: `RE-AMP`
- Internal code/project naming is still mostly `NeuralAmpModeler` and that is intentional for now
- Current curated cab mic set:
  - `S-57`
  - `R-121`
  - `M-421`
- Release mode still embeds curated cab IRs and amp variant assets while allowing user-loaded custom IRs

## Recent relevant history before this handoff update
- 2026-06-05: merged the modeled boost pedal overhaul to `main`; baseline HEAD is now `dab31ad`
- 2026-06-05: finalized boost pedal graphics/layout polish, generic labeling, and user-facing `0..10` boost controls with internal remapping preserved
- 2026-06-04: landed build-time A2 slimmable-size control for all plugin-loaded slimmable NAM models; baseline HEAD is now `2fb6591`
- 2026-06-04: fast-forwarded local `main` to include the `iplug2-upstream-update` branch; baseline HEAD is now `9ae005c`
- 2026-06-04: added a repo-level rule to `AGENTS.md` stating that the user builds manually from Visual Studio and agents must not run builds unless explicitly asked
- `dab31ad` `Polish boost pedal UI`
- `7dd499a` `Route boost mode to modeled pedals`
- `8a776d8` `Add modeled boost DSP`
- `81ab484` `Prepare modeled boost controls`
- `2fb6591` `Apply build-time slim setting to slimmable NAM loads`
- `9ae005c` `Update iPlug2 mono input selection fix`
- `4a47cc4` `Link iPlug2 UTF8 helpers in app and VST3`
- `2866063` `Update iPlug2 popup menu compatibility fix`
- `3c319a4` `Update iPlug2 RtAudio validation fix`
- `1f16bd2` `Update iPlug2 upstream branch`
- `9311b5b` `Update dependencies for A2 NAM support`
- `de8c435` `Add dev diagnostics readout overlay`
- `2f3b0ec` `Add Amp 2 saturating master stage`
- `5e73f81` `Tune Amp 2 tone stack and pre gain`
- `89f5c72` `Add curated MD-421 cab IR option`
- `94429bd` `Fix amp bypass routing and state handling`
- `e544f42` `Remove generated Python bytecode`
- `9e9e4fa` `Rename external product identity to RE-AMP`
- `00d08c4` `Rebrand app assets and standalone metadata`
- `d66c291` `Update future plan`
- `f800e1b` `Merge branch 'reduce-amp-switch-artifacts'`
- `6ab8e9a` `Update artifact work handover`
- `9d378e6` `Reduce IR switching artifacts`
- `e2fed53` `Reduce amp switching artifacts`

## What is completed now

### 1) Artifact reduction work is already merged to `main`
Outcome:
- The old amp/cab switching investigation branch is no longer the active baseline.
- `main` already contains the amp and IR switching cleanup work.

Landed details:
- Amp variant switching is materially cleaner, especially in `Normalized` mode.
- Amp slot switching uses a short masked handoff instead of the older click/clonk behavior.
- Cab enable/source/IR changes use slot-local old/new IR crossfades instead of the failed whole-output mute path.

Settled constants on the merged baseline:
- `kAmpSlotSwitchDeClickSamples = 512`
- `kAmpModelVariantCrossfadeSamples = 512`
- `kPathToggleTransitionSamples = 512`
- `kAmpSlotTransitionSamples = 3072`
- `kOutputGainSmoothTimeSeconds = 0.02`
- `kIRTransitionSamples = 12288`

### 2) External-facing rebrand to `RE-AMP` is landed
Outcome:
- Windows/mac standalone metadata, output naming, and installer/package naming were rebranded externally.

Important nuance:
- Internal source file names, class names, and most project names still use `NeuralAmpModeler`.
- That internal rename has not been done and should not be reopened unless the user asks.

### 3) Amp bypass behavior is fixed on `main`
Outcome:
- Bypassing the whole AMP section now passes dry signal forward into the cab stage instead of muting.
- Turning an amp off with its amp-face on/off button now bypasses the full amp stage, not just the model block.
- `Normalized` output mode no longer applies model normalization gain to dry audio when the model stage is bypassed.

Important landed details:
- The successful fix was in the live amp-stage state selection in `ProcessBlock()`, not more downstream routing guesses.
- The cab handoff cleanup remains part of the current baseline.

### 4) Curated `MD-421` cab support is landed
Outcome:
- The curated cab system now includes a third mic source exposed in the UI as `M-421`.

Landed details:
- Added curated folder token `421`
- Added UI source label `M-421`
- Added `Mic/421.png`
- Added release-mode embedded IR assets for the five `421` captures
- Added missing Windows resource registration in `main.rc` so the new bitmap actually loads at runtime

### 5) Modeled boost pedals are now landed on `main`
Outcome:
- The old boost `A/B` NAM switch path has been replaced in live use by two built-in modeled pedals.
- The boost mode switch now compares `TS` and `PD` voices instead of loading stomp NAMs in the signal path.
- The pedal graphics/layout were refreshed and the boost controls are now presented more like a hardware pedal.

Important landed details:
- Both boost modes are fully programmatic DSP and use preallocated state with 2x oversampled processing.
- `Boost Drive`, `Boost Character`, and `Output Volume` now present as `0.0 .. 10.0` with `5.0` at noon and no unit text.
- Internal remapping preserves the current DSP tuning:
  - Drive control `0..10` maps to the prior `-10..+10 dB` range.
  - Output level control `0..10` maps to the prior `-15..+15 dB` range.
- The live mode switch is intentionally labeled generically (`Boost Type`) so users evaluate by ear rather than by brand matching.
- The old stomp NAM boost assets/paths/loaders still exist in the codebase/settings flow for now; they were not deleted in this pass and may still be useful for future development experiments.

### 6) The broader feature baseline from earlier work still applies
Already landed on `main`:
- Metering overhaul
- Gate/stomp decoupling
- Preset/session restore fixes
- Plugin preset stomp NAM / cab IR restore fixes
- Interactive Cab V1
- Embedded curated cab IRs for release mode
- Compressor stomp pedal v1
- Amp model variants v1
- Doubler improvement pass
- Startup/default asset refresh
- Amp-face scaffolding and slot-specific amp UI work
- Tuner improvements and tuner UI polish

### 7) Amp 2 tuning work is now landed on `main`
Outcome:
- Amp 2 no longer uses the old generic shared voicing as-is.
- Amp 2 now has its own tuned post-model tone stack, slot-specific pre-gain taper, and a saturating master behavior.

Important landed details:
- Amp 2 keeps the same visible control set: `Pre Gain`, `Bass`, `Mid`, `Treble`, `Presence`, `Depth`, `Master`, plus `DEPTH` and `SCOOP`.
- Amp 2 `A/B` variants still share the same tone stack and master behavior; the variant switch remains model-only.
- Amp 2 `Pre Gain` is intentionally remapped to `-20 dB / -5 dB / +10 dB` at min / noon / max.
- Amp 2 `Master` now rises mostly as level first, then adds saturation/compression in the upper range instead of behaving as pure post-volume.

### 8) Dev diagnostics overlay is now landed on `main`
Outcome:
- A dev-only diagnostics readout can now be shown in both standalone and plugin builds.

Important landed details:
- Controlled by `NAM_DEV_DIAGNOSTICS` in `NeuralAmpModeler/config.h`.
- Standalone shows sample rate, block size, buffer latency estimate, DSP latency, DSP load, process CPU `current/average/peak`, and RAM.
- Plugin builds intentionally omit CPU/RAM and show only DSP/buffer/latency stats, because plugin CPU/RAM would mostly reflect the host DAW process.

### 9) A2 support and iPlug2 upstream updates are now part of the current baseline
Outcome:
- `main` now includes the A2 dependency update plus the later iPlug2 upstream integration work that followed on top of it.

Important landed details:
- The A2-related dependency baseline is represented by `9311b5b` (`Update dependencies for A2 NAM support`).
- The current baseline also includes the later iPlug2 fixes from `iplug2-upstream-update`.
- Those follow-up commits cover upstream branch refresh, RtAudio validation, popup menu compatibility, UTF-8 helper linkage for app/VST3, and mono input selection behavior.
- The current local `main` branch is the preferred starting baseline for further work.

### 10) Build-time A2 slimmable-size control is now landed on `main`
Outcome:
- The plugin now applies a build-time slim value to every loaded NAM model that implements `nam::SlimmableModel`.

Important landed details:
- The central application point is `LoadNAMDSPForPath()` in `NeuralAmpModeler/NeuralAmpModeler.cpp`.
- That path covers embedded and external model loads before `ResamplingNAM` wrapping, so the setting reaches staged amp models, right-channel companion models, and stomp models.
- `NeuralAmpModeler/config.h` now exposes an override-friendly `NAM_SLIMMABLE_SIZE` macro plus typed `NAMConfig::SlimmableSize`.
- Visual Studio preprocessor override usage is `NAM_SLIMMABLE_SIZE=0.5` in project properties; do not prepend `/D` in the property field.
- The runtime call clamps the configured value to `0.0 .. 1.0` before calling `SetSlimmableSize()`.

## Important current conclusions
- Do not reopen tuner work unless the user explicitly asks.
- Do not restart the old amp/IR artifact investigation from scratch; `main` already contains the accepted baseline.
- Keep custom IR support in release mode.
- Use the slot presentation / behavior / resolved-spec scaffolding for future amp-specific divergence.
- User now builds manually after patches; do not run builds unless the user explicitly asks.
- That build rule is now also recorded directly in `AGENTS.md`, not only in this handoff file.
- The earlier stereo-collapse-through-cab concern appears to have been addressed by `d41b00c` (`Preserve stereo through cab IR processing`); do not reopen that investigation unless the user reports a current regression.
- The modeled boost pedal baseline is now the accepted path on `main`; do not route the live stomp switch back through boost NAM slots unless the user explicitly asks.
- Prefer small, reviewable follow-ups from the current stable `main` baseline.
- Internal `NeuralAmpModeler` naming cleanup can wait.
- If future work needs A2 CPU tuning, start from the landed `NAM_SLIMMABLE_SIZE` hook instead of adding a second model-load path.

## Build/config baseline to preserve
- Audio/performance validation: `Release | x64` only
- Standalone audio test: run without debugger (`Ctrl+F5`)
- Do not evaluate DSP performance in Debug
- The user now builds manually after each patch unless they explicitly ask otherwise
- The user manually built and verified the A2 slim-setting change before pushing it
- Do not assume local `config.h` matches committed defaults during user testing
- Local `NeuralAmpModeler/config.h` churn is expected during the dev phase, especially for toggles like `NAM_STARTUP_TMPLOAD_DEFAULTS`, `NAM_DEV_DIAGNOSTICS`, `NAM_RELEASE_MODE`, and `NAM_RELEASE_IGNORE_PRESET_MODEL_PATHS`; do not treat that dirtiness alone as a blocker

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
1. Continue from the current stable `main` baseline and prefer small, reviewable follow-ups in the area the user explicitly requests next.
2. If stereo behavior is questioned again, treat it as regression verification against `d41b00c` rather than an open unresolved architecture problem.
3. The next small feature the user wants to tackle is normalizing reverb output for better UX.

## Starter prompt for the next agent
You are continuing work in `D:\\Dev\\NAMPlugin` on branch `main`.

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

Current baseline:
1. `main` already includes the merged amp/IR switching artifact reduction work from `reduce-amp-switch-artifacts`
2. External-facing rebrand to `RE-AMP` is landed
3. Amp bypass routing/state handling fixes are landed
4. Curated cab mic set now includes `S-57`, `R-121`, and `M-421`
5. Metering, gate/stomp decoupling, preset/session restore fixes, Cab V1, compressor stomp v1, amp variants v1, doubler improvements, startup/default asset refresh, and slot-specific amp UI work are already landed
6. Do not reopen tuner work unless the user explicitly asks
7. Release mode embeds curated cab IRs and amp variant assets while still allowing custom IR loading
8. Amp 2 custom tuning is landed on `main`: custom tone stack voicing, slot-specific `Pre Gain` taper, and a saturating `Master` behavior
9. Dev diagnostics overlay is landed: standalone shows CPU/RAM plus DSP stats, plugin builds intentionally show only DSP/buffer/latency stats
10. The A2 dependency update baseline is landed on `main`, and local `main` also includes the iPlug2 upstream follow-up commits through `9ae005c`
11. Build-time A2 slimmable-size control is landed on `main` via `NAM_SLIMMABLE_SIZE` / `NAMConfig::SlimmableSize`, applied in `LoadNAMDSPForPath()`
12. The user now builds manually after patches; do not run builds unless explicitly asked
13. That no-build-without-user-prompt rule is now written directly in `AGENTS.md`
14. The live boost pedal is now a built-in modeled dual-voice pedal on `main`:
   - Boost mode switch compares `TS` and `PD`
   - User-facing controls are `Boost Drive`, `Boost Character`, and `Output Volume`
   - Those controls display `0..10` with `5` at noon and no unit text
15. The old boost NAM slot plumbing still exists in code/settings for possible future development use, but it is not the live boost signal path now

Suggested next task unless the user redirects:
1. Start from the current `main` baseline and follow the user's next requested task
2. If stereo behavior is reported again, verify against the `d41b00c` cab/IR stereo-preservation changes first
3. The next small feature request is normalizing reverb output for better UX
4. Keep the patch small and reviewable from the current stable `main` baseline
