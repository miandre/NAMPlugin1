# NeuralAmpModeler UI Graphical Elements Map

## Purpose

This is a practical map of what is currently rendered, where it comes from, and how to change it.

## Asset Declaration and Packaging

## Filename macros (source of truth)

Asset macros are in `NeuralAmpModeler/config.h`:

- Fonts: `ROBOTO_FN`, `MICHROMA_FN`
- SVG icons: `GEAR_FN`, `FILE_FN`, `CLOSE_BUTTON_FN`, `LEFT_ARROW_FN`, `RIGHT_ARROW_FN`, `MODEL_ICON_FN`, `IR_ICON_ON_FN`, `IR_ICON_OFF_FN`, `GLOBE_ICON_FN`
- Bitmaps/JPGs: background, knob, file row, input level, lines, switch handle, meter

## Asset files on disk

- Images: `NeuralAmpModeler/resources/img`
- Fonts: `NeuralAmpModeler/resources/fonts`

## Windows resource embedding

`NeuralAmpModeler/resources/main.rc` embeds all listed fonts/images (see section that starts at `main.rc:262`).

If you add a new asset for Windows:

1. Add macro in `config.h`.
2. Add resource entry in `main.rc`.
3. Load and use it in `mLayoutFunc`.

## macOS/iOS resource copying

- macOS: `NeuralAmpModeler/scripts/prepare_resources-mac.py`
- iOS: `NeuralAmpModeler/scripts/prepare_resources-ios.py`

Both scripts copy all files from `resources/img` and `resources/fonts`.

## Visual Element Inventory

## 1) Global background and decorative layer

- Background image: `BACKGROUND_FN`
- Foreground decorative lines overlay: `LINES_FN`
- Attached in `NeuralAmpModeler.cpp:249` and `NeuralAmpModeler.cpp:250`

How to change:

- Replace `Background*.jpg` and/or `Lines*.png` files to reskin without code changes.
- Update layout only if coverage or composition must change.

## 2) Typography

- Fonts loaded in `NeuralAmpModeler.cpp:120`.
- Title style uses `Michroma-Regular` (`NeuralAmpModeler.cpp:53`).
- Most control text uses style value/label text color from `IVStyle`.

How to change:

- Swap TTF files while keeping same macro names for a drop-in change.
- Or define new font macro + load call + style assignment.

## 3) Color theme

- Core palette constants: `NeuralAmpModeler/Colors.h`.
- Main style colors in `colorSpec` (`NeuralAmpModeler.cpp:25`).
- Runtime highlight override via `kMsgTagHighlightColor` (`NeuralAmpModeler.cpp:617`).

How to change:

- Edit `Colors.h` for global palette shifts.
- Edit style construction in `NeuralAmpModeler.cpp` for per-role color behavior.
- Keep contrast checks around labels/value text and disabled states.

## 4) Knobs

- Class: `NAMKnobControl` in `NeuralAmpModelerControls.h:85`.
- Uses bitmap face (`KnobBackground`) + custom ring and pointer glow.
- Instances attached in `NeuralAmpModeler.cpp:284` through `NeuralAmpModeler.cpp:294`.

How to change:

- Skin only: replace `KnobBackground*.png`.
- Shape/indicator behavior: edit `NAMKnobControl::DrawWidget`.
- Positioning: edit knob `IRECT`s in layout block.

## 5) Switches (Noise Gate / EQ)

- Class: `NAMSwitchControl` in `NeuralAmpModelerControls.h:117`.
- Uses custom track drawing + `SlideSwitchHandle` bitmap.
- Instances attached in `NeuralAmpModeler.cpp:278` and `NeuralAmpModeler.cpp:281`.

How to change:

- Skin only: replace `SlideSwitchHandle*.png`.
- Visual behavior: edit `DrawTrack` / `DrawHandle`.
- Positioning: change `ngToggleArea` / `eqToggleArea`.

## 6) Gate LED indicator

