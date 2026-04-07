Last updated: 2026-04-07

Purpose: concise handoff so a new agent can continue from the current stable baseline without replaying the full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md`
4. `FUTURE_PLAN.md`

## Current repository state
- Active branch: `main`
- Working tree at handoff: expected clean after this handoff update is committed and pushed
- Current product name for user-facing outputs: `RE-AMP`
- Internal code/project naming is still mostly `NeuralAmpModeler` and that is intentional for now
- Current curated cab mic set:
  - `S-57`
  - `R-121`
  - `M-421`
- Release mode still embeds curated cab IRs and amp variant assets while allowing user-loaded custom IRs

## Recent relevant history before this handoff update
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

### 5) The broader feature baseline from earlier work still applies
Already landed on `main`:
- Metering overhaul
- Gate/stomp decoupling
- Preset/session restore fixes
- Plugin preset stomp NAM / cab IR restore fixes
- Interactive Cab V1
- Embedded curated cab IRs for release mode
- Compressor stomp pedal v1
- Boost v2 with `A/B` model selection
- Amp model variants v1
- Doubler improvement pass
- Startup/default asset refresh
- Amp-face scaffolding and slot-specific amp UI work
- Tuner improvements and tuner UI polish

### 6) Amp 2 tuning work is now landed on `main`
Outcome:
- Amp 2 no longer uses the old generic shared voicing as-is.
- Amp 2 now has its own tuned post-model tone stack, slot-specific pre-gain taper, and a saturating master behavior.

Important landed details:
- Amp 2 keeps the same visible control set: `Pre Gain`, `Bass`, `Mid`, `Treble`, `Presence`, `Depth`, `Master`, plus `DEPTH` and `SCOOP`.
- Amp 2 `A/B` variants still share the same tone stack and master behavior; the variant switch remains model-only.
- Amp 2 `Pre Gain` is intentionally remapped to `-20 dB / -5 dB / +10 dB` at min / noon / max.
- Amp 2 `Master` now rises mostly as level first, then adds saturation/compression in the upper range instead of behaving as pure post-volume.

### 7) Dev diagnostics overlay is now landed on `main`
Outcome:
- A dev-only diagnostics readout can now be shown in both standalone and plugin builds.

Important landed details:
- Controlled by `NAM_DEV_DIAGNOSTICS` in `NeuralAmpModeler/config.h`.
- Standalone shows sample rate, block size, buffer latency estimate, DSP latency, DSP load, process CPU `current/average/peak`, and RAM.
- Plugin builds intentionally omit CPU/RAM and show only DSP/buffer/latency stats, because plugin CPU/RAM would mostly reflect the host DAW process.

## Important current conclusions
- Do not reopen tuner work unless the user explicitly asks.
- Do not restart the old amp/IR artifact investigation from scratch; `main` already contains the accepted baseline.
- Keep custom IR support in release mode.
- Use the slot presentation / behavior / resolved-spec scaffolding for future amp-specific divergence.
- User now builds manually after patches; do not run builds unless the user explicitly asks.
- The next issue to investigate is stereo correctness through the cab/IR path: with true stereo input, the signal appears to collapse to mono somewhere before becoming stereo again.
- Prefer small, reviewable follow-ups from the current stable `main` baseline.
- Internal `NeuralAmpModeler` naming cleanup can wait.

## Build/config baseline to preserve
- Audio/performance validation: `Release | x64` only
- Standalone audio test: run without debugger (`Ctrl+F5`)
- Do not evaluate DSP performance in Debug
- The user now builds manually after each patch unless they explicitly ask otherwise
- No recent `Release | x64` build was run from this shell for the latest doc-only changes
- Do not assume local `config.h` matches committed defaults during user testing

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
1. Investigate the cab/IR stereo path before adding more features.
2. Map the true stereo signal path through amp processing, cab/IR processing, post-IR processing, and output routing to find exactly where stereo collapses to mono.
3. Fix it using the current stereo/mono routing scaffolding instead of adding a parallel path.
4. Keep the patch narrow:
   - preserve existing effective-mono behavior when the source is actually mono
   - avoid broad cab refactors unless the root cause forces it
   - do not regress the current amp/IR switching artifact work
5. Verify with true stereo material and listen for width preservation, routing correctness, and any new clicks or load regressions.

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
5. Metering, gate/stomp decoupling, preset/session restore fixes, Cab V1, compressor stomp v1, boost v2, amp variants v1, doubler improvements, startup/default asset refresh, and slot-specific amp UI work are already landed
6. Do not reopen tuner work unless the user explicitly asks
7. Release mode embeds curated cab IRs and amp variant assets while still allowing custom IR loading
8. Amp 2 custom tuning is landed on `main`: custom tone stack voicing, slot-specific `Pre Gain` taper, and a saturating `Master` behavior
9. Dev diagnostics overlay is landed: standalone shows CPU/RAM plus DSP stats, plugin builds intentionally show only DSP/buffer/latency stats
10. The user now builds manually after patches; do not run builds unless explicitly asked

Suggested next task unless the user redirects:
1. Investigate where the cab/IR path collapses true stereo to mono before widening again
2. Use the existing stereo/mono routing scaffolding and current cab architecture instead of adding a parallel path
3. Keep the patch small and reviewable from the current stable `main` baseline
