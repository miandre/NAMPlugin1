Last updated: 2026-03-11

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

Landed work:
- Rig/Release workflow mode scaffold
- Slot edit lock policy and user-initiated edit gating
- Fixed-slot path handling and restore protection
- Slot model source abstraction:
  - `ExternalPath`
  - `EmbeddedModelId`
- Safer slot switching:
  - defer swap until target model is ready
  - de-click on actual swap
- Safer preset recall:
  - deterministic mute instead of hybrid old-model/new-parameter rendering

### Done: Release-mode fixed-asset scaffold v1
Outcome:
- `NAM_RELEASE_MODE=1` behaves like a first "release-feel" runtime path without full packaging yet

Landed work:
- Fixed amp/stomp/IR asset manifest scaffold
- Controlled asset resolution from known `tmpLoad` locations
- Release-mode startup/default/preset restore applies those fixed assets
- Release-mode stomp/IR browsing is disabled or ignored

Current limitation:
- This is still controlled-path loading, not true embedded payload packaging

### Done: Milestone E - gate UX reshape
Outcome:
- Gate now behaves as a one-knob macro instead of a raw threshold-only feel

Landed work:
- One-knob gate macro mapping for threshold/timing
- Top-row gate placement
- Gate mini-slider on/off control
- Gate attenuation indicator next to the top-row gate area
- Old stomp-page gate controls removed

### Done: Milestone G - doubler v1
Outcome:
- Doubler now works as a double-track preview rather than a simple widener

Landed work:
- Post-cab, pre-delay/reverb doubler stage
- Disabled automatically for the true stereo-core path
- Take-based left/right rendering model
- Loudness compensation across the amount range
- Dedicated top-row doubler knob and mini-slider
- Doubler defaults to off
- Current knob range remapped into the musically useful span

Current limitation:
- This is a strong baseline, not a final polished production doubler
- More tuning is possible, but not required right now

## Active milestones

### Milestone H: Proper stereo/mono input and output metering
Goal:
- Make input and output metering clearly reflect mono vs stereo operation

Desired outcome:
- Mono input mode should meter the effective mono path correctly
- Stereo input mode should meter both channels meaningfully
- Output metering should reflect the actual delivered output image/state

Why it matters:
- The plugin now has several mode-dependent stereo behaviors:
  - mono vs stereo input mode
  - cab stereo behavior
  - doubler availability and stereo spread
- Metering should reflect that clearly for debugging and UX

Suggested scope:
1. Inspect current input/output meter feed points
2. Define expected meter behavior for:
   - mono input
   - stereo input
   - mono-core + stereo post processing
3. Implement the smallest RT-safe improvement first

Risk:
- Low-medium

RT safety watch-outs:
- Meter taps must stay allocation-free and deterministic
- No UI work from the callback beyond the existing meter messaging pattern

### Milestone F: Transpose decision path
Goal:
- Decide whether transpose should stay, be simplified, or be removed based on latency and quality tradeoffs

Why it matters:
- It affects perceived responsiveness and product scope
- Better to decide before more feature work depends on it

Risk:
- Medium

### Milestone B: Release asset packaging and final variant strategy
Goal:
- Evolve the current Release-mode scaffold into the final fixed/bundled asset system

Current state:
- Runtime scaffold exists
- Packaging pipeline does not
- Final model list is not known yet
- Hardware-switch mapping for amp variants is not finalized yet

What can wait:
- true embedded payload format
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

### Milestone C: Cab system v1 (1D interpolation)
Goal:
- Introduce musically useful mic-position interpolation with manageable complexity

Scope:
- 1D cone-position interpolation first
- Curated bundled IR support
- External IR fallback can remain in Rig mode

Dependency note:
- Better started once the curated cab/IR content is clearer

Risk:
- High CPU/complexity if not phased carefully

RT safety watch-outs:
- Precompute interpolation metadata
- No dynamic allocation in the callback

### Milestone D: Cab system v2 (distance + advanced routing)
Goal:
- Add distance axis and optional dual-cab pan/level workflows

Risk:
- High. State/UI/DSP complexity

RT safety watch-outs:
- Keep interpolation and routing deterministic and allocation-free in the callback

## Recommended execution order from now
1. Milestone H: proper stereo/mono metering
2. Milestone F: transpose decision path
3. Resume Milestone B when final Release assets and hardware-switch behavior are clearer
4. Milestone C after the curated cab direction is better defined
5. Milestone D only after cab v1 proves stable

## Small feature suggestion
- Proper stereo/mono-aware metering for both input and output should remain near the top of the backlog
- This is useful both for UX and for debugging the now more mode-dependent stereo signal path

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

Task suggestion:
1. Inspect current input/output metering behavior
2. Propose a minimal mono/stereo-aware metering improvement
3. Implement the smallest reviewable RT-safe patch

Constraints:
- Keep diffs minimal
- Keep audio thread RT-safe
- Do not run builds/tests unless explicitly requested
- Dev mode: breaking changes are allowed when they improve architecture and iteration speed
