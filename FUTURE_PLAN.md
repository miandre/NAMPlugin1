# FUTURE_PLAN.md

Last updated: 2026-03-06

Purpose: active roadmap for remaining work.  
This file is mandatory onboarding context for every new agent.

## Development mode assumptions
- Project is in active development with a single tester.
- Breaking changes are acceptable when they materially improve architecture/iteration speed.
- Backward compatibility for old presets is not a hard requirement right now.

## Completed milestones (remove from active backlog)

### Done: Tempo/transport + synced delay foundation
- Host/manual tempo handling implemented.
- Standalone manual tempo workflow implemented.
- Delay time sync mode (note divisions) implemented.

### Done: Delay upgrades
- Ping-pong behavior implemented and corrected for mono/stereo input behavior.
- Delay ducker implemented with extended usable range.
- Delay digital readout implemented and polished.

### Done: EQ/FX UI transition work
- EQ moved to dedicated page.
- FX/EQ navigation decoupled/fixed.
- FX/EQ graphics and control layout polish completed.

## Active milestones (not completed)

### Milestone A: Amp slot architecture completion (current branch focus)
Goal:
- Finish Rig/Release split so Release slots are deterministic and resilient to preset/path drift.

Current state:
- Foundation commit is in place (`439d946`).
- Additional uncommitted WIP exists for fixed-slot path resolution and unserialization behavior.

Remaining tasks:
1. Review and commit current WIP as one minimal patch.
2. Verify preset restore/slot switching behavior in both modes.
3. Decide whether Release mode should allow per-slot optional editable exceptions.
4. Add lightweight tests/checklist steps for slot-lock invariants.

Risk:
- Medium. Mostly state/serialization behavior risk, low DSP risk.

RT safety watch-outs:
- Keep all slot/model loading and filesystem work off audio thread.

### Milestone B: Model bundling and release-packaging strategy (new)
Goal:
- Support fixed bundled models without exposing plain `.nam` files in release workflow.

Proposed direction:
1. Add slot source abstraction:
   - `ExternalPath` (Rig mode),
   - `EmbeddedModelId` (Release mode).
2. Add build-time packaging step:
   - pack models into one manifest + payload bundle,
   - optional compression/encryption.
3. Add background-thread model materialization path:
   - preferred: load model directly from memory payload,
   - fallback: controlled cache materialization if loader requires file path.
4. Keep runtime per-slot DSP cache (already implemented) for fast switching.

Copy-protection reality:
- Client-side protection can be strengthened but not made absolute.
- Bundling/obfuscation is not enough alone.
- Stronger practical stack: license-bound encrypted payloads + in-memory decode + watermarking/legal controls.

Risk:
- Medium-high (pipeline/tooling + loader integration).

RT safety watch-outs:
- No decrypt/decompress/file I/O on audio thread.

### Milestone C: Cab system v1 (1D interpolation)
Goal:
- Introduce musically useful mic position interpolation with manageable complexity.

Scope:
- 1D cone-position interpolation first.
- Curated bundled IR set support.
- Keep external IR fallback in Rig mode.

Risk:
- High CPU/complexity if not phased.

RT safety watch-outs:
- Precompute interpolation metadata; no dynamic allocations in callback.

### Milestone D: Cab system v2 (2D distance + advanced routing)
Goal:
- Add distance axis and optional dual-cab pan/level workflows.

Risk:
- High (state/UI/DSP complexity).

RT safety watch-outs:
- Keep interpolation and routing deterministic and allocation-free in callback.

### Milestone E: Gate UX reshape (one-knob behavior)
Goal:
- Threshold knob also drives hold/release contour in a predictable musical curve.

Risk:
- Medium (tuning-heavy).

RT safety watch-outs:
- Smoothing/curve evaluation must remain lightweight.

### Milestone F: Transpose decision path
Goal:
- Decide whether to keep, simplify, or remove transpose based on latency/quality tradeoff.

Risk:
- Medium (user-facing behavior change likely).

### Milestone G: Doubler
Goal:
- Add audition-oriented doubler (micro pitch/time modulation + width).

Risk:
- Medium.

RT safety watch-outs:
- Bounded modulation work and preallocated buffers only.

## Recommended execution order from now
1. Milestone A: finish amp-slot architecture WIP and commit.
2. Milestone B: implement minimal model-bundle abstraction scaffold.
3. Milestone C: cab interpolation v1.
4. Milestones E/F/G in parallel as smaller feature tracks.
5. Milestone D after v1 proves stable.

## Next-agent prompt (copy/paste)
You are continuing work in `D:\Dev\NAMPlugin` on branch `amp-slot-architecture`.

Read first, in this exact order:
1) `AGENTS.md`
2) `SKILLS.md`
3) `AGENT_SESSION_SUMMARY.md`
4) `FUTURE_PLAN.md`

Then confirm repo/submodule state:
- `git status --short`
- `git branch --show-current`
- `git rev-parse --short HEAD`
- `git submodule status iPlug2`
- `git -C iPlug2 remote -v`

Task:
1) Finalize and commit current uncommitted Milestone-A (amp-slot architecture) WIP.
2) Propose the smallest first Milestone-B patch for bundled-model support (abstraction only, no full packer yet).
3) List RT-safety checks and manual validation steps before merge.

Constraints:
- Keep diffs minimal.
- Keep audio thread RT-safe.
- Do not run builds/tests unless explicitly requested.
- Dev mode: breaking changes are allowed when they improve architecture/iteration speed.
