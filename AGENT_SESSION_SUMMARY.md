Last updated: 2026-06-12

Purpose: concise handoff so a new agent can continue from the current stable baseline without replaying the full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md`
4. `FUTURE_PLAN.md`

## Current repository state at handoff
- Active branch: `main`
- Current HEAD at this handoff: `5e7bc90` `Minor Doubler tweeks`
- Recent commits included in `main`:
  - `6b12fde` `Establish tuner 2.0 baseline`
  - `7bc1a96` `Fix tuner bypass monitor gain`
  - `8b918ee` `Polish tuner response feel`
  - `4670e51` `Improve tuner in-tune guidance`
  - `0bd95aa` `Update VS project`
  - `5e7bc90` `Minor Doubler tweeks`
- `origin/main` matched `5e7bc90` before these documentation edits
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

## Tuner 2.0 baseline and UI overview now landed on main
Outcome:
- Tuner pitch tracking is substantially improved compared with the first tuner iteration.
- Standard 7-string guitar tuning now works well across `B E A D G B E`.
- Alternate guitar tunings tested by the user, including C tuning, work well.
- 5-string bass low B and C-tuned bass low C are now handled after the larger analysis-frame change.
- Tuner BYP monitor volume no longer depends on which amp model was active when opening the tuner.
- Tuner response smoothing and the in-tune display were polished and visually approved by the user.

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

Tuner smoothing polish:
- Commit `8b918ee` shortened attack-settle holds and retuned target/velocity limits for a calmer but more responsive display.
- The analyzer remains the owner of tuner motion smoothing; `NAMTunerDisplayControl` consumes the final published cents value directly.
- Do not add a second display-side needle filter unless new testing demonstrates a concrete need.

Tuner graphics/usability pass:
- Commit `4670e51` updated `NAMTunerDisplayControl` in `NeuralAmpModeler/NeuralAmpModelerControls.h`.
- In-tune state enters at `+/-1.0` cent and exits beyond `+/-1.2` cents to provide small hysteresis.
- The old broad green segment and center tick were replaced with a narrow two-marker center gate.
- The note name, cents readout, and needle turn green together only in the accepted in-tune state.
- The baseline now uses mirrored red-to-amber-to-green gradients, with amber aligned around the `+/-25` cent ticks.
- The needle line/dot were enlarged for readability while the gate width remains independently fixed.
- The existing `MUTE` / `BYP` / `ON` workflow and panel layout were preserved.
- The user evaluated multiple visual iterations in the standalone UI and approved the final result.

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
- Visual Studio project files were refreshed in `0bd95aa`; do not undo that project-file state as unrelated churn.
- The latest doubler tuning in `5e7bc90` changed:
  - `kSpreadMaxMs` from `14.0` to `18.0`
  - `kRightTrimMaxDB` from `-3.9` to `-2.9`
  - `kKnobMinSpreadMs` from `6.0` to `10.0`
  - `kKnobMinRightTrimDB` from `-1.2` to `-0.2`

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
The tuner UI overview is complete. A newly recorded future design candidate is an Amp 1 tone-stack and control pass.

Before implementing Amp 1 changes:
- Discuss the intended amp character, desired control set, and which current controls should stay, move, or be repurposed.
- Amp 1 currently uses `ToneStackKind::BasicNam`, `MasterBehaviorKind::Standard`, the common `Pre Gain / Bass / Mid / Treble / Presence / Depth / Master` controls, and an A/B `CHARACTER` model switch.
- Use the existing slot-specific presentation/behavior architecture and Amp 2 custom tone-stack pattern rather than adding Amp 1 special cases in the audio callback.
- Decide whether existing parameter IDs can be reused with slot-specific behavior. Ask before adding parameters or changing preset/state serialization.
- Any new tone-stack processing must remain preallocated, deterministic, and allocation/lock/I/O free in `ProcessBlock()`.

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
1. `main` includes the completed Tuner 2.0 detector, smoothing, BYP monitor fix, and in-tune graphics pass.
2. Tuner detection uses an `8192` analysis frame and works well for tested guitar and 5-string bass low B.
3. The tuner UI uses a `+/-1.0` cent entry threshold, `+/-1.2` cent exit hysteresis, a center gate, and mirrored tuning gradients.
4. Latest HEAD before handover docs is `5e7bc90`, including the latest minor doubler tuning.
5. External-facing product identity is `RE-AMP`; internal `NeuralAmpModeler` naming remains intentional.
6. User builds manually after patches; do not run builds unless explicitly asked.
7. Keep patches small and audio-thread safe.

Suggested next task unless the user redirects:
1. Discuss a possible Amp 1 tone-stack and control redesign.
2. Agree on the target voicing and control surface before changing code or serialization.