- Class: `NAMLEDControl` in `NeuralAmpModelerControls.h:63`.
- Positioned near gate control in `NeuralAmpModeler.cpp:171`.
- Value updated in `OnIdle()` from attenuation state (`NeuralAmpModeler.cpp:471`).

How to change:

- Appearance: edit fill/stroke colors and radius in `NAMLEDControl::Draw`.
- Position: adjust `noiseGateLEDRect`.

## 7) File browser rows (Model / IR L / IR R)

- Composite control class: `NAMFileBrowserControl` (`NeuralAmpModelerControls.h:257`).
- Uses row background bitmap (`FileBackground`) and SVG buttons.
- Attached in `NeuralAmpModeler.cpp:261`, `NeuralAmpModeler.cpp:267`, `NeuralAmpModeler.cpp:273`.

Child elements:

- Load button icon (`File.svg`)
- Prev/next arrows
- Filename button
- Right-side clear/get button state

How to change:

- Row skin: replace `FileBackground*.png`.
- Button visuals: replace relevant SVG files.
- Row composition behavior: edit `OnAttached()` in `NAMFileBrowserControl`.
- Layout placement: edit `modelArea`, `irLeftArea`, `irRightArea`.

## 8) IR toggle and blend control visuals

- IR on/off switch icon: `ISVGSwitchControl` at `NeuralAmpModeler.cpp:266`.
- Cab blend knob uses `NAMKnobControl` at `NeuralAmpModeler.cpp:277`.

How to change:

- Toggle icon style: replace `IRIconOn.svg` and `IRIconOff.svg`.
- Blend knob style/label behavior: same knobs guidance above.

## 9) Meters

- Class: `NAMMeterControl` in `NeuralAmpModelerControls.h:503`.
- Meter background bitmap: `MeterBackground`.
- Attached in `NeuralAmpModeler.cpp:297` and `NeuralAmpModeler.cpp:298`.

How to change:

- Skin only: replace `MeterBackground*.png`.
- Rendering style (track/peak/grid): edit `DrawTrackHandle` and `DrawPeak`.
- Placement: edit `inputMeterArea` and `outputMeterArea`.

## 10) Settings overlay panel

- Container class: `NAMSettingsPageControl` (`NeuralAmpModelerControls.h:683`).
- Full-panel background bitmap uses `BACKGROUND_FN`.
- Open/close animation uses control blend changes (`HideAnimated`).
- Attached hidden by default in `NeuralAmpModeler.cpp:309`.

How to change:

- Panel content/layout: edit `NAMSettingsPageControl::OnAttached`.
- Animation behavior: edit `HideAnimated`.
- Close/settings button visuals: `NAMSquareButtonControl` / `NAMCircleButtonControl` and corresponding SVGs.

## 11) Model info/about text block

- `ModelInfoControl` and nested `AboutControl` in `NeuralAmpModelerControls.h`.
- Updated from model metadata by `_UpdateControlsFromModel()` (`NeuralAmpModeler.cpp:1030`).

How to change:

- Text style and content: edit those control classes.
- Leave control tags/bridge logic intact unless intentionally migrating.

## Safe Change Recipes

## Recipe A: Reskin only (no behavior changes)

1. Replace files in `resources/img` and/or `resources/fonts` with same names.
2. Rebuild.
3. Validate DPI variants (`@2x`, `@3x`) look consistent.

## Recipe B: New asset with new name

1. Add macro in `config.h`.
2. Add Windows resource line in `main.rc`.
3. Load asset in `mLayoutFunc`.
4. Wire asset into control(s).
5. Rebuild and verify on standalone app first.

## Recipe C: Move controls without changing behavior

1. Edit only `IRECT` geometry in `mLayoutFunc`.
2. Keep same param IDs, control tags, and callbacks.
3. Verify enable/disable logic still lines up (gate, EQ, IR toggles).

## High-Risk Areas (Avoid Accidental Breakage)

- `EParams` order and names in `NeuralAmpModeler.h`.
- `ECtrlTags` and `EMsgTags` mappings.
- File browser callbacks and message tags.
- `OnParamChangeUI()` disable rules.
- `OnUIOpen()` state re-sync behavior.
