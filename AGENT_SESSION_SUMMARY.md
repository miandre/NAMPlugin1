# AGENT_SESSION_SUMMARY.md

Last updated: 2026-02-26

Purpose: concise handoff for new agents so work can continue without replaying full chat history.

## Read order for new agents
1. `AGENTS.md`
2. `SKILLS.md`
3. `AGENT_SESSION_SUMMARY.md` (this file)

## Current repository state
- Branch: `main`
- Remote: `origin` = `https://github.com/miandre/NAMPlugin1.git`
- `main` is synced to `origin/main` at commit `73c276f`.
- Working tree: clean.

## Submodule state (important)
- `iPlug2` submodule URL is now your fork:
  - `https://github.com/miandre/iPlug2.git`
- Superproject pins `iPlug2` to commit:
  - `b54bf7af6b15f84b9bd7593e63bdf75e70d658db`
- In your forked `iPlug2`, fix branch used:
  - `nam/sonarworks-asio-and-input-routing`

## What was completed in this session context
### 1) Startup test asset convenience (tmpLoad)
- Added startup defaults to auto-load local test assets for faster iteration (test-only workflow).
- Related commit already on `main`:
  - `e1a1f7f`: startup tmpLoad defaults with compile-time toggle.

### 2) Top-nav bypass bug fixes
- Fixed top icon menu section bypass/deactivate so Amp and Cab section deactivate behaves like section bypass controls.
- Related commit on `main`:
  - `596b97a`: top-nav Amp/Cab bypass now gates DSP sections.

### 3) Sonarworks ASIO wrapper crash investigation + fix path
Observed crash:
- Access violation executing `0x0000000000000000`.
- Call stack remained inside `SonarworksASIODriver.dll` and `SSLUSBDriverasio_x64.dll`.

Effective fix path implemented in `iPlug2` and pinned via submodule:
- `Dependencies/IPlug/RTAudio/RtAudio.cpp`
  - Added `bufferSwitchTimeInfo` callback shim.
  - Set `asioCallbacks.bufferSwitchTimeInfo = &bufferSwitchTimeInfo`.
  - `kAsioSupportsTimeInfo` now returns `1` (was `0`).
- `IPlug/APP/IPlugAPP_host.cpp`
  - More robust stream init/channel count handling.
  - Uses actual opened channel counts; null-safe pointer wiring.
  - Uses callback `nFrames` correctly.
- `IPlug/APP/IPlugAPP.cpp`
  - Processes using incoming frame count (`nFrames`) instead of assumed block size.
- `IPlug/APP/IPlugAPP_dialog.cpp`
  - Input routing UI improvements: right input can be set to `off`.
  - Removed legacy TEMP behavior that forced coupled stereo selection.
- `IPlug/APP/IPlugAPP_host.h`
  - Added opened-channel state fields; later cleaned dead legacy `mBufIndex` usage.

Submodule commit containing these iPlug2 fixes:
- `b54bf7af6`: `APP/ASIO: fix Sonarworks callback crash and honor input channel routing`.

### 4) Superproject hardening
- `NeuralAmpModeler/NeuralAmpModeler.cpp`
  - Added null guards in input/output/meter paths to tolerate null channel pointers safely.
- `NeuralAmpModeler/config.h`
  - APP channel IO set to `"1-2 2-2"` to keep stereo input capability available in standalone.

Related commit on `main`:
- `73c276f`: `Standalone: harden null buffer handling and expose 2-2 APP IO config`.

### 5) Submodule pointer + fork URL update
- `.gitmodules` updated to your iPlug2 fork URL.
- Superproject now points `iPlug2` submodule to `b54bf7af6`.
- Related commit on `main`:
  - `ae5e65e`: `Submodule: point iPlug2 to fork and pin Sonarworks/IO fix commit`.

## Practical behavior status after fixes
- Sonarworks-wrapped ASIO no longer crashes in user verification.
- Standalone input routing now allows disabling right input (`Audio In R = off`).
- Feeding interface input 2 can be effectively disabled from app settings.

## Verification reminders
- Validate in `Release|x64` only.
- Run standalone via `Ctrl+F5` in Visual Studio (not under debugger for DSP/perf judgments).
- User preference: do not run builds from agent unless explicitly asked.

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
- `iPlug2` is pinned to `b54bf7af6` from `https://github.com/miandre/iPlug2.git`.
- Sonarworks ASIO crash is currently fixed in this baseline.
- Standalone `Audio In R` can be set to `off`.

Task style requirements:
- Propose minimal diffs first.
- Keep parameter enum additions append-only.
- Preserve audio-thread safety (no alloc/locks/I/O/logging in callback path).
- Do not run builds unless explicitly requested by user.
