# NeuralAmpModeler UI Engine Overview

## Purpose

This document explains how the current UI is built, what comes from iPlug2, and what is custom in this project.

Primary files:

- `NeuralAmpModeler/NeuralAmpModeler.cpp`
- `NeuralAmpModeler/NeuralAmpModeler.h`
- `NeuralAmpModeler/NeuralAmpModelerControls.h`
- `NeuralAmpModeler/Colors.h`
- `NeuralAmpModeler/config.h`

## iPlug2 vs Project Overrides

Most core UI machinery is inherited from iPlug2:

- Window/editor lifecycle.
- `IGraphics` rendering and control tree.
- Base controls (`IControl`, `IVKnobControl`, `IVSlideSwitchControl`, etc.).
- Text entry, file dialogs, popup menu framework, and control invalidation.

Project-specific customization is concentrated in three places:

1. UI composition/layout in `mLayoutFunc` (`NeuralAmpModeler.cpp:113`).
2. Theme/style setup in `Colors.h` + `IVStyle` initialization (`NeuralAmpModeler.cpp:25`).
3. Custom wrapper controls in `NeuralAmpModelerControls.h`.

## UI Lifecycle

## 1) Graphics creation

- `mMakeGraphicsFunc` constructs the UI surface with `MakeGraphics(...)`.
- Uses `PLUG_WIDTH`/`PLUG_HEIGHT`/`PLUG_FPS` from `config.h`.
- iOS gets a scale adjustment (`NeuralAmpModeler.cpp:104`).

## 2) Layout build

`mLayoutFunc` does the full UI build (`NeuralAmpModeler.cpp:113`):

- Enables resizer, tooltips, multitouch.
- Loads fonts/SVG/bitmaps.
- Computes all `IRECT` regions.
- Attaches controls in draw order.

All visible UI is assembled here.

## 3) Runtime synchronization

- `OnUIOpen()` rehydrates file browser labels/history for already-loaded model/IRs (`NeuralAmpModeler.cpp:536`).
- `OnParamChangeUI()` enables/disables controls based on toggles (`NeuralAmpModeler.cpp:588`).
- `OnIdle()` updates meters and gate LED state from delegate-side state (`NeuralAmpModeler.cpp:466`).
- `OnMessage()` handles UI-originated control messages (clear model/IR, highlight color) (`NeuralAmpModeler.cpp:610`).

## Layout and Composition Model

Layout is manual and deterministic, built from `IRECT` transforms:

- Start from `pGraphics->GetBounds()`.
- Derive padded regions (`GetPadded`, `GetFromTop`, `GetFromBottom`, etc.).
- Split knob row with `GetGridCell` columns (`NeuralAmpModeler.cpp:155`).
- Split file browser section with `SubRectVertical` (`NeuralAmpModeler.cpp:181`).

There is no declarative/flex layout engine in use. Major UI changes are done by editing this geometry code.

## Styling System

Theme color constants are in `NeuralAmpModeler/Colors.h`.

Global style structs are built in `NeuralAmpModeler.cpp`:

- `colorSpec`
- `style`
- `titleStyle`
- `radioButtonStyle`

Runtime highlight recolor is handled via message `kMsgTagHighlightColor` and `IVectorBase::SetColor(...)` in `OnMessage()` (`NeuralAmpModeler.cpp:617`).

## Custom Controls (Override Layer)

`NeuralAmpModelerControls.h` defines the project-specific UI controls:

- `NAMKnobControl`: custom knob drawing with bitmap face + custom indicator.
- `NAMSwitchControl`: custom track/handle drawing for slide switches.
- `NAMMeterControl`: meter with custom bitmap background and bar rendering.
- `NAMFileBrowserControl`: composite row with load/prev/next/name/clear/get controls and per-row last-directory memory.
- `NAMSettingsPageControl`: animated overlay panel with nested controls.
- `NAMLEDControl`: gate attenuation indicator LED.

These classes mainly override drawing and per-control interaction behavior while staying inside iPlug2 control APIs.

## Resource Ownership

Asset filename macros are declared in `NeuralAmpModeler/config.h:59`.

Windows resource embedding:

- `NeuralAmpModeler/resources/main.rc` includes fonts and images from `resources/fonts` and `resources/img`.

Build-time resource include paths (Windows):

- `common-win.props:96`.

macOS/iOS resource copying:

- `NeuralAmpModeler/scripts/prepare_resources-mac.py`
- `NeuralAmpModeler/scripts/prepare_resources-ios.py`

## UI <-> DSP Bridge Points

Control and message IDs are in `NeuralAmpModeler.h`:

- Control tags: `ECtrlTags` (`NeuralAmpModeler.h:57`).
- Message tags: `EMsgTags` (`NeuralAmpModeler.h:72`).

Important bridge behaviors:

- File browsers send clear messages to delegate.
- Delegate sends load/failed messages back to specific browser controls.
- Gate LED visual state comes from `mNoiseGateIsAttenuating` atomic and is pushed in `OnIdle()`.

## What to Modify for a Major Redesign

For a full UI/UX rewrite while keeping behavior:

1. Rearrange geometry and attachment order in `mLayoutFunc`.
2. Restyle colors and text via `Colors.h` and style objects.
3. Replace or redraw assets in `resources/img` and `resources/fonts`.
4. Extend/refactor custom controls in `NeuralAmpModelerControls.h` when visuals/interaction exceed simple layout changes.

Keep parameter IDs, control tags, and message tags stable unless you intentionally migrate them.
