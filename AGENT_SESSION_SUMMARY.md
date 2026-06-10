Last updated: 2026-06-10

Purpose: concise handoff so a new agent can continue from the current stable baseline without replaying the full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md`
4. `FUTURE_PLAN.md`

## Current repository state at handoff
- Active branch after merge: `main`
- Tuner 2.0 work was developed on `feature/tuner-2.0-baseline` and fast-forward merged into `main`
- Latest tuner commits included in `main`:
  - `6b12fde` `Establish tuner 2.0 baseline`
  - `7bc1a96` `Fix tuner bypass monitor gain`
- Working tree should be clean after this handover commit
- iPlug2 submodule state at handoff check:
  - `c4a2c31be1a2b5b7d932fd50ea268b0241e26836 iPlug2 (v1.0.0-beta-696-gc4a2c31be)`
  - `origin` = `https://github.com/miandre/iPlug2.git`
  - `upstream` = `https://github.com/iPlug2/iPlug2.git`
- Current product name for user-facing outputs: `RE-AMP`
- Internal code/project naming is still mostly `NeuralAmpModeler`; do not rename internally unless explicitly requested

## User workflow constraints
- The user builds manually from Visual Studio.
- Do not run builds unless explicitly asked.
- Audio/performance validation is `Release | x64` only.
- Standalone audio testing should be run without debugger (`Ctrl+F5`).
- Debug performance is not meaningful for DSP judgement.
- Local `NeuralAmpModeler/config.h` churn is expected during development; do not treat dev-toggle dirtiness alone as a blocker.

## Tuner 2.0 baseline now landed on main
Outcome:
- Tuner pitch tracking is substantially improved compared with the first tuner iteration.
- Standard 7-string guitar tuning now works well across `B E A D G B E`.
- Alternate guitar tunings tested by the user, including C tuning, work well.
- 5-string bass low B and C-tuned bass low C are now handled after the larger analysis-frame change.
- Tuner BYP monitor volume no longer depends on which amp model was active when opening the tuner.

Important landed tuner details:
- `TunerAnalyzer::kAnalysisSize` is now `8192`.
- The tuner uses a cleaner MPM/NSDF-style detector with note confirmation, display target smoothing, and attack suppression.
- The larger `8192` frame fixed practical bass-low stability without unacceptable CPU cost or noticeable guitar lag in user testing.
- Tuner diagnostics were added under `NAM_DEV_DIAGNOSTICS`:
  - diagnostics line includes `Tun raw ... ph ... out ...`
  - `ph *` means phase estimate was applied
  - `ph ~` means phase was estimated but not trusted/applied
  - `ph x` means phase was attempted but rejected
  - `q` is the phase purity score
- Dev diagnostics overlay also includes a build marker (`#NN`) generated from `__DATE__`/`__TIME__` so the user can verify they are testing a new build.
- `NeuralAmpModeler/config.h` now has `#pragma once` so tuner diagnostics macros are available consistently when `TunerAnalyzer.h` is included.

Important tuner conclusions from testing:
- The original high-string center jump was caused by octave-down detection: e.g. B3 was internally read as MIDI 47 instead of 59.
- The octave-promotion rule was tightened so normal guitar A2 and above do not collapse to lower-octave candidates.
- The explicit bass-low fallback scan was removed after the `8192` frame made it redundant and riskier than useful.
- General low-note attack suppression remains; the A/D-only attack tweak was reverted after the true issue was found to be octave detection.
- The user reported the final baseline works well on guitar and bass.

Tuner BYP monitor fix:
- The old BYP issue was confirmed: tuner BYP output volume depended on the amp model active when opening the tuner.
- Cause: tuner BYP used `_ProcessOutput()`, which used `mOutputGain` including active-model loudness/calibration compensation.
- Fix: tuner BYP now calls `_ProcessOutputWithTargetGain(..., _GetOutputGainForModel(nullptr))`.
- Result: BYP monitor uses the clean post-input-gain signal plus user `Output` level only, not model loudness/calibration compensation.

## Other current baseline items still apply
- Amp/IR switching artifact reduction is merged.
- External-facing rebrand to `RE-AMP` is landed.
- Amp bypass routing/state handling fixes are landed.
- Curated cab mic set includes `S-57`, `R-121`, and `M-421`.
- Release mode embeds curated cab IRs and amp variant assets while still allowing custom IR loading.
- Metering, gate/stomp decoupling, preset/session restore fixes, Cab V1, compressor stomp v1, amp variants v1, doubler improvements, startup/default asset refresh, and slot-specific amp UI work are already landed.
- Amp 2 custom tuning is landed: custom tone stack voicing, slot-specific `Pre Gain` taper, and saturating `Master` behavior.
- Dev diagnostics overlay is landed: standalone shows CPU/RAM plus DSP stats; plugin builds intentionally show only DSP/buffer/latency stats.
- A2 dependency update and iPlug2 upstream follow-up baseline remain landed.
- Build-time A2 slimmable-size control remains landed via `NAM_SLIMMABLE_SIZE` / `NAMConfig::SlimmableSize`, applied in `LoadNAMDSPForPath()`.
- The live boost pedal is a built-in modeled dual-voice pedal; the old boost NAM slot plumbing still exists for possible future development but is not the live boost signal path.
- Reverb post-mix output compensation is landed in `_ProcessFXReverbStage()` with:
  - `postMixCompStart = 0.49`
  - `postMixCompMaxGain = 2.4`
  - `postMixCompCurve = pow(postMixCompNorm, 1.40)`

## Current policy notes
- Real-time audio rules still apply in `ProcessBlock()` and anything it calls:
  - no heap allocation
  - no locks/waits/sleeps
  - no file/network/UI/logging
  - no exceptions across the callback
  - deterministic preallocated processing only
- Keep diffs small and reviewable.
- Do not revert user changes unless explicitly requested.
- Prefer targeted patches over broad refactors.

## Suggested next task
The user requested the next task be a UI overview of the Tuner, consisting of two parts:
1. Fine-tune tuner smoothing for better UX.
2. Make a small tuner graphics update for better usability.

Suggested approach:
- Start from the current `main` baseline.
- Treat current tuner detection as the accepted baseline; do not rewrite detector logic unless new evidence appears.
- First evaluate UX smoothing by eye/feel on guitar and bass before changing constants.
- Keep graphics changes small and usability-driven; preserve the current overall tuner look unless the user asks for a larger redesign.
- Keep any tuner UI changes isolated from DSP where possible.

## Starter prompt for the next agent
You are continuing work in `D:\Dev\NAMPlugin` on branch `main`.

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
1. `main` includes the Tuner 2.0 baseline and tuner BYP monitor gain fix.
2. Tuner detection now uses an `8192` analysis frame and works well for tested guitar and 5-string bass low B.
3. Tuner BYP monitor output is independent of active amp model loudness/calibration compensation.
4. External-facing product identity is `RE-AMP`; internal `NeuralAmpModeler` naming remains intentional.
5. User builds manually after patches; do not run builds unless explicitly asked.
6. Keep patches small and audio-thread safe.

Suggested next task unless the user redirects:
1. UI overview of the Tuner.
2. Part one: fine-tune smoothing for better UX.
3. Part two: small tuner graphics update for better usability.
