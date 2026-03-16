Last updated: 2026-03-16

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
- Stomp-section bypass no longer disables the gate

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
- Bypass transition fixed to avoid the brief wrong-image collapse when switching off

Current limitation:
- This is a strong baseline, not a final polished production doubler
- More tuning is possible, but not required right now

### Done: Milestone H - proper stereo/mono input and output metering
Outcome:
- Metering now clearly reflects the effective mono/stereo behavior of the current path

Landed work:
- Stereo-aware input metering
- Stereo-aware output metering
- Clean vector meter redraw instead of the old striped/bitmap-heavy look
- Stereo lane layout fix
- Latched red clip warning with click-to-clear
- Shared clear behavior across both meters
- Faster decay / peak timing

Current limitation:
- This is intentionally minimal and stable; avoid reopening it without a clear user request

### Done: Preset/session restore stabilization follow-up
Outcome:
- Preset context now survives restart/session reopen much more clearly

Landed work:
- Standalone last preset name + dirty state restore
- Plugin session preset context restore
- `Input Stereo` removed from preset-owned behavior
- Plugin stomp NAM / cab IR restore fixes

## Active milestones

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

## Low-priority polish lane
- Continue amp-face visual polish only if the user explicitly wants it
- Keep those changes UI-only and isolated from DSP/state work where possible

Examples:
- per-slot art tweaks
- hover-state tuning
- off-state visual language
- section dimming tweaks

## Recommended execution order from now
1. Milestone F: transpose decision path
2. Resume Milestone B when final Release assets and hardware-switch behavior are clearer
3. Milestone C after the curated cab direction is better defined
4. Milestone D only after cab v1 proves stable
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
1. Metering, gate/stomp decoupling, preset/session restore fixes, and recent amp UI polish are already landed
2. Do not reopen the metering or plugin preset-restore paths unless the user asks
3. Prefer small, reviewable follow-ups from the current stable baseline

Suggested next task unless the user redirects:
1. Inspect the transpose feature path and current tradeoffs
2. Propose whether it should stay, be simplified, or be removed
3. Implement the smallest reviewable RT-safe patch only if the user wants code

Constraints:
- Keep diffs minimal
- Keep audio thread RT-safe
- Do not run builds/tests unless explicitly requested
- Dev mode: breaking changes are allowed when they improve architecture and iteration speed
