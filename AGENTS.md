# AGENTS.md — Instructions for coding agents (Codex) in this repo

These rules apply to any AI agent operating in this repository.
If anything conflicts with real-time audio safety or build stability, prioritize safety and ask.

Also read and follow: SKILLS.md (working modes, review process, communication style).

## Project context
- Fork of NeuralAmpModelerPlugin.
- Primary dev platform: Windows + Visual Studio 2022.
- Primary targets: Standalone app + VST3 (AAX is out-of-scope unless explicitly requested).

## Build & run (Windows)
- Audio/performance validation: **Release | x64** only.
- Run standalone for audio testing: **Start Without Debugging** (Ctrl+F5 in Visual Studio).
- Debug builds / running under debugger can severely degrade real-time DSP; do not judge DSP performance in Debug.

## Non-negotiable real-time audio rules (audio thread)
For `ProcessBlock()` and anything it calls directly/indirectly:
- NO heap allocation (no `new`, `malloc`, `std::vector` growth, `std::string` building, `std::function` allocations, etc.).
- NO locks, mutexes, waits, sleeps.
- NO file I/O, network I/O, printing/logging, UI calls.
- NO exceptions thrown across the callback; avoid throwing in audio paths entirely.
- Prefer deterministic O(n) loops and preallocated buffers.
- Heavy work (model/IR loading, parsing, filesystem) must run off the audio thread and be swapped in safely.

If unsure whether code runs on the audio thread, assume it does and keep it real-time safe.

## Change policy
- Prefer small, reviewable diffs (one feature/bugfix per patch).
- Keep style consistent with surrounding code.
- Do not reformat unrelated code or perform sweeping refactors unless asked.
- Ask before: adding dependencies, changing preset/serialization, changing default audio/device behavior, large multi-file refactors.

## Required agent workflow (always)
When asked to implement or change code:
1) **Plan first**: list the minimal files/symbols you will touch and why.
2) **Implement as a minimal patch**: smallest diff that achieves the goal.
3) **Real-time safety note**: explicitly confirm audio-thread safety constraints were respected.
4) **Verification steps**: exact steps to build/run and what to observe.

### Patch footer checklist (include at end of your response)
- [ ] Release|x64 builds
- [ ] No allocations/locks/I/O/logging in audio thread paths
- [ ] UI changes minimal and consistent
- [ ] How to verify audibly / behaviorally

## Parameter + UI conventions
- New DSP control should:
  - Add param ID, init name/unit/range, apply at a clearly defined stage in the signal chain.
  - Add UI control using existing control classes/patterns (e.g., NAMKnobControl).
  - Keep UI layout indices decoupled from param enum ordering.
  - Consider smoothing for continuous params to avoid zipper noise.
