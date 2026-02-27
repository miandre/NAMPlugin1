# AGENT_SESSION_SUMMARY.md

Last updated: 2026-02-27

Purpose: concise handoff for new agents so work can continue without replaying full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md` (this file)

## Current repository state
- Branch: `main`
- Remote: `origin` = `https://github.com/miandre/NAMPlugin1.git`
- `main` is synced to `origin/main` at commit `f33f9b8`.
- Working tree was clean right after merge/push of `f33f9b8`.

## Submodule state (important)
- `iPlug2` remote is your fork:
  - `https://github.com/miandre/iPlug2.git`
- Superproject pins `iPlug2` to:
  - `b54bf7af6b15f84b9bd7593e63bdf75e70d658db`

## Baseline commits to preserve
- `596b97a` Fix top-nav Amp/Cab bypass to gate DSP sections
- `ae5e65e` Submodule pointer update to forked iPlug2
- `73c276f` Null-buffer hardening + APP IO config
- `b37dd5b` Phase 1: mono-core to stereo bus and output routing
- `06239f9` Phase 2: plugin dual-mono core for stereo input processing
- `4c6ad96` Phase 2: stereo input mode UI + stereo-safe model/IR loading
- `f33f9b8` Fix state recall and async amp slot model loading

## What was completed in this wave
### 1) Stereo work (Phase 1 + 2)
- Plugin/app now support stereo output path with stereo-capable routing.
- Plugin stereo input mode implemented (dual-mono model cores) with mono mode retained.
- Standalone stereo input test support kept, with default behavior still mono-friendly.
- Mono/stereo input mode is user-toggleable in GUI (top section) and default is mono (`Input 2` off behavior in practice).
- Stereo-mode load/routing bugs were fixed so models/IRs load and apply correctly in both mono and stereo input modes.

### 2) Slot behavior + model loading robustness
- Non-active slot clear now targets the correct slot via control tag.
- Amp slot switching and model loading moved to async/background workflow:
  - background worker loads NAM models,
  - lock-free atomic handoff to audio thread,
  - audio thread owns final swap of active slot model.
- Goal achieved: avoid synchronous slot-load stalls and avoid previous transient blend/volume jump behavior during slot switch.

### 3) State recall overhaul
- State chunks are enabled (`PLUG_DOES_STATE_CHUNKS 1`).
- New state schema includes:
  - active amp slot,
  - all 3 amp slot model paths,
  - stomp/IR paths,
  - top-nav active section + bypass states,
  - per-slot amp state payload,
  - full parameter payload.
- Legacy state loading paths preserved/fallback handled.
- Session reopen + DAW preset recall now restore slot/model behavior correctly (user-verified).

### 4) Standalone state persistence
- Standalone now persists/restores state to local app data (`plugin-state.bin`), so reopening the EXE can restore previous state.

## Practical behavior status (user-verified)
- Sonarworks ASIO crash fix baseline still good.
- Standalone allows disabling right input (`Audio In R = off`).
- Mono and stereo input routing/load behavior works in standalone and plugin.
- Session open/close and DAW preset recall now restore expected state.

## Open issue to investigate next
1. CPU inefficiency case:
   - Repro: run standalone (mono input) or plugin on mono track, then set plugin input mode to stereo.
   - Observation: heavy models can start crackling/clicking in stereo mode even though only one real input is available.
   - Hypothesis: redundant dual-path processing is still running when effective input is mono.
   - Next agent should validate signal-path gating and add an optimization path for "effective mono input" while keeping stereo mode semantics intact.

## Requested upcoming features
1. Built-in preset system for standalone app:
   - There is a preset menu but no full create/save workflow yet.
   - Add save/new/overwrite UX and state serialization integration.
2. Stereo FX improvements:
   - Revisit delay/reverb with proper stereo implementations (not just dual-mono mirror where avoidable).
   - Keep RT-safe DSP and minimal UI disruption unless requested.

## Verification reminders
- Validate in `Release|x64` only.
- Run standalone via `Ctrl+F5` in Visual Studio (not under debugger for DSP/perf judgments).
- User preference: do not run builds unless explicitly requested by user.

## Starter prompt for next agent (copy/paste)
You are continuing work in `D:\\Dev\\NAMPlugin` on branch `main`.

Read first, in this exact order:
1) `AGENTS.md`
2) `SKILLS.md`
3) `AGENT_SESSION_SUMMARY.md`

Then confirm current git/submodule state before editing:
- `git status --short`
- `git submodule status iPlug2`
- `git -C iPlug2 remote -v`

Current baseline to preserve:
- `main` includes:
  - `596b97a` (top-nav Amp/Cab bypass fix)
  - `ae5e65e` (iPlug2 forked submodule pointer update)
  - `73c276f` (null-buffer hardening + APP IO config)
  - `b37dd5b` (stereo Phase 1 routing)
  - `06239f9` (stereo Phase 2 dual-mono core)
  - `4c6ad96` (stereo input mode UI + stereo-safe load/routing)
  - `f33f9b8` (state recall + async model bank loading)
- `iPlug2` is pinned to `b54bf7af6` from `https://github.com/miandre/iPlug2.git`.
- Sonarworks ASIO crash fix should remain intact.

New tasks:
1) Investigate/fix CPU issue: in stereo input mode with effectively mono input source, avoid redundant dual-path heavy processing that causes crackle on CPU-heavy models.
2) Design + implement built-in standalone preset save/create workflow (existing preset menu currently lacks full authoring flow).
3) Propose and implement a proper stereo reverb/delay strategy (minimal diff first, RT-safe, then iterate).

Task style requirements:
- Propose minimal diffs first.
- Keep parameter enum additions append-only.
- Preserve audio-thread safety (no alloc/locks/I/O/logging in callback path).
- Do not run builds unless explicitly requested by user.
