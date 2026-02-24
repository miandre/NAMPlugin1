# AGENT_SESSION_SUMMARY.md

Last updated: 2026-02-24

Purpose: concise handoff for new agents so work can continue without replaying the full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md` (this file)

## Current repository state
- Branch: `main`
- Working tree: clean at handoff time
- Remote: `origin` = `https://github.com/miandre/NAMPlugin1.git`

## High-level trajectory completed so far
- Tuner:
  - Detection quality significantly improved over multiple iterations.
  - UI redesigned to a clearer, amp-sim style display with mode controls.
  - Tuner logic split out from monolithic plugin code into dedicated structure/modules (done earlier in history).
- UI framework:
  - Top navigation/icons moved to SVG for cleaner scaling.
  - Multi-page section model implemented (`Stomp`, `Amp`, `Cab`, `FX`, `Settings`) with section-specific backgrounds.
  - Header/footer/side controls refactored and aligned through several layout passes.
- Background/rendering:
  - High-DPI background handling improved (including resize behavior).
  - Multiple resolution assets integrated.
- Transpose:
  - Rubber Band based transpose implemented and tuned.
  - Click-free bypass/crossfade behavior added around zero-semitone transitions.
- Stomp section v1 (latest merged feature):
  - Stomp UI controls added (custom pedal knobs, stomp buttons, LEDs).
  - Gate pedal and boost pedal scaffolding connected.
  - Stomp model browser added (for pre-amp boost model loading).
  - Stomp DSP path integrated into processing chain.

## Most relevant recent commits (already merged to main)
- `f8143d3` Add stomp section v1 controls, DSP wiring, and assets
- `82bb58d` Merge branch `feature/transpose-rubberband`
- `4ea5a4c` Transpose: Rubber Band backend + click-free bypass
- `836f667` UI: section-specific backgrounds and visibility
- `5f8452c` UI: background/layout proportion refresh
- `490e99c` UI: SVG migration and top/tuner interactions
- `dc74f29` Hi-DPI background scaling on resize
- `d3fe503` Tuner analyzer/top-nav/monitor modes

## Stomp v1 specifics
### UI assets introduced
- `NeuralAmpModeler/resources/img/PedalKnob.png`
- `NeuralAmpModeler/resources/img/PedalKnobShadow.png`
- `NeuralAmpModeler/resources/img/StompButtonUp.png`
- `NeuralAmpModeler/resources/img/StompButtonDown.png`
- `NeuralAmpModeler/resources/img/GreenLedOn.png`
- `NeuralAmpModeler/resources/img/GreenLedOff.png`
- `NeuralAmpModeler/resources/img/RedLedOn.png`
- `NeuralAmpModeler/resources/img/RedLedOff.png`

### Stomp behavior implemented
- Gate pedal:
  - Threshold + release controls
  - On/off switch
  - Red on/off LED + green active/attenuating LED behavior
- Boost pedal:
  - Level control
  - On/off switch
  - Red on/off LED
  - Optional stomp NAM model loaded and processed before amp model path
- Stomp model file picker present in stomp view.

### Notable current gate wiring
- Gate release parameter is wired in DSP path.
- User requested and accepted gate release knob range update to `5..1000 ms`.
- Additional gate internals exist in DSP (`time`, `ratio`, `openTime`, `holdTime`, `closeTime`), but only selected ones are exposed in UI.

## Known conventions established in this project work
- Keep parameter/UI wiring minimal and incremental.
- Prefer SVG for icons where practical.
- For audio-thread safety: no allocations/locks/I/O in `ProcessBlock()` path.
- Heavy tasks (model/IR loading) staged off audio thread and swapped safely.
- Validate DSP behavior in `Release|x64` + standalone without debugger.

## Open direction / likely next steps
- Continue Stomp section refinement:
  - finalize graphics/positions for controls
  - tune gate behavior and exposed controls as needed
  - expand boost pedal UX and model-management polish
- Add preset workflow enhancements where needed.
- Continue per-section feature expansion for `Cab` and `FX`.

## If behavior seems different than expected
- First check which branch is actually running and whether latest `main` was fetched.
- Confirm section visibility logic (top-nav state) before assuming DSP failure.
- Confirm Release build and test outside debugger before judging latency/CPU/audio feel.
