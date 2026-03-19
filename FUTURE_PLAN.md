Last updated: 2026-03-19

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

Landed work:
- Fixed amp/stomp/IR asset manifest scaffold
- Controlled asset resolution from known `tmpLoad` locations
- Release-mode startup/default/preset restore applies those fixed assets

### Done: Milestone E - gate UX reshape
Outcome:
- Gate now behaves as a one-knob macro instead of a raw threshold-only feel

### Done: Milestone G - doubler v1
Outcome:
- Doubler works as a double-track preview rather than a simple widener

Current limitation:
- Strong baseline, not a final polished production doubler

### Done: Milestone H - proper stereo/mono input and output metering
Outcome:
- Metering now clearly reflects the effective mono/stereo behavior of the current path

### Done: Preset/session restore stabilization follow-up
Outcome:
- Preset context now survives restart/session reopen much more clearly

### Done: Milestone C - Cab system v1
Outcome:
- Interactive dual-cab workflow is now the main cab architecture

Landed work:
- Two independent cab slots:
  - `Cab A`
  - `Cab B`
- Per-slot controls:
  - enable
  - source
  - position
  - level
  - pan
  - custom IR loader
- Curated mic interpolation v1:
  - 1D cone-position interpolation
  - five captures per mic
  - current mic set:
    - `57`
    - `121`
- Left-slot slider direction mirrors correctly relative to the speaker art
- Old single-cab blend workflow removed
- Cab page UI redesign landed

Current limitation:
- This is a stable v1, not the end-state cab system
- No distance axis yet
- No more advanced dual-cab routing or speaker-selection logic yet

### Done: Release-mode curated cab embedding v1
Outcome:
- Curated cab IRs for the current mic set are embedded in release builds

Landed work:
- Generated embedded PCM asset path for curated cab IRs
- Release-mode cab staging can load curated IRs from compiled data
- Visual Studio projects include the generated asset source
- `Custom IR` loading remains available in release mode

Current limitation:
- This only covers the curated cab mic set
- Amp/stomp final embedded packaging strategy is still not finished

## Active milestones

### Milestone I: Compressor stomp pedal v1
Goal:
- Add a built-in compressor stomp pedal with a very small, musical control set

Proposed v1 scope:
- controls:
  - `Amount`
  - `Level`
  - `Soft/Hard` switch
- likely implementation:
  - built-in DSP compressor, not a NAM model
  - soft/hard maps to different compression character via ratio/knee and optionally timing presets

Recommended design bias:
- Keep it simple and reviewable
- Use a fixed internal compressor design with just a few exposed musical controls
- Prefer predictable preallocated DSP state over feature breadth

Likely signal-chain placement:
- stomp/preamp side of the chain
- before the amp model
- avoid larger architecture churn unless the user explicitly wants it

Risk:
- Medium

RT safety watch-outs:
- No allocations or dynamic graph changes in the callback
- Smooth control changes where needed
- Keep detector/state memory preallocated

### Milestone F: Transpose decision path
Goal:
- Decide whether transpose should stay, be simplified, or be removed based on latency and quality tradeoffs

Current state:
- Feature is hidden
- Idle off-path CPU issue was already reduced
- App-side “transpose seems on” behavior appears to have been old standalone-state restore rather than a changed default

Risk:
- Medium

### Milestone B: Release asset packaging and final variant strategy
Goal:
- Finish the remaining release-mode packaging strategy beyond the current curated cab embedding

Current state:
- Curated cab IR embedding exists
- Final amp/stomp embedded asset strategy is still incomplete
- Final model list is not known yet
- Hardware-switch mapping for amp variants is not finalized yet

What can wait:
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
1. Milestone I: compressor stomp pedal v1
2. Milestone F: transpose decision path only if the user returns to it
3. Resume Milestone B when final release asset set and hardware-switch behavior are clearer
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
1. Metering, gate/stomp decoupling, preset/session restore fixes, and Interactive Cab V1 are already landed
2. Do not reopen the metering or plugin preset-restore paths unless the user asks
3. Release mode now embeds curated cab IRs but still allows custom IR loading
4. Prefer small, reviewable follow-ups from the current stable baseline

Suggested next task unless the user redirects:
1. Inspect how to add a built-in compressor stomp pedal with minimal architecture churn
2. Propose the smallest RT-safe insertion point and control set
3. If the user wants code, implement only the smallest reviewable first slice

Compressor idea under consideration:
- controls:
  - `Amount`
  - `Level`
  - `Soft/Hard`
- likely behavior:
  - built-in compressor DSP
  - soft/hard maps to ratio/knee and possibly timing presets

Constraints:
- Keep diffs minimal
- Keep audio thread RT-safe
- Do not run builds/tests unless explicitly requested
- Dev mode: breaking changes are allowed when they improve architecture and iteration speed
