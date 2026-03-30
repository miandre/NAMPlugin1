Last updated: 2026-03-30

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

## Important current conclusions
- Do not reopen tuner work unless the user explicitly asks.
- Do not restart the old amp/IR artifact investigation from scratch; `main` already contains the accepted baseline.
- Keep custom IR support in release mode.
- Use the slot presentation / behavior / resolved-spec scaffolding for future amp-specific divergence.
- Prefer small, reviewable follow-ups from the current stable `main` baseline.
- Internal `NeuralAmpModeler` naming cleanup can wait.

## Build/config baseline to preserve
- Audio/performance validation: `Release | x64` only
- Standalone audio test: run without debugger (`Ctrl+F5`)
- Do not evaluate DSP performance in Debug
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
1. Start the next feature as a focused custom tone stack task for one amp slot.
2. First, discuss specs with user before coding. 
3. Then, map the current tone-stack creation path and slot-specific amp scaffolding before changing DSP.
4Keep the patch narrow:
  - add or adapt one tone-stack implementation
  - wire it through the existing slot behavior/spec flow
  - avoid broad UI or serialization churn unless the user asks
5. Validate in `Release | x64` and listen specifically for control mapping, bypass behavior, and any new zipper/click artifacts.

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

Suggested next task unless the user redirects:
1. Explore a custom tone stack for one amp slot
2. Use the existing slot presentation / behavior / resolved-spec scaffolding instead of adding a parallel one-off path
3. Keep the patch small and reviewable from the current stable `main` baseline
