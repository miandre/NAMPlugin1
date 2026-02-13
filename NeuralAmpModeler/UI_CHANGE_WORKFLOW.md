# NeuralAmpModeler UI Change Workflow

## Purpose

This guide describes a low-risk workflow for major UI/UX changes while preserving current behavior (model loading, IR loading/blend, gate LED logic, EQ/gate toggles, settings panel, meters, and serialization-related param wiring).

## Guiding Principle

Change visual structure first, behavior second. Keep bridge points stable until each visual pass is validated.

## Phase 0: Baseline Snapshot

Before editing:

1. Capture a baseline screenshot (main view + settings view).
2. Note current control positions and tags:
- Param controls (`kInputLevel`, `kNoiseGateThreshold`, etc.).
- Tagged controls (`kCtrlTagModelFileBrowser`, `kCtrlTagIRFileBrowserLeft`, `kCtrlTagIRFileBrowserRight`, `kCtrlTagSettingsBox`, etc.).
3. Confirm baseline behavior manually:
- Model and IR browse/load/clear.
- IR toggle and blend.
- Gate LED behavior.
- EQ and gate disable states.

Key references:

- Tags: `NeuralAmpModeler/NeuralAmpModeler.h`
- Layout/attachments: `NeuralAmpModeler/NeuralAmpModeler.cpp:113`

## Phase 1: Choose Scope

Pick one scope per patch:

- Skin only (colors/assets/fonts).
- Layout only (reposition existing controls).
- Control drawing logic (custom look/interaction).
- UX behavior changes (new flows, tabs, pages).

Do not mix all scopes in one patch unless necessary.

## Phase 2: Execute in Safe Order

Recommended order for major redesign:

1. Theme pass
- `Colors.h`
- Style objects in `NeuralAmpModeler.cpp`

2. Asset pass
- `resources/img`, `resources/fonts`
- `config.h` macros only if new names are introduced
- `main.rc` updates for Windows if adding new resources

3. Layout pass
- `mLayoutFunc` geometry only
- Keep callback wiring untouched

4. Custom control pass
- `NeuralAmpModelerControls.h` draw overrides
- Avoid changing control tag or param assignments unless planned

5. Behavior pass (only if needed)
- `OnParamChangeUI`, `OnUIOpen`, `OnMessage`, per-control callbacks

## Phase 3: Preserve Functional Contracts

When redesigning UI, keep these contracts stable:

1. Param binding contract
- Controls remain bound to intended params.
- Param IDs/order in `EParams` are not casually changed.

2. Control tag contract
- Existing `kCtrlTag*` tags remain valid where used by delegate code.

3. Message contract
- Existing `kMsgTag*` semantics remain valid.
- File browser controls still receive `kMsgTagLoaded*` and `kMsgTagLoadFailed`.

4. UI state contract
- `OnParamChangeUI()` still disables/enables correct controls.
- `OnUIOpen()` still restores loaded model/IR labels.

## Phase 4: Validate Each Pass

For each incremental UI patch, validate:

1. Build:
- Windows `Release|x64`.

2. Interaction:
- All knobs move, show values, and update labels.
- Gate/EQ toggles still disable expected controls.
- Settings panel opens/closes and remains interactive.
- File browser rows still support load/prev/next/clear/get.

3. Visual integrity:
- No overlap/clipping at current plugin size.
- Text remains readable over backgrounds.
- Hover/pressed/disabled states are visible.
- DPI variants (`@2x`, `@3x`) look correct.

4. Behavior integrity:
- No regressions in model/IR staging behavior.
- Gate LED still tracks attenuation, not only gate switch state.

## Phase 5: Platform Packaging Checks

When adding or renaming assets:

Windows:

- Update `config.h` macro(s).
- Add resource entries in `resources/main.rc`.

macOS/iOS:

- Ensure files are in `resources/img` or `resources/fonts`.
- Resource copy scripts should include them automatically.

If an asset appears on one platform but not another, check packaging first before changing runtime code.

## Practical Refactor Pattern

For big redesigns, split commits like this:

1. `ui/theme`: color and style only.
2. `ui/assets`: image/font replacements and resource wiring.
3. `ui/layout`: `IRECT` and attachment reorder only.
4. `ui/controls`: draw overrides and minor interaction polish.
5. `ui/behavior` (optional): explicit UX flow changes.

This makes regressions fast to isolate.

## Common Pitfalls

1. Changing control tags and forgetting delegate lookups.
2. Reordering layout attachments and accidentally hiding controls by z-order.
3. Renaming assets in code but not in `main.rc` (Windows).
4. Breaking disable-state UX by skipping updates in `OnParamChangeUI()`.
5. Mixing visual and behavior changes in one large patch.

## Suggested Starting Strategy for Your Future UI Rewrite

1. Keep all existing controls/callbacks first and redesign only geometry + style.
2. Verify behavior parity.
3. Then replace individual controls with new classes one by one if needed.
4. Keep one parity checklist and run it after each UI patch.
