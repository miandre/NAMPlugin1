#pragma once

#include <array>
#include <algorithm> // std::clamp
#include <cmath> // std::round
#include <functional>
#include <memory>
#include <sstream> // std::stringstream
#include <string>
#include <unordered_map> // std::unordered_map
#include <utility>
#include "IControls.h"

#define PLUG() static_cast<PLUG_CLASS_NAME*>(GetDelegate())
#define NAM_KNOB_HEIGHT 120.0f
#define NAM_SWTICH_HEIGHT 50.0f

using namespace iplug;
using namespace igraphics;

enum class NAMBrowserState
{
  Empty, // when no file loaded, show "Get" button
  Loaded // when file loaded, show "Clear" button
};

// Where the corner button on the plugin (settings, close settings) goes
// :param rect: Rect for the whole plugin's UI
IRECT CornerButtonArea(const IRECT& rect)
{
  const auto mainArea = rect.GetPadded(-20);
  return mainArea.GetFromTRHC(50, 50).GetCentredInside(20, 20);
};

class NAMSquareButtonControl : public ISVGButtonControl
{
public:
  NAMSquareButtonControl(const IRECT& bounds, IActionFunction af, const ISVG& svg)
  : ISVGButtonControl(bounds, af, svg, svg)
  {
  }

  void Draw(IGraphics& g) override
  {
    if (mMouseIsOver)
      g.FillRoundRect(PluginColors::MOUSEOVER, mRECT, 2.f);

    ISVGButtonControl::Draw(g);
  }
};

class NAMCircleButtonControl : public ISVGButtonControl
{
public:
  NAMCircleButtonControl(const IRECT& bounds, IActionFunction af, const ISVG& svg)
  : ISVGButtonControl(bounds, af, svg, svg)
  {
  }

  void Draw(IGraphics& g) override
  {
    if (mMouseIsOver)
    {
      // Match top-nav hover feel with a clearer "pop" around the cog.
      const auto hoverRect = mRECT.GetScaledAboutCentre(1.08f);
      g.FillEllipse(PluginColors::MOUSEOVER.WithOpacity(0.1f), hoverRect);
      g.DrawEllipse(PluginColors::NAM_THEMECOLOR.WithOpacity(0.3f), hoverRect, &mBlend, 1.2f);
    }

    ISVGButtonControl::Draw(g);
  }
};

class NAMBackgroundBitmapControl : public IBitmapControl
{
public:
  NAMBackgroundBitmapControl(const IRECT& bounds, const char* resourceName, const IBitmap& bitmap)
  : IBitmapControl(bounds, bitmap)
  , mResourceName(resourceName)
  {
    mIgnoreMouse = true;
  }

  void OnRescale() override
  {
    const int targetScale = Clip(static_cast<int>(std::ceil(GetUI()->GetTotalScale())), 1, 3);
    mBitmap = GetUI()->LoadBitmap(mResourceName.c_str(), 1, false, targetScale);
  }

  void SetResourceName(const char* resourceName)
  {
    if (resourceName == nullptr || mResourceName == resourceName)
      return;
    mResourceName = resourceName;
    OnRescale();
    SetDirty(false);
  }

private:
  std::string mResourceName;
};

class NAMLEDControl : public IControl
{
public:
  NAMLEDControl(const IRECT& bounds, const IColor& onColor = IColor(255, 96, 230, 120),
                const IColor& offColor = COLOR_BLACK.WithOpacity(0.85f))
  : IControl(bounds)
  , mOnColor(onColor)
  , mOffColor(offColor)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override
  {
    const auto radius = static_cast<float>((mRECT.W() < mRECT.H() ? mRECT.W() : mRECT.H()) * 0.35);
    const float cx = mRECT.MW();
    const float cy = mRECT.MH();
    const bool isOn = GetValue() > 0.5;
    const IColor fillColor = isOn ? mOnColor : mOffColor;

    g.FillCircle(fillColor, cx, cy, radius, &mBlend);
    g.DrawCircle(COLOR_BLACK.WithOpacity(0.75f), cx, cy, radius, &mBlend, 1.0f);
  }

private:
  IColor mOnColor;
  IColor mOffColor;
};

class NAMOutlinedLEDControl : public IControl
{
public:
  NAMOutlinedLEDControl(const IRECT& bounds,
                        const IColor& onFillColor = COLOR_WHITE.WithOpacity(0.92f),
                        const IColor& offOutlineColor = COLOR_WHITE.WithOpacity(0.92f),
                        const IColor& onOutlineColor = COLOR_WHITE.WithOpacity(0.92f))
  : IControl(bounds)
  , mOnFillColor(onFillColor)
  , mOffOutlineColor(offOutlineColor)
  , mOnOutlineColor(onOutlineColor)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override
  {
    const float radius = static_cast<float>((mRECT.W() < mRECT.H() ? mRECT.W() : mRECT.H()) * 0.34f);
    const float cx = mRECT.MW();
    const float cy = mRECT.MH();
    const bool isOn = GetValue() > 0.5;

    if (isOn)
      g.FillCircle(mOnFillColor, cx, cy, radius, &mBlend);

    g.DrawCircle(isOn ? mOnOutlineColor : mOffOutlineColor, cx, cy, radius, &mBlend, 1.4f);
  }

private:
  IColor mOnFillColor;
  IColor mOffOutlineColor;
  IColor mOnOutlineColor;
};

class NAMBitmapLEDControl : public IControl
{
public:
  NAMBitmapLEDControl(const IRECT& bounds, const IBitmap& onBitmap, const IBitmap& offBitmap)
  : IControl(bounds)
  , mOnBitmap(onBitmap)
  , mOffBitmap(offBitmap)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override
  {
    g.DrawFittedBitmap(GetValue() > 0.5 ? mOnBitmap : mOffBitmap, mRECT);
  }

  void OnRescale() override
  {
    mOnBitmap = GetUI()->GetScaledBitmap(mOnBitmap);
    mOffBitmap = GetUI()->GetScaledBitmap(mOffBitmap);
  }

private:
  IBitmap mOnBitmap;
  IBitmap mOffBitmap;
};

class NAMBitmapToggleControl : public IControl
{
public:
  NAMBitmapToggleControl(const IRECT& bounds, int paramIdx, const IBitmap& offBitmap, const IBitmap& onBitmap)
  : IControl(bounds, paramIdx)
  , mOffBitmap(offBitmap)
  , mOnBitmap(onBitmap)
  {
  }

  void Draw(IGraphics& g) override
  {
    g.DrawFittedBitmap(GetValue() > 0.5 ? mOnBitmap : mOffBitmap, mRECT);
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    if (IsDisabled())
      return;
    SetValueFromUserInput(GetValue() > 0.5 ? 0.0 : 1.0);
  }

  void OnRescale() override
  {
    mOffBitmap = GetUI()->GetScaledBitmap(mOffBitmap);
    mOnBitmap = GetUI()->GetScaledBitmap(mOnBitmap);
  }

private:
  IBitmap mOffBitmap;
  IBitmap mOnBitmap;
};

class NAMSyncTextToggleControl : public IControl
{
public:
  NAMSyncTextToggleControl(const IRECT& bounds, int paramIdx, const IText& onText, const IText& offText)
  : IControl(bounds, paramIdx)
  , mOnText(onText)
  , mOffText(offText)
  {
  }

  void Draw(IGraphics& g) override
  {
    const bool syncOn = _IsSyncOn();
    if (mMouseIsOver && !IsDisabled() && !syncOn)
      g.FillRoundRect(COLOR_WHITE.WithOpacity(0.08f), mRECT, 3.0f, &mBlend);
    g.DrawText(syncOn ? mOnText : mOffText, "SYNC", mRECT, &mBlend);
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    if (IsDisabled())
      return;

    if (const auto* pParam = GetParam())
    {
      const int nextMode = (pParam->Int() == 0) ? 1 : 0;
      SetValueFromUserInput(pParam->ToNormalized(static_cast<double>(nextMode)));
      return;
    }

    SetValueFromUserInput(_IsSyncOn() ? 1.0 : 0.0);
  }

private:
  bool _IsSyncOn() const
  {
    if (const auto* pParam = GetParam())
      return pParam->Int() == 0;
    return GetValue() < 0.5;
  }

  IText mOnText;
  IText mOffText;
};

class NAMTempoNumberBoxControl : public IVNumberBoxControl
{
public:
  using IVNumberBoxControl::IVNumberBoxControl;

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (IsDisabled())
      return;
    IVNumberBoxControl::OnMouseDown(x, y, mod);
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    if (IsDisabled())
      return;
    IVNumberBoxControl::OnMouseDrag(x, y, dX, dY, mod);
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    if (IsDisabled())
      return;
    IVNumberBoxControl::OnMouseWheel(x, y, mod, d);
  }

  void OnMouseDblClick(float x, float y, const IMouseMod& mod) override
  {
    if (IsDisabled())
      return;
    IVNumberBoxControl::OnMouseDblClick(x, y, mod);
  }

  void OnAttached() override
  {
    IVNumberBoxControl::OnAttached();
    _ApplyVisualState(false);
  }

  void Draw(IGraphics& g) override
  {
    const bool disabled = IsDisabled();
    const bool active = (mMouseIsOver || mMouseIsDown) && !disabled;
    _ApplyVisualState(active);
    if (disabled)
    {
      // Keep disabled hover identical to idle (no white flash while SYNC is on).
      const IColor idleFill = IColor(40, 255, 255, 255);
      SetColor(kHL, idleFill);
      SetColor(kFG, idleFill);
    }
    if (!active && mTextReadout != nullptr)
      g.FillRect(IColor(40, 255, 255, 255), mTextReadout->GetRECT(), &mBlend);
    IVNumberBoxControl::Draw(g);
  }

private:
  void _ApplyVisualState(bool active)
  {
    if (mVisualActive == active)
      return;

    mVisualActive = active;

    // Keep hover and drag the same: black text on white field.
    SetColor(kHL, COLOR_WHITE.WithOpacity(0.92f));
    SetColor(kFG, COLOR_WHITE.WithOpacity(0.92f));

    if (mTextReadout != nullptr)
    {
      const IColor textColor = active ? COLOR_BLACK.WithOpacity(0.95f) : COLOR_WHITE.WithOpacity(0.96f);
      const IVStyle readoutStyle = mStyle.WithDrawFrame(true).WithValueText(mStyle.valueText.WithFGColor(textColor));
      mTextReadout->SetStyle(readoutStyle);
      mTextReadout->SetDirty(false);
    }
  }

  bool mVisualActive = true;
};

// A toggle parameter button with momentary press visuals:
// bitmap goes "down" only while the mouse is pressed.
class NAMMomentaryBitmapButtonControl : public IControl
{
public:
  NAMMomentaryBitmapButtonControl(const IRECT& bounds, int paramIdx, const IBitmap& upBitmap, const IBitmap& downBitmap)
  : IControl(bounds, paramIdx)
  , mUpBitmap(upBitmap)
  , mDownBitmap(downBitmap)
  {
  }

  void Draw(IGraphics& g) override
  {
    g.DrawFittedBitmap(mPressed ? mDownBitmap : mUpBitmap, mRECT);
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    if (IsDisabled())
      return;
    mPressed = true;
    SetValueFromUserInput(GetValue() > 0.5 ? 0.0 : 1.0);
    SetDirty(false);
  }

  void OnMouseUp(float, float, const IMouseMod&) override
  {
    mPressed = false;
    SetDirty(false);
  }

  void OnMouseOut() override
  {
    mPressed = false;
    IControl::OnMouseOut();
  }

  void OnRescale() override
  {
    mUpBitmap = GetUI()->GetScaledBitmap(mUpBitmap);
    mDownBitmap = GetUI()->GetScaledBitmap(mDownBitmap);
  }

private:
  IBitmap mUpBitmap;
  IBitmap mDownBitmap;
  bool mPressed = false;
};

class NAMTopIconControl : public IControl
{
public:
  using Action = std::function<void()>;

  NAMTopIconControl(const IRECT& bounds, const IBitmap& onBitmap, const IBitmap& activeBitmap, const IBitmap& offBitmap,
                    Action onActivate, Action onToggleBypass, bool drawActiveUnderline = true)
  : IControl(bounds)
  , mOnBitmap(onBitmap)
  , mActiveBitmap(activeBitmap)
  , mOffBitmap(offBitmap)
  , mOnActivate(std::move(onActivate))
  , mOnToggleBypass(std::move(onToggleBypass))
  , mDrawActiveUnderline(drawActiveUnderline)
  {
  }

  NAMTopIconControl(const IRECT& bounds, const ISVG& onSVG, const ISVG& activeSVG, const ISVG& offSVG, Action onActivate,
                    Action onToggleBypass, bool drawActiveUnderline = true)
  : IControl(bounds)
  , mOnSVG(std::make_unique<ISVG>(onSVG))
  , mActiveSVG(std::make_unique<ISVG>(activeSVG))
  , mOffSVG(std::make_unique<ISVG>(offSVG))
  , mOnActivate(std::move(onActivate))
  , mOnToggleBypass(std::move(onToggleBypass))
  , mDrawActiveUnderline(drawActiveUnderline)
  {
  }

  void SetVisualState(const bool isActive, const bool isBypassed)
  {
    if (mIsActive == isActive && mIsBypassed == isBypassed)
      return;
    mIsActive = isActive;
    mIsBypassed = isBypassed;
    SetDirty(false);
  }

  void Draw(IGraphics& g) override
  {
    const bool drawSVG = (mOnSVG && mActiveSVG && mOffSVG);
    const ISVG* pSVG = nullptr;
    const IBitmap* pBitmap = nullptr;
    float iconAspect = 1.0f;

    if (drawSVG)
    {
      pSVG = mIsActive ? mActiveSVG.get() : (mIsBypassed ? mOffSVG.get() : mOnSVG.get());
      if (pSVG == nullptr || !pSVG->IsValid())
        return;
      const float svgW = pSVG->W();
      const float svgH = pSVG->H();
      iconAspect = (svgW > 0.0f && svgH > 0.0f) ? (svgW / svgH) : 1.0f;
    }
    else
    {
      pBitmap = &(mIsActive ? mActiveBitmap : (mIsBypassed ? mOffBitmap : mOnBitmap));
      if (pBitmap->W() <= 0 || pBitmap->H() <= 0)
        return;
      iconAspect = static_cast<float>(pBitmap->W()) / static_cast<float>(pBitmap->H());
    }

    // Keep a consistent icon height across all top icons; width follows aspect ratio.
    // Reserve inner padding so hover-pop and underline stay inside the slot.
    constexpr float kIconPadX = 2.0f;
    constexpr float kIconPadTop = 4.0f;
    constexpr float kIconPadBottom = 7.0f;
    const IRECT iconSlot(mRECT.L + kIconPadX, mRECT.T + kIconPadTop, mRECT.R - kIconPadX, mRECT.B - kIconPadBottom);
    IRECT drawRect = iconSlot.GetCentredInside(iconSlot.H() * iconAspect, iconSlot.H());
    const IRECT baseIconRect = drawRect;
    if (mMouseIsOver)
    {
      // Subtle "pop" on hover.
      drawRect = drawRect.GetScaledAboutCentre(1.05f).GetVShifted(-0.5f);
    }
    const float alpha = mIsBypassed ? 0.45f : 1.0f;
    IBlend iconBlend(EBlend::Default, alpha);
    if (drawSVG)
    {
      // Keep top-nav SVG icons visible/consistent against the dark header.
      const IColor svgTint = COLOR_WHITE;
      g.DrawSVG(*pSVG, drawRect, &iconBlend, &svgTint, &svgTint);
    }
    else
      g.DrawFittedBitmap(*pBitmap, drawRect, &iconBlend);

    if (mDrawActiveUnderline && mIsActive)
    {
      // Current visible view: fixed-width underline (consistent for all icons), tight to icon.
      constexpr float kFixedUnderlineWidth = 34.0f; // tuned to match stomp icon width visually
      const float underlineWidth = std::min(kFixedUnderlineWidth, baseIconRect.W() - 4.0f);
      const float lineHalfW = 0.5f * underlineWidth;
      const float lineY = iconSlot.B + 7.0f;
      g.DrawLine(COLOR_WHITE.WithOpacity(0.95f),
                 baseIconRect.MW() - lineHalfW,
                 lineY,
                 baseIconRect.MW() + lineHalfW,
                 lineY,
                 &mBlend,
                 1.8f);
    }
  }

  void OnMouseDown(float, float, const IMouseMod& mod) override
  {
    if (IsDisabled())
      return;
    if (mod.C || mod.R)
    {
      if (mOnToggleBypass)
        mOnToggleBypass();
      return;
    }
    if (mOnActivate)
      mOnActivate();
  }

  void OnMouseDblClick(float, float, const IMouseMod&) override
  {
    // Deactivation is handled via Ctrl+Click or Right-Click.
  }

  void OnMouseOver(float x, float y, const IMouseMod& mod) override { IControl::OnMouseOver(x, y, mod); }

  void OnMouseOut() override { IControl::OnMouseOut(); }

  void OnRescale() override
  {
    if (mOnSVG && mActiveSVG && mOffSVG)
      return;
    mOnBitmap = GetUI()->GetScaledBitmap(mOnBitmap);
    mActiveBitmap = GetUI()->GetScaledBitmap(mActiveBitmap);
    mOffBitmap = GetUI()->GetScaledBitmap(mOffBitmap);
  }

private:
  IBitmap mOnBitmap;
  IBitmap mActiveBitmap;
  IBitmap mOffBitmap;
  std::unique_ptr<ISVG> mOnSVG;
  std::unique_ptr<ISVG> mActiveSVG;
  std::unique_ptr<ISVG> mOffSVG;
  Action mOnActivate;
  Action mOnToggleBypass;
  bool mIsActive = false;
  bool mIsBypassed = false;
  bool mDrawActiveUnderline = true;
};

class NAMKnobControl : public IVKnobControl
{
public:
  NAMKnobControl(const IRECT& bounds, int paramIdx, const char* label, const IVStyle& style, const IBitmap& bitmap,
                 bool drawIndicatorTrack = true, bool useDarkIndicatorDot = false, float knobScale = 1.0f,
                 float labelYOffset = 0.0f, float valueYOffset = 0.0f)
  : IVKnobControl(bounds, paramIdx, label, style, true)
  , mBitmap(bitmap)
  , mUseSVG(false)
  , mDrawIndicatorTrack(drawIndicatorTrack)
  , mUseDarkIndicatorDot(useDarkIndicatorDot)
  , mKnobScale(knobScale)
  , mLabelYOffset(labelYOffset)
  , mValueYOffset(valueYOffset)
  {
    mInnerPointerFrac = 0.55;
  }

  NAMKnobControl(const IRECT& bounds, int paramIdx, const char* label, const IVStyle& style, const ISVG& svg,
                 bool drawIndicatorTrack = true, bool useDarkIndicatorDot = false, float knobScale = 1.0f,
                 float labelYOffset = 0.0f, float valueYOffset = 0.0f)
  : IVKnobControl(bounds, paramIdx, label, style, true)
  , mSVG(std::make_unique<ISVG>(svg))
  , mUseSVG(true)
  , mDrawIndicatorTrack(drawIndicatorTrack)
  , mUseDarkIndicatorDot(useDarkIndicatorDot)
  , mKnobScale(knobScale)
  , mLabelYOffset(labelYOffset)
  , mValueYOffset(valueYOffset)
  {
    mInnerPointerFrac = 0.55;
  }

  void OnRescale() override
  {
    if (mBitmap.W() > 0 && mBitmap.H() > 0)
      mBitmap = GetUI()->GetScaledBitmap(mBitmap);
  }
  void OnResize() override
  {
    IVKnobControl::OnResize();
    if (mLabelYOffset != 0.0f)
      mLabelBounds.Translate(0.0f, mLabelYOffset);
    if (mValueYOffset != 0.0f)
      mValueBounds.Translate(0.0f, mValueYOffset);
  }

  void DrawWidget(IGraphics& g) override
  {
    auto knobRect = mWidgetBounds.GetCentredInside(mWidgetBounds.W(), mWidgetBounds.W()).GetScaledAboutCentre(mKnobScale);
    const float widgetRadius = 0.73f * 0.5f * knobRect.W();
    const float cx = knobRect.MW(), cy = knobRect.MH();
    const float angle = mAngle1 + (static_cast<float>(GetValue()) * (mAngle2 - mAngle1));
    if (mDrawIndicatorTrack)
      DrawIndicatorTrack(g, angle, cx + 0.5, cy, widgetRadius);
    if (mUseSVG && mSVG && mSVG->IsValid())
      g.DrawSVG(*mSVG, knobRect, &mBlend);
    else if (mBitmap.W() > 0 && mBitmap.H() > 0)
      g.DrawFittedBitmap(mBitmap, knobRect);
    float data[2][2];
    RadialPoints(angle, cx, cy, mInnerPointerFrac * widgetRadius, mInnerPointerFrac * widgetRadius, 2, data);
    if (mUseDarkIndicatorDot)
    {
      g.FillCircle(COLOR_BLACK.WithOpacity(0.95f), data[1][0], data[1][1], 3.0f, &mBlend);
    }
    else
    {
      g.PathCircle(data[1][0], data[1][1], 3);
      g.PathFill(IPattern::CreateRadialGradient(data[1][0], data[1][1], 4.0f,
                                                {{GetColor(mMouseIsOver ? kX3 : kX1), 0.f},
                                                 {GetColor(mMouseIsOver ? kX3 : kX1), 0.8f},
                                                 {COLOR_TRANSPARENT, 1.0f}}),
                 {}, &mBlend);
      g.DrawCircle(COLOR_BLACK.WithOpacity(0.5f), data[1][0], data[1][1], 3, &mBlend);
    }
  }

private:
  IBitmap mBitmap;
  std::unique_ptr<ISVG> mSVG;
  bool mUseSVG = false;
  bool mDrawIndicatorTrack = true;
  bool mUseDarkIndicatorDot = false;
  float mKnobScale = 1.0f;
  float mLabelYOffset = 0.0f;
  float mValueYOffset = 0.0f;
};

class NAMAmpBitmapKnobControl : public IVKnobControl
{
public:
  NAMAmpBitmapKnobControl(const IRECT& bounds,
                         int paramIdx,
                         const char* label,
                         const IVStyle& style,
                         const std::array<IBitmap, 3>& knobBitmaps,
                         const std::array<IBitmap, 3>& backgroundBitmaps,
                         int initialAmpIndex,
                         float knobScale = 1.0f,
                         float labelYOffset = 0.0f,
                         float valueYOffset = 0.0f)
  : IVKnobControl(bounds, paramIdx, label, style, true)
  , mKnobBitmaps(knobBitmaps)
  , mBackgroundBitmaps(backgroundBitmaps)
  , mAmpIndex(std::clamp(initialAmpIndex, 0, 2))
  , mKnobScale(knobScale)
  , mLabelYOffset(labelYOffset)
  , mValueYOffset(valueYOffset)
  {
  }

  void SetAmpStyle(int ampIndex)
  {
    const int clampedIndex = std::clamp(ampIndex, 0, 2);
    if (mAmpIndex == clampedIndex)
      return;

    mAmpIndex = clampedIndex;
    SetDirty(false);
  }

  void OnRescale() override
  {
    for (auto& bitmap : mKnobBitmaps)
      if (bitmap.IsValid())
        bitmap = GetUI()->GetScaledBitmap(bitmap);
    for (auto& bitmap : mBackgroundBitmaps)
      if (bitmap.IsValid())
        bitmap = GetUI()->GetScaledBitmap(bitmap);
  }

  void OnResize() override
  {
    IVKnobControl::OnResize();
    if (mLabelYOffset != 0.0f)
      mLabelBounds.Translate(0.0f, mLabelYOffset);
    if (mValueYOffset != 0.0f)
      mValueBounds.Translate(0.0f, mValueYOffset);
  }

  void DrawWidget(IGraphics& g) override
  {
    const IBitmap& knobBitmap = mKnobBitmaps[static_cast<size_t>(mAmpIndex)];
    const IBitmap& backgroundBitmap = mBackgroundBitmaps[static_cast<size_t>(mAmpIndex)];
    if (!knobBitmap.IsValid() && !backgroundBitmap.IsValid())
      return;

    const float sourceW = backgroundBitmap.IsValid() ? static_cast<float>(backgroundBitmap.W()) : static_cast<float>(knobBitmap.W());
    const float sourceH = backgroundBitmap.IsValid() ? static_cast<float>(backgroundBitmap.H()) : static_cast<float>(knobBitmap.H());
    const float knobW = sourceW * mKnobScale;
    const float knobH = sourceH * mKnobScale;
    const IRECT knobBounds = mWidgetBounds.GetCentredInside(knobW, knobH);
    const IBlend bitmapBlend(EBlend::Default, IsDisabled() ? 0.45f : 1.0f);

    if (backgroundBitmap.IsValid())
      g.DrawFittedBitmap(backgroundBitmap, knobBounds, &bitmapBlend);

    if (!knobBitmap.IsValid())
      return;

    const double angle = -130.0 + GetValue() * 260.0;
    g.StartLayer(this, knobBounds);
    g.DrawFittedBitmap(knobBitmap, knobBounds, &bitmapBlend);
    auto layer = g.EndLayer();
    g.DrawRotatedLayer(layer, angle);
  }

private:
  std::array<IBitmap, 3> mKnobBitmaps;
  std::array<IBitmap, 3> mBackgroundBitmaps;
  int mAmpIndex = 0;
  float mKnobScale = 1.0f;
  float mLabelYOffset = 0.0f;
  float mValueYOffset = 0.0f;
};

class NAMDeactivatableKnobControl : public NAMKnobControl
{
public:
  NAMDeactivatableKnobControl(const IRECT& bounds, int valueParamIdx, int activeParamIdx, const char* label, const IVStyle& style,
                              const IBitmap& bitmap, bool drawIndicatorTrack = true, bool useDarkIndicatorDot = false,
                              float knobScale = 1.0f, float labelYOffset = 0.0f, float valueYOffset = 0.0f)
  : NAMKnobControl(bounds,
                   kNoParameter,
                   label,
                   style,
                   bitmap,
                   drawIndicatorTrack,
                   useDarkIndicatorDot,
                   knobScale,
                   labelYOffset,
                   valueYOffset)
  , mValueParamIdx(valueParamIdx)
  , mActiveParamIdx(activeParamIdx)
  {
  }

  NAMDeactivatableKnobControl(const IRECT& bounds, int valueParamIdx, int activeParamIdx, const char* label, const IVStyle& style,
                              const ISVG& svg, bool drawIndicatorTrack = true, bool useDarkIndicatorDot = false,
                              float knobScale = 1.0f, float labelYOffset = 0.0f, float valueYOffset = 0.0f)
  : NAMKnobControl(bounds,
                   kNoParameter,
                   label,
                   style,
                   svg,
                   drawIndicatorTrack,
                   useDarkIndicatorDot,
                   knobScale,
                   labelYOffset,
                   valueYOffset)
  , mValueParamIdx(valueParamIdx)
  , mActiveParamIdx(activeParamIdx)
  {
  }

  void Draw(IGraphics& g) override
  {
    _SyncValueFromDelegate();
    NAMKnobControl::Draw(g);

    if (!_IsActive())
    {
      const IRECT dimRect = mWidgetBounds.GetCentredInside(mWidgetBounds.W(), mWidgetBounds.W()).GetScaledAboutCentre(0.92f);
      g.FillEllipse(COLOR_BLACK.WithOpacity(0.42f), dimRect, &mBlend);
    }
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    if (IsDisabled())
      return;
    if (mod.C || mod.R)
    {
      _ToggleActive();
      SetDirty(false);
      return;
    }
    _SyncValueFromDelegate();
    if (auto* pDelegate = GetDelegate())
      pDelegate->BeginInformHostOfParamChangeFromUI(mValueParamIdx);
    mInformingHost = true;
    NAMKnobControl::OnMouseDown(x, y, mod);
  }

  void OnMouseDrag(float x, float y, float dX, float dY, const IMouseMod& mod) override
  {
    NAMKnobControl::OnMouseDrag(x, y, dX, dY, mod);
    _SendValueToDelegate();
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float d) override
  {
    if (IsDisabled())
      return;

    _SyncValueFromDelegate();
    if (auto* pDelegate = GetDelegate())
      pDelegate->BeginInformHostOfParamChangeFromUI(mValueParamIdx);
    NAMKnobControl::OnMouseWheel(x, y, mod, d);
    _SendValueToDelegate();
    if (auto* pDelegate = GetDelegate())
      pDelegate->EndInformHostOfParamChangeFromUI(mValueParamIdx);
    SetDirty(false);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    NAMKnobControl::OnMouseUp(x, y, mod);
    _SendValueToDelegate();
    if (mInformingHost)
    {
      if (auto* pDelegate = GetDelegate())
        pDelegate->EndInformHostOfParamChangeFromUI(mValueParamIdx);
      mInformingHost = false;
    }
  }

private:
  void _SyncValueFromDelegate()
  {
    if (const auto* pDelegate = GetDelegate())
      if (const auto* pParam = pDelegate->GetParam(mValueParamIdx))
        SetValue(pParam->GetNormalized());
  }

  void _SendValueToDelegate()
  {
    if (auto* pDelegate = GetDelegate())
      pDelegate->SendParameterValueFromUI(mValueParamIdx, GetValue());
  }

  bool _IsActive()
  {
    if (const auto* pDelegate = GetDelegate())
      if (const auto* pParam = pDelegate->GetParam(mActiveParamIdx))
        return pParam->Bool();
    return true;
  }

  void _ToggleActive()
  {
    if (auto* pDelegate = GetDelegate())
    {
      if (const auto* pParam = pDelegate->GetParam(mActiveParamIdx))
      {
        pDelegate->BeginInformHostOfParamChangeFromUI(mActiveParamIdx);
        pDelegate->SendParameterValueFromUI(mActiveParamIdx, pParam->Bool() ? 0.0 : 1.0);
        pDelegate->EndInformHostOfParamChangeFromUI(mActiveParamIdx);
      }
    }
  }

  int mValueParamIdx = kNoParameter;
  int mActiveParamIdx = kNoParameter;
  bool mInformingHost = false;
};

class NAMPedalKnobControl : public IVKnobControl
{
public:
  NAMPedalKnobControl(const IRECT& bounds, int paramIdx, const char* label, const IVStyle& style, const IBitmap& knobBitmap,
                      const IBitmap& shadowBitmap, float knobScale = 1.0f, float labelYOffset = 0.0f,
                      float valueYOffset = 0.0f, bool showValueWhileDragging = true)
  : IVKnobControl(bounds, paramIdx, label, style, true)
  , mKnobBitmap(knobBitmap)
  , mShadowBitmap(shadowBitmap)
  , mKnobScale(knobScale)
  , mLabelYOffset(labelYOffset)
  , mValueYOffset(valueYOffset)
  , mShowValueWhileDraggingEnabled(showValueWhileDragging)
  {
  }

  void OnRescale() override
  {
    mKnobBitmap = GetUI()->GetScaledBitmap(mKnobBitmap);
    mShadowBitmap = GetUI()->GetScaledBitmap(mShadowBitmap);
  }

  void OnResize() override
  {
    IVKnobControl::OnResize();
    if (mLabelYOffset != 0.0f)
      mLabelBounds.Translate(0.0f, mLabelYOffset);
    const float knobW = mKnobBitmap.IsValid() ? static_cast<float>(mKnobBitmap.W()) * mKnobScale : mWidgetBounds.W();
    const float knobH = mKnobBitmap.IsValid() ? static_cast<float>(mKnobBitmap.H()) * mKnobScale : mWidgetBounds.H();
    const IRECT knobBounds = mWidgetBounds.GetCentredInside(knobW, knobH);
    mValueBounds = knobBounds.GetCentredInside(knobW * 0.74f, 20.0f);
    if (mValueYOffset != 0.0f)
      mValueBounds.Translate(0.0f, mValueYOffset);
  }

  void Draw(IGraphics& g) override
  {
    DrawBackground(g, mRECT);
    DrawLabel(g);
    DrawWidget(g);
    if (mShowValueWhileDragging)
      DrawValue(g, false);
  }

  void OnMouseDown(float x, float y, const IMouseMod& mod) override
  {
    IKnobControlBase::OnMouseDown(x, y, mod);
    mShowValueWhileDragging = mShowValueWhileDraggingEnabled;
    SetDirty(false);
  }

  void OnMouseUp(float x, float y, const IMouseMod& mod) override
  {
    IKnobControlBase::OnMouseUp(x, y, mod);
    mShowValueWhileDragging = false;
    SetDirty(true);
  }

  void OnMouseOut() override
  {
    mShowValueWhileDragging = false;
    IVKnobControl::OnMouseOut();
  }

  void DrawWidget(IGraphics& g) override
  {
    if (!mKnobBitmap.IsValid())
      return;

    const float knobW = static_cast<float>(mKnobBitmap.W()) * mKnobScale;
    const float knobH = static_cast<float>(mKnobBitmap.H()) * mKnobScale;
    const IRECT knobBounds = mWidgetBounds.GetCentredInside(knobW, knobH);
    if (mShadowBitmap.IsValid())
      g.DrawFittedBitmap(mShadowBitmap, knobBounds, &mBlend);

    const double angle = -130.0 + GetValue() * 260.0;
    // Draw the knob to a layer fitted to scaled bounds, then rotate that layer.
    g.StartLayer(this, knobBounds);
    g.DrawFittedBitmap(mKnobBitmap, knobBounds, &mBlend);
    auto layer = g.EndLayer();
    g.DrawRotatedLayer(layer, angle);
  }

private:
  IBitmap mKnobBitmap;
  IBitmap mShadowBitmap;
  float mKnobScale = 1.0f;
  float mLabelYOffset = 0.0f;
  float mValueYOffset = 0.0f;
  bool mShowValueWhileDraggingEnabled = true;
  bool mShowValueWhileDragging = false;
};

class NAMDelayTimeKnobControl : public NAMPedalKnobControl
{
public:
  using NAMPedalKnobControl::NAMPedalKnobControl;

  void OnInit() override
  {
    NAMPedalKnobControl::OnInit();
    _UpdateSyncValueReadout();
  }

  void SetDirty(bool push, int valIdx = kNoValIdx) override
  {
    NAMPedalKnobControl::SetDirty(push, valIdx);
    _UpdateSyncValueReadout();
  }

private:
  bool _IsSyncMode()
  {
    auto* pDelegate = GetDelegate();
    if (pDelegate == nullptr)
      return false;
    const auto* pModeParam = pDelegate->GetParam(kFXDelayTimeMode);
    if (pModeParam == nullptr)
      return false;
    return pModeParam->Int() == 0;
  }

  static int _GetSyncDivisionIndex(double normalizedValue)
  {
    const auto& labels = _SyncDivisionLabels();
    const double normalized = std::clamp(normalizedValue, 0.0, 1.0);
    const int maxIndex = static_cast<int>(labels.size()) - 1;
    return static_cast<int>(std::llround(normalized * static_cast<double>(maxIndex)));
  }

  void _UpdateSyncValueReadout()
  {
    if (!_IsSyncMode())
      return;
    const auto& labels = _SyncDivisionLabels();
    const int idx = _GetSyncDivisionIndex(GetValue());
    if (idx >= 0 && idx < static_cast<int>(labels.size()))
      mValueStr.Set(labels[static_cast<size_t>(idx)]);
  }

  static const std::array<const char*, 14>& _SyncDivisionLabels()
  {
    static const std::array<const char*, 14> labels = {
      "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D", "1/8",
      "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/1"
    };
    return labels;
  }
};

class NAMFXDelayDigitalDisplayControl : public IControl
{
public:
  explicit NAMFXDelayDigitalDisplayControl(const IRECT& bounds)
  : IControl(bounds)
  {
    mIgnoreMouse = true;
    mTimeReadout.Set("--");
    mMixReadout.Set("--");
    mFeedbackReadout.Set("--");
  }

  void OnInit() override { _RefreshReadouts(true); }

  void OnGUIIdle() override { _RefreshReadouts(false); }

  void Draw(IGraphics& g) override
  {
    _RefreshReadouts(false);

    bool delayActive = true;
    if (const auto* pDelegate = GetDelegate())
      if (const auto* pDelayActiveParam = pDelegate->GetParam(kFXDelayActive))
        delayActive = pDelayActiveParam->Bool();

    const float displayOpacity = delayActive ? 1.0f : 0.22f;
    const float roundness = 5.0f;
    g.FillRoundRect(IColor(delayActive ? 0 : 130, 10, 12, 16), mRECT, roundness, &mBlend);

    const auto content = mRECT.GetPadded(-6.0f);
    const auto row1 = content.SubRectVertical(3, 0);
    const auto row2 = content.SubRectVertical(3, 1);
    const auto row3 = content.SubRectVertical(3, 2);
    const float rowH = static_cast<float>(row1.H());
    const float labelSize = std::clamp(rowH * 0.34f, 9.0f, 16.0f);
    const float valueSize = std::clamp(rowH * 0.52f, 12.0f, 24.0f);
    const IText labelText(labelSize,
                          COLOR_GRAY.WithOpacity(0.94f * displayOpacity),
                          "Roboto-Regular",
                          EAlign::Near,
                          EVAlign::Middle);
    const IText valueText(valueSize,
                          IColor(255, 180, 255, 200).WithOpacity(displayOpacity),
                          "Michroma-Regular",
                          EAlign::Far,
                          EVAlign::Middle);

    g.DrawText(labelText, "MIX", row1, &mBlend);
    g.DrawText(valueText, mMixReadout.Get(), row1, &mBlend);
    g.DrawText(labelText, "FDBK", row2, &mBlend);
    g.DrawText(valueText, mFeedbackReadout.Get(), row2, &mBlend);
    g.DrawText(labelText, "TIME", row3, &mBlend);
    g.DrawText(valueText, mTimeReadout.Get(), row3, &mBlend);
  }

private:
  void _RefreshReadouts(const bool forceDirty)
  {
    const auto* pDelegate = GetDelegate();
    if (pDelegate == nullptr)
      return;

    const auto* pModeParam = pDelegate->GetParam(kFXDelayTimeMode);
    const auto* pTimeParam = pDelegate->GetParam(kFXDelayTimeMs);
    const auto* pMixParam = pDelegate->GetParam(kFXDelayMix);
    const auto* pFeedbackParam = pDelegate->GetParam(kFXDelayFeedback);
    if (pModeParam == nullptr || pTimeParam == nullptr || pMixParam == nullptr || pFeedbackParam == nullptr)
      return;

    WDL_String timeReadout;
    if (pModeParam->Int() == 0)
    {
      const auto& labels = _SyncDivisionLabels();
      const double normalized = std::clamp(pTimeParam->GetNormalized(), 0.0, 1.0);
      const int idx =
        static_cast<int>(std::llround(normalized * static_cast<double>(static_cast<int>(labels.size()) - 1)));
      const int safeIdx = std::clamp(idx, 0, static_cast<int>(labels.size()) - 1);
      timeReadout.Set(labels[static_cast<size_t>(safeIdx)]);
    }
    else
    {
      timeReadout.SetFormatted(24, "%0.0f ms", pTimeParam->Value());
    }

    WDL_String mixReadout;
    mixReadout.SetFormatted(24, "%0.1f %%", pMixParam->Value());
    WDL_String feedbackReadout;
    feedbackReadout.SetFormatted(24, "%0.1f %%", pFeedbackParam->Value());

    const bool changed = std::strcmp(mTimeReadout.Get(), timeReadout.Get()) != 0
      || std::strcmp(mMixReadout.Get(), mixReadout.Get()) != 0
      || std::strcmp(mFeedbackReadout.Get(), feedbackReadout.Get()) != 0;
    if (!changed && !forceDirty)
      return;

    mTimeReadout.Set(timeReadout.Get());
    mMixReadout.Set(mixReadout.Get());
    mFeedbackReadout.Set(feedbackReadout.Get());
    SetDirty(false);
  }

  static const std::array<const char*, 14>& _SyncDivisionLabels()
  {
    static const std::array<const char*, 14> labels = {
      "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D", "1/8",
      "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2", "1/1"
    };
    return labels;
  }

  WDL_String mTimeReadout;
  WDL_String mMixReadout;
  WDL_String mFeedbackReadout;
};

class NAMSwitchControl : public IVSlideSwitchControl, public IBitmapBase
{
public:
  NAMSwitchControl(const IRECT& bounds, int paramIdx, const char* label, const IVStyle& style, IBitmap bitmap)
  : IVSlideSwitchControl(bounds, paramIdx, label,
                         style.WithRoundness(0.666f)
                           .WithShowValue(false)
                           .WithEmboss(true)
                           .WithShadowOffset(1.5f)
                           .WithDrawShadows(false)
                           .WithColor(kFR, COLOR_BLACK)
                           .WithFrameThickness(0.5f)
                           .WithWidgetFrac(0.5f)
                           .WithLabelOrientation(EOrientation::South))
  , IBitmapBase(bitmap)
  {
  }

  void DrawWidget(IGraphics& g) override
  {
    DrawTrack(g, mWidgetBounds);
    DrawHandle(g, mHandleBounds);
  }

  void DrawTrack(IGraphics& g, const IRECT& bounds) override
  {
    IRECT handleBounds = GetAdjustedHandleBounds(bounds);
    handleBounds = IRECT(handleBounds.L, handleBounds.T, handleBounds.R, handleBounds.T + mBitmap.H());
    IRECT centreBounds = handleBounds.GetPadded(-mStyle.shadowOffset);
    IRECT shadowBounds = handleBounds.GetTranslated(mStyle.shadowOffset, mStyle.shadowOffset);
    //    const float contrast = mDisabled ? -GRAYED_ALPHA : 0.f;
    float cR = 7.f;
    const float tlr = cR;
    const float trr = cR;
    const float blr = cR;
    const float brr = cR;

    // outer shadow
    if (mStyle.drawShadows)
      g.FillRoundRect(GetColor(kSH), shadowBounds, tlr, trr, blr, brr, &mBlend);

    // Embossed style unpressed
    if (mStyle.emboss)
    {
      // Positive light
      g.FillRoundRect(GetColor(kPR), handleBounds, tlr, trr, blr, brr /*, &blend*/);

      // Negative light
      g.FillRoundRect(GetColor(kSH), shadowBounds, tlr, trr, blr, brr /*, &blend*/);

      // Fill in foreground
      g.FillRoundRect(GetValue() > 0.5 ? GetColor(kX1) : COLOR_BLACK, centreBounds, tlr, trr, blr, brr, &mBlend);

      // Shade when hovered
      if (mMouseIsOver)
        g.FillRoundRect(GetColor(kHL), centreBounds, tlr, trr, blr, brr, &mBlend);
    }
    else
    {
      g.FillRoundRect(GetValue() > 0.5 ? GetColor(kX1) : COLOR_BLACK, handleBounds, tlr, trr, blr, brr /*, &blend*/);

      // Shade when hovered
      if (mMouseIsOver)
        g.FillRoundRect(GetColor(kHL), handleBounds, tlr, trr, blr, brr, &mBlend);
    }

    if (mStyle.drawFrame)
      g.DrawRoundRect(GetColor(kFR), handleBounds, tlr, trr, blr, brr, &mBlend, mStyle.frameThickness);
  }

  void DrawHandle(IGraphics& g, const IRECT& filledArea) override
  {
    IRECT r;
    if (GetSelectedIdx() == 0)
    {
      r = filledArea.GetFromLeft(mBitmap.W());
    }
    else
    {
      r = filledArea.GetFromRight(mBitmap.W());
    }

    g.DrawBitmap(mBitmap, r, 0, 0, nullptr);
  }
};

class NAMReadOnlyCheckboxControl : public IControl
{
public:
  NAMReadOnlyCheckboxControl(const IRECT& bounds, const char* label, const IVStyle& style)
  : IControl(bounds)
  , mLabel(label != nullptr ? label : "")
  , mLabelText(style.labelText.WithAlign(EAlign::Near))
  {
    mIgnoreMouse = true;
  }

  void SetChecked(const bool checked)
  {
    if (mChecked == checked)
      return;
    mChecked = checked;
    SetDirty(false);
  }

  bool IsChecked() const { return mChecked; }

  void Draw(IGraphics& g) override
  {
    const float boxSize = std::min(11.0f, std::max(8.0f, mRECT.H() - 2.0f));
    const float boxTop = mRECT.MH() - 0.5f * boxSize;
    const IRECT boxBounds(mRECT.L + 2.0f, boxTop, mRECT.L + 2.0f + boxSize, boxTop + boxSize);

    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.7f), boxBounds, 2.0f, &mBlend);
    g.DrawRoundRect(PluginColors::NAM_THEMECOLOR.WithOpacity(0.8f), boxBounds, 2.0f, &mBlend, 1.0f);

    if (mChecked)
    {
      const float x1 = boxBounds.L + 2.0f;
      const float y1 = boxBounds.MH() + 0.5f;
      const float x2 = boxBounds.L + 4.5f;
      const float y2 = boxBounds.B - 2.5f;
      const float x3 = boxBounds.R - 2.0f;
      const float y3 = boxBounds.T + 2.5f;
      g.DrawLine(PluginColors::NAM_THEMECOLOR, x1, y1, x2, y2, &mBlend, 1.6f);
      g.DrawLine(PluginColors::NAM_THEMECOLOR, x2, y2, x3, y3, &mBlend, 1.6f);
    }

    const IRECT textBounds(boxBounds.R + 5.0f, mRECT.T, mRECT.R, mRECT.B);
    g.DrawText(mLabelText, mLabel.c_str(), textBounds, &mBlend);
  }

private:
  std::string mLabel;
  IText mLabelText;
  bool mChecked = false;
};

class NAMBlendSliderControl : public IVSliderControl
{
public:
  NAMBlendSliderControl(const IRECT& bounds, int paramIdx, const IVStyle& style)
  : IVSliderControl(bounds, paramIdx, "CAB BLEND", style.WithShowValue(false), false, EDirection::Horizontal,
                    DEFAULT_GEARING, 6.0f, 3.0f, true)
  {
  }

  void OnResize() override
  {
    IVSliderControl::OnResize();
    // Keep the label position unchanged, but lift slider track/handle slightly.
    constexpr float kSliderYOffset = -9.0f;
    mWidgetBounds.Translate(0.0f, kSliderYOffset);
    mTrackBounds.Translate(0.0f, kSliderYOffset);
  }

  void DrawTrack(IGraphics& g, const IRECT& filledArea) override
  {
    const float cr = GetRoundedCornerRadius(mTrackBounds);

    // Draw a neutral track only (no progress-fill region).
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.65f), mTrackBounds, cr, &mBlend);
    if (mStyle.drawFrame)
      g.DrawRoundRect(GetColor(kFR), mTrackBounds, cr, &mBlend, mStyle.frameThickness);

    // Visual center marker for the 50/50 blend point.
    const float centerX = mTrackBounds.MW();
    g.DrawLine(COLOR_WHITE.WithOpacity(0.75f), centerX, mTrackBounds.T - 3.0f, centerX, mTrackBounds.B + 3.0f,
               &mBlend, 1.0f);
  }
};

class NAMEQFaderSliderControl : public IVSliderControl
{
public:
  NAMEQFaderSliderControl(const IRECT& bounds, int paramIdx, const IVStyle& style, const IBitmap& knobBitmap)
  : IVSliderControl(bounds, paramIdx, "", style.WithShowLabel(false).WithShowValue(false), false, EDirection::Vertical,
                    DEFAULT_GEARING, 6.0f, 3.0f, true)
  , mKnobBitmap(knobBitmap)
  {
  }

  void OnRescale() override
  {
    IVSliderControl::OnRescale();
    if (mKnobBitmap.IsValid())
      mKnobBitmap = GetUI()->GetScaledBitmap(mKnobBitmap);
  }

  void DrawTrack(IGraphics&, const IRECT&) override {}

  void DrawHandle(IGraphics& g, const IRECT& bounds) override
  {
    if (!mKnobBitmap.IsValid())
      return;

    // Keep the slider body transparent and draw only a compact handle bitmap.
    const float sourceW = static_cast<float>(mKnobBitmap.W());
    const float sourceH = static_cast<float>(mKnobBitmap.H());
    const float ratio = sourceW > 0.0f ? (sourceH / sourceW) : 1.0f;

    const float handleW = std::max(bounds.W() * 1.35f, 10.0f);
    const float handleH = std::min(handleW * ratio, static_cast<float>(mWidgetBounds.H()) * 0.28f);
    IRECT knobBounds(bounds.MW() - 0.5f * handleW, bounds.MH() - 0.5f * handleH, bounds.MW() + 0.5f * handleW,
                     bounds.MH() + 0.5f * handleH);
    if (knobBounds.T < mWidgetBounds.T)
      knobBounds.Translate(0.0f, static_cast<float>(mWidgetBounds.T) - knobBounds.T);
    if (knobBounds.B > mWidgetBounds.B)
      knobBounds.Translate(0.0f, static_cast<float>(mWidgetBounds.B) - knobBounds.B);
    const IBlend knobBlend(EBlend::Default, IsDisabled() ? 0.45f : 1.0f);
    g.DrawFittedBitmap(mKnobBitmap, knobBounds, &knobBlend);
  }

private:
  IBitmap mKnobBitmap;
};

class NAMTunerMonitorControl : public IVTabSwitchControl
{
public:
  NAMTunerMonitorControl(const IRECT& bounds, int paramIdx, const IVStyle& style)
  : IVTabSwitchControl(bounds, paramIdx, {"MUTE", "BYP", "FULL"}, "",
                       style.WithShowLabel(false)
                         .WithShowValue(false)
                         .WithDrawShadows(false)
                         .WithDrawFrame(false)
                         .WithEmboss(false)
                         .WithRoundness(0.30f),
                       EVShape::Rectangle, EDirection::Horizontal)
  {
  }

  void DrawWidget(IGraphics& g) override
  {
    const int selected = GetSelectedIdx();
    static const char* kLabels[3] = {"MUTE", "BYP", "ON"};

    for (int i = 0; i < mNumStates; ++i)
    {
      IRECT tabBounds = mButtons.Get()[i].GetPadded(-0.5f);
      const bool pressed = (i == selected);
      const bool disabled = IsDisabled() || GetStateDisabled(i);
      const bool mouseOver = (mMouseOverButton == i);

      IColor fillColor = pressed ? _GetActiveColor(i) : COLOR_BLACK.WithOpacity(0.84f);
      if (disabled)
        fillColor = fillColor.WithOpacity(0.45f);

      g.FillRoundRect(fillColor, tabBounds, 4.0f, &mBlend);
      g.DrawRoundRect(COLOR_WHITE.WithOpacity(0.45f), tabBounds, 4.0f, &mBlend, 1.0f);

      if (!pressed && mouseOver)
        g.FillRoundRect(COLOR_WHITE.WithOpacity(0.08f), tabBounds, 4.0f, &mBlend);

      const IColor textColor = pressed ? COLOR_BLACK.WithOpacity(0.92f) : COLOR_WHITE.WithOpacity(0.92f);
      const IText tabText(15.0f, textColor, "Roboto-Regular", EAlign::Center, EVAlign::Middle);
      const char* label = (i >= 0 && i < 3) ? kLabels[i] : "";
      g.DrawText(tabText, label, tabBounds, &mBlend);
    }
  }

private:
  static IColor _GetActiveColor(const int idx)
  {
    switch (idx)
    {
      case 0: return {220, 220, 64, 64}; // MUTE: red, translucent
      case 1: return {220, 210, 170, 48}; // BYP: yellow, translucent
      case 2: return {220, 72, 190, 88}; // FULL: green, translucent
      default: return PluginColors::NAM_THEMECOLOR.WithOpacity(0.8f);
    }
  }
};

class NAMTunerDisplayControl : public IControl
{
public:
  NAMTunerDisplayControl(const IRECT& bounds)
  : IControl(bounds)
  {
    mIgnoreMouse = true;
  }

  void SetTunerState(const bool active, const bool hasPitch, const int midiNote, const float cents)
  {
    const float clampedCents = static_cast<float>(std::clamp(static_cast<double>(cents), -50.0, 50.0));
    mActive = active;
    mHasPitch = hasPitch;
    if (!mActive || !mHasPitch)
    {
      mDisplayedMidiNote = -1;
      mPendingMidiNote = -1;
      mPendingMidiCount = 0;
      mNeedleHoldFrames = 0;
      mTargetCents = 0.0f;
      mDisplayCents = 0.0f;
      SetDirty(false);
      return;
    }

    // Only accept a new note after a few consistent updates, to ignore pluck transients.
    if (mDisplayedMidiNote < 0)
    {
      mDisplayedMidiNote = midiNote;
      mPendingMidiNote = -1;
      mPendingMidiCount = 0;
      mNeedleHoldFrames = 2;
    }
    else if (midiNote != mDisplayedMidiNote)
    {
      if (mPendingMidiNote == midiNote)
        ++mPendingMidiCount;
      else
      {
        mPendingMidiNote = midiNote;
        mPendingMidiCount = 1;
      }

      if (mPendingMidiCount >= 3)
      {
        mDisplayedMidiNote = midiNote;
        mPendingMidiNote = -1;
        mPendingMidiCount = 0;
        mNeedleHoldFrames = 3;
      }
      else
      {
        // Keep current display until the new note has settled.
        mNeedleHoldFrames = std::max(mNeedleHoldFrames, 2);
      }
    }
    else
    {
      mPendingMidiNote = -1;
      mPendingMidiCount = 0;
    }

    mTargetCents = clampedCents;
    if (mNeedleHoldFrames > 0)
    {
      --mNeedleHoldFrames;
    }
    else
    {
      const float delta = mTargetCents - mDisplayCents;
      const float alpha = std::fabs(delta) > 14.0f ? 0.50f : 0.24f;
      mDisplayCents += alpha * delta;
    }

    SetDirty(false);
  }

  void Draw(IGraphics& g) override
  {
    const IRECT panel = mRECT.GetPadded(-1.0f);
    g.FillRoundRect(COLOR_BLACK.WithOpacity(0.86f), panel, 8.0f, &mBlend);
    g.DrawRoundRect(PluginColors::NAM_THEMECOLOR.WithOpacity(0.75f), panel, 8.0f, &mBlend, 1.0f);

    const float sidePad = std::max(20.0f, panel.W() * 0.07f);
    const float lineY = panel.B - std::max(22.0f, panel.H() * 0.23f);
    const float lineL = panel.L + sidePad;
    const float lineR = panel.R - sidePad;
    const float centerX = 0.5f * (lineL + lineR);
    const float xRange = 0.5f * (lineR - lineL);
    const float baselineThickness = 2.6f;

    // Baseline + "in tune" zone for quick readability.
    g.DrawLine(COLOR_WHITE.WithOpacity(0.42f), lineL, lineY, lineR, lineY, &mBlend, baselineThickness);
    const float inTuneHalfWidth = (5.0f / 50.0f) * xRange;
    g.DrawLine(IColor(220, 100, 220, 130), centerX - inTuneHalfWidth, lineY, centerX + inTuneHalfWidth, lineY, &mBlend,
               baselineThickness + 1.2f);

    // Readable ticks at -50, -25, 0, +25, +50 cents.
    const float tickNorms[] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
    for (const float norm : tickNorms)
    {
      const float x = centerX + norm * xRange;
      const float tickHalfHeight = (norm == 0.0f) ? 13.0f : 9.0f;
      const IColor tickColor =
        (norm == 0.0f) ? PluginColors::NAM_THEMECOLOR.WithOpacity(0.95f) : COLOR_WHITE.WithOpacity(0.38f);
      g.DrawLine(tickColor, x, lineY - tickHalfHeight, x, lineY + tickHalfHeight, &mBlend, (norm == 0.0f) ? 2.0f : 1.2f);
    }

    const float noteFontSize = std::max(30.0f, panel.H() * 0.40f);
    IText primaryText(noteFontSize, COLOR_WHITE, "Michroma-Regular", EAlign::Center, EVAlign::Middle);
    IText secondaryText(12.0f, COLOR_WHITE.WithOpacity(0.85f), "Roboto-Regular", EAlign::Center, EVAlign::Middle);
    if (!mActive)
    {
      g.DrawText(secondaryText, "TUNER OFF", panel.GetFromTop(30.0f));
      return;
    }
    if (!mHasPitch)
    {
      return;
    }

    g.DrawText(primaryText, _GetNoteNameNoOctave(mDisplayedMidiNote), panel.GetFromTop(std::max(44.0f, panel.H() * 0.46f)));

    const float needleX = centerX + (mDisplayCents / 50.0f) * xRange;
    const IColor needleColor =
      (std::fabs(mDisplayCents) <= 4.0f) ? IColor(255, 96, 230, 120) : PluginColors::NAM_THEMECOLOR;
    g.DrawLine(needleColor, needleX, lineY - 18.0f, needleX, lineY + 8.0f, &mBlend, 3.2f);
    g.FillCircle(needleColor.WithOpacity(0.9f), needleX, lineY, 3.6f, &mBlend);
  }

private:
  static const char* _GetNoteNameNoOctave(const int midiNote)
  {
    static const char* kNoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int idx = ((midiNote % 12) + 12) % 12;
    return kNoteNames[idx];
  }

  bool mActive = false;
  bool mHasPitch = false;
  int mDisplayedMidiNote = -1;
  int mPendingMidiNote = -1;
  int mPendingMidiCount = 0;
  int mNeedleHoldFrames = 0;
  float mTargetCents = 0.0f;
  float mDisplayCents = 0.0f;
};

class NAMFileNameControl : public IVButtonControl
{
public:
  NAMFileNameControl(const IRECT& bounds, const char* label, const IVStyle& style)
  : IVButtonControl(bounds, DefaultClickActionFunc, label, style)
  {
  }

  void SetLabelAndTooltip(const char* str)
  {
    SetLabelStr(str);
    SetTooltip(str);
  }

  void SetLabelAndTooltipEllipsizing(const WDL_String& fileName)
  {
    auto EllipsizeFilePath = [](const char* filePath, size_t prefixLength, size_t suffixLength, size_t maxLength) {
      const std::string ellipses = "...";
      assert(maxLength <= (prefixLength + suffixLength + ellipses.size()));
      std::string str{filePath};

      if (str.length() <= maxLength)
      {
        return str;
      }
      else
      {
        return str.substr(0, prefixLength) + ellipses + str.substr(str.length() - suffixLength);
      }
    };

    auto ellipsizedFileName = EllipsizeFilePath(fileName.get_filepart(), 22, 22, 45);
    SetLabelStr(ellipsizedFileName.c_str());
    SetTooltip(fileName.get_filepart());
  }
};

class NAMFileBrowserControl : public IDirBrowseControlBase
{
public:
  NAMFileBrowserControl(const IRECT& bounds, int clearMsgTag, const char* labelStr, const char* fileExtension,
                        IFileDialogCompletionHandlerFunc ch, const IVStyle& style, const ISVG& loadSVG,
                        const ISVG& clearSVG, const ISVG& leftSVG, const ISVG& rightSVG, const IBitmap& bitmap)
  : IDirBrowseControlBase(bounds, fileExtension, false, false)
  , mClearMsgTag(clearMsgTag)
  , mDefaultLabelStr(labelStr)
  , mCompletionHandlerFunc(ch)
  , mStyle(style.WithColor(kFG, COLOR_TRANSPARENT).WithDrawFrame(false))
  , mBitmap(bitmap)
  , mLoadSVG(loadSVG)
  , mClearSVG(clearSVG)
  , mLeftSVG(leftSVG)
  , mRightSVG(rightSVG)
  , mBrowserState(NAMBrowserState::Empty)
  {
    mIgnoreMouse = true;
  }

  void Draw(IGraphics& g) override { g.DrawFittedBitmap(mBitmap, mRECT); }

  void OnPopupMenuSelection(IPopupMenu* pSelectedMenu, int valIdx) override
  {
    if (pSelectedMenu)
    {
      IPopupMenu::Item* pItem = pSelectedMenu->GetChosenItem();

      if (pItem)
      {
        mSelectedItemIndex = mItems.Find(pItem);
        LoadFileAtCurrentIndex();
      }
    }
  }

  void OnAttached() override
  {
    auto prevFileFunc = [&](IControl* pCaller) {
      const auto nItems = NItems();
      if (nItems == 0)
        return;
      mSelectedItemIndex--;

      if (mSelectedItemIndex < 0)
        mSelectedItemIndex = nItems - 1;

      LoadFileAtCurrentIndex();
    };

    auto nextFileFunc = [&](IControl* pCaller) {
      const auto nItems = NItems();
      if (nItems == 0)
        return;
      mSelectedItemIndex++;

      if (mSelectedItemIndex >= nItems)
        mSelectedItemIndex = 0;

      LoadFileAtCurrentIndex();
    };

    auto loadFileFunc = [&](IControl* pCaller) {
      WDL_String fileName;
      WDL_String path;
      if (mLastDirectory.GetLength())
        path.Set(mLastDirectory.Get());
      else
        GetSelectedFileDirectory(path);
#ifdef NAM_PICK_DIRECTORY
      pCaller->GetUI()->PromptForDirectory(path, [&](const WDL_String& fileName, const WDL_String& path) {
        if (path.GetLength())
        {
          mLastDirectory.Set(path.Get());
          ClearPathList();
          AddPath(path.Get(), "");
          SetupMenu();
          SelectFirstFile();
          LoadFileAtCurrentIndex();
        }
      });
#else
      pCaller->GetUI()->PromptForFile(
        fileName, path, EFileAction::Open, mExtension.Get(), [&](const WDL_String& fileName, const WDL_String& path) {
          if (fileName.GetLength())
          {
            mLastDirectory.Set(path.Get());
            ClearPathList();
            AddPath(path.Get(), "");
            SetupMenu();
            SetSelectedFile(fileName.Get());
            LoadFileAtCurrentIndex();
          }
        });
#endif
    };

    auto clearFileFunc = [&](IControl* pCaller) {
      pCaller->GetDelegate()->SendArbitraryMsgFromUI(mClearMsgTag, this->GetTag());
      mFileNameControl->SetLabelAndTooltip(mDefaultLabelStr.Get());
      SetBrowserState(NAMBrowserState::Empty);
      // FIXME disabling output mode...
      //      pCaller->GetUI()->GetControlWithTag(kCtrlTagOutputMode)->SetDisabled(false);
    };

    auto chooseFileFunc = [&, loadFileFunc](IControl* pCaller) {
      if (std::string_view(pCaller->As<IVButtonControl>()->GetLabelStr()) == mDefaultLabelStr.Get())
      {
        loadFileFunc(pCaller);
      }
      else
      {
        CheckSelectedItem();

        if (!mMainMenu.HasSubMenus())
        {
          mMainMenu.SetChosenItemIdx(mSelectedItemIndex);
        }
        pCaller->GetUI()->CreatePopupMenu(*this, mMainMenu, pCaller->GetRECT());
      }
    };

    IRECT padded = mRECT.GetPadded(-6.f).GetHPadded(-2.f);
    const auto buttonWidth = padded.H();
    const auto loadFileButtonBounds = padded.ReduceFromLeft(buttonWidth);
    const auto clearButtonBounds = padded.ReduceFromRight(buttonWidth);
    const auto leftButtonBounds = padded.ReduceFromLeft(buttonWidth);
    const auto rightButtonBounds = padded.ReduceFromLeft(buttonWidth);
    const auto fileNameButtonBounds = padded;

    AddChildControl(new NAMSquareButtonControl(loadFileButtonBounds, DefaultClickActionFunc, mLoadSVG))
      ->SetAnimationEndActionFunction(loadFileFunc);
    AddChildControl(new NAMSquareButtonControl(leftButtonBounds, DefaultClickActionFunc, mLeftSVG))
      ->SetAnimationEndActionFunction(prevFileFunc);
    AddChildControl(new NAMSquareButtonControl(rightButtonBounds, DefaultClickActionFunc, mRightSVG))
      ->SetAnimationEndActionFunction(nextFileFunc);
    AddChildControl(mFileNameControl = new NAMFileNameControl(fileNameButtonBounds, mDefaultLabelStr.Get(), mStyle))
      ->SetAnimationEndActionFunction(chooseFileFunc);

    mClearButton = new NAMSquareButtonControl(clearButtonBounds, DefaultClickActionFunc, mClearSVG);
    mClearButton->SetAnimationEndActionFunction(clearFileFunc);
    AddChildControl(mClearButton);

    // initialize control visibility
    SetBrowserState(NAMBrowserState::Empty);
  }

  void LoadFileAtCurrentIndex()
  {
    if (mSelectedItemIndex > -1 && mSelectedItemIndex < NItems())
    {
      WDL_String fileName, path;
      GetSelectedFile(fileName);
      mFileNameControl->SetLabelAndTooltipEllipsizing(fileName);
      mCompletionHandlerFunc(fileName, path);
    }
  }

  void OnMsgFromDelegate(int msgTag, int dataSize, const void* pData) override
  {
    switch (msgTag)
    {
      case kMsgTagLoadFailed:
        // Honestly, not sure why I made a big stink of it before. Why not just say it failed and move on? :)
        {
          std::string label(std::string("(FAILED) ") + std::string(mFileNameControl->GetLabelStr()));
          mFileNameControl->SetLabelAndTooltip(label.c_str());
          SetBrowserState(NAMBrowserState::Empty);
        }
        break;
      case kMsgTagLoadedModel:
      case kMsgTagLoadedStompModel:
      case kMsgTagLoadedIRLeft:
      case kMsgTagLoadedIRRight:
      {
        WDL_String fileName, directory;
        fileName.Set(reinterpret_cast<const char*>(pData));
        directory.Set(reinterpret_cast<const char*>(pData));
        directory.remove_filepart(true);
        mLastDirectory.Set(directory.Get());

        ClearPathList();
        AddPath(directory.Get(), "");
        SetupMenu();
        SetSelectedFile(fileName.Get());
        mFileNameControl->SetLabelAndTooltipEllipsizing(fileName);
        SetBrowserState(NAMBrowserState::Loaded);
      }
      break;
      case kMsgTagClearModel:
      case kMsgTagClearStompModel:
      case kMsgTagClearIRLeft:
      case kMsgTagClearIRRight:
        mFileNameControl->SetLabelAndTooltip(mDefaultLabelStr.Get());
        SetBrowserState(NAMBrowserState::Empty);
        break;
      default: break;
    }
  }

  void RefreshBrowserStateVisibility() { SetBrowserState(mBrowserState); }

private:
  void SelectFirstFile() { mSelectedItemIndex = mFiles.GetSize() ? 0 : -1; }

  void GetSelectedFileDirectory(WDL_String& path)
  {
    GetSelectedFile(path);
    path.remove_filepart();
    return;
  }

  // set the state of the browser and the visibility of the clear button
  void SetBrowserState(NAMBrowserState newState)
  {
    mBrowserState = newState;
    if (mClearButton == nullptr)
      return;

    // If this browser (or its parent settings page) is hidden, keep action button hidden.
    const bool parentHidden = (GetParent() != nullptr) && GetParent()->IsHidden();
    if (IsHidden() || parentHidden)
    {
      mClearButton->Hide(true);
      return;
    }

    switch (mBrowserState)
    {
      case NAMBrowserState::Empty:
        mClearButton->Hide(true);
        break;
      case NAMBrowserState::Loaded:
        mClearButton->Hide(false);
        break;
    }
  }

  WDL_String mDefaultLabelStr;
  WDL_String mLastDirectory;
  IFileDialogCompletionHandlerFunc mCompletionHandlerFunc;
  NAMFileNameControl* mFileNameControl = nullptr;
  IVStyle mStyle;
  IBitmap mBitmap;
  ISVG mLoadSVG, mClearSVG, mLeftSVG, mRightSVG;
  int mClearMsgTag;
  NAMBrowserState mBrowserState;
  NAMSquareButtonControl* mClearButton = nullptr;
};

class NAMMeterControl : public IVPeakAvgMeterControl<>, public IBitmapBase
{
  static constexpr float KMeterMin = -70.0f;
  static constexpr float KMeterMax = -0.01f;

public:
  NAMMeterControl(const IRECT& bounds, const IBitmap& bitmap, const IVStyle& style)
  : IVPeakAvgMeterControl<>(bounds, "", style.WithShowValue(false).WithDrawFrame(false).WithWidgetFrac(0.8),
                            EDirection::Vertical, {}, 0, KMeterMin, KMeterMax, {})
  , IBitmapBase(bitmap)
  {
    SetPeakSize(1.0f);
  }

  void OnRescale() override { mBitmap = GetUI()->GetScaledBitmap(mBitmap); }

  virtual void OnResize() override
  {
    SetTargetRECT(MakeRects(mRECT));
    mWidgetBounds = mWidgetBounds.GetMidHPadded(5).GetVPadded(10);
    MakeTrackRects(mWidgetBounds);
    MakeStepRects(mWidgetBounds, mNSteps);
    SetDirty(false);
  }

  void DrawBackground(IGraphics& g, const IRECT& r) override
  {
    g.DrawFittedBitmap(mBitmap, r);
    // Subtle theme-colored frame to better define meter bounds.
    g.DrawRect(GetColor(kX1).WithOpacity(0.8f), r.GetPadded(-0.5f), &mBlend, 1.0f);
  }

  void DrawTrackHandle(IGraphics& g, const IRECT& r, int chIdx, bool aboveBaseValue) override
  {
    if (r.H() > 2)
      g.FillRect(GetColor(kX1), r, &mBlend);
  }

  void DrawPeak(IGraphics& g, const IRECT& r, int chIdx, bool aboveBaseValue) override
  {
    g.DrawGrid(COLOR_BLACK, mTrackBounds.Get()[chIdx], 10, 2);
    g.FillRect(GetColor(kX3), r, &mBlend);
  }
};

// Container where we can refer to children by names instead of indices
class IContainerBaseWithNamedChildren : public IContainerBase
{
public:
  IContainerBaseWithNamedChildren(const IRECT& bounds)
  : IContainerBase(bounds) {};
  ~IContainerBaseWithNamedChildren() = default;

protected:
  IControl* AddNamedChildControl(IControl* control, std::string name, int ctrlTag = kNoTag, const char* group = "")
  {
    // Make sure we haven't already used this name
    assert(mChildNameIndexMap.find(name) == mChildNameIndexMap.end());
    mChildNameIndexMap[name] = NChildren();
    return AddChildControl(control, ctrlTag, group);
  };

  IControl* GetNamedChild(std::string name)
  {
    const int index = mChildNameIndexMap[name];
    return GetChild(index);
  };


private:
  std::unordered_map<std::string, int> mChildNameIndexMap;
}; // class IContainerBaseWithNamedChildren


struct PossiblyKnownParameter
{
  bool known = false;
  double value = 0.0;
};

struct ModelInfo
{
  PossiblyKnownParameter sampleRate;
  PossiblyKnownParameter inputCalibrationLevel;
  PossiblyKnownParameter outputCalibrationLevel;
};

class ModelInfoControl : public IContainerBaseWithNamedChildren
{
public:
  ModelInfoControl(const IRECT& bounds, const IVStyle& style)
  : IContainerBaseWithNamedChildren(bounds)
  , mStyle(style) {};

  void ClearModelInfo()
  {
    static_cast<IVLabelControl*>(GetNamedChild(mControlNames.sampleRate))->SetStr("");
    mHasInfo = false;
  };

  void Hide(bool hide) override
  {
    // Don't show me unless I have info to show!
    IContainerBase::Hide(hide || (!mHasInfo));
  };

  void OnAttached() override
  {
    AddChildControl(new IVLabelControl(GetRECT().SubRectVertical(4, 0), "Model information:", mStyle));
    AddNamedChildControl(new IVLabelControl(GetRECT().SubRectVertical(4, 1), "", mStyle), mControlNames.sampleRate);
    // AddNamedChildControl(
    //   new IVLabelControl(GetRECT().SubRectVertical(4, 2), "", mStyle), mControlNames.inputCalibrationLevel);
    // AddNamedChildControl(
    //   new IVLabelControl(GetRECT().SubRectVertical(4, 3), "", mStyle), mControlNames.outputCalibrationLevel);
  };

  void SetModelInfo(const ModelInfo& modelInfo)
  {
    auto SetControlStr = [&](const std::string& name, const PossiblyKnownParameter& p, const std::string& units,
                             const std::string& childName) {
      std::stringstream ss;
      ss << name << ": ";
      if (p.known)
      {
        ss << p.value << " " << units;
      }
      else
      {
        ss << "(Unknown)";
      }
      static_cast<IVLabelControl*>(GetNamedChild(childName))->SetStr(ss.str().c_str());
    };

    SetControlStr("Sample rate", modelInfo.sampleRate, "Hz", mControlNames.sampleRate);
    // SetControlStr(
    //   "Input calibration level", modelInfo.inputCalibrationLevel, "dBu", mControlNames.inputCalibrationLevel);
    // SetControlStr(
    //   "Output calibration level", modelInfo.outputCalibrationLevel, "dBu", mControlNames.outputCalibrationLevel);

    mHasInfo = true;
  };

private:
  const IVStyle mStyle;
  struct
  {
    const std::string sampleRate = "sampleRate";
    // const std::string inputCalibrationLevel = "inputCalibrationLevel";
    // const std::string outputCalibrationLevel = "outputCalibrationLevel";
  } mControlNames;
  // Do I have info?
  bool mHasInfo = false;
};

class OutputModeControl : public IVRadioButtonControl
{
public:
  static constexpr int kNormalizedIndex = 1;
  static constexpr int kCalibratedIndex = 2;

  OutputModeControl(const IRECT& bounds, int paramIdx, const IVStyle& style, float buttonSize)
  : IVRadioButtonControl(
      bounds, paramIdx, {}, "Output Mode", style, EVShape::Ellipse, EDirection::Vertical, buttonSize) {};

  void SetNormalizedDisable(const bool disable)
  {
    SetSupportLabel(kNormalizedIndex, "Normalized", disable);
  };
  void SetCalibratedDisable(const bool disable)
  {
    SetSupportLabel(kCalibratedIndex, "Calibrated", disable);
  };

private:
  void SetSupportLabel(const int labelIndex, const char* modeName, const bool disable)
  {
    if (labelIndex < 0 || labelIndex >= mTabLabels.GetSize() || mTabLabels.Get(labelIndex) == nullptr)
      return;

    std::stringstream ss;
    ss << modeName;
    if (disable)
    {
      ss << " [Not supported by model]";
    }
    mTabLabels.Get(labelIndex)->Set(ss.str().c_str());
  }
};

class NAMSettingsPageControl : public IContainerBaseWithNamedChildren
{
public:
  NAMSettingsPageControl(const IRECT& bounds, const IBitmap& bitmap, const IBitmap& inputLevelBackgroundBitmap,
                         const IBitmap& switchBitmap, ISVG closeSVG, const IVStyle& style,
                         const IVStyle& radioButtonStyle)
  : IContainerBaseWithNamedChildren(bounds)
  , mAnimationTime(0)
  , mBitmap(bitmap)
  , mInputLevelBackgroundBitmap(inputLevelBackgroundBitmap)
  , mSwitchBitmap(switchBitmap)
  , mStyle(style)
  , mRadioButtonStyle(radioButtonStyle)
  , mCloseSVG(closeSVG)
  {
    mIgnoreMouse = false;
  }

  void ClearModelInfo()
  {
    auto* modelInfoControl = static_cast<ModelInfoControl*>(GetNamedChild(mControlNames.modelInfo));
    assert(modelInfoControl != nullptr);
    modelInfoControl->ClearModelInfo();
  }

  bool OnKeyDown(float x, float y, const IKeyPress& key) override
  {
    if (key.VK == kVK_ESCAPE)
    {
      HideAnimated(true);
      return true;
    }

    return false;
  }

  void HideAnimated(bool hide)
  {
    mWillHide = hide;

    if (hide == false)
    {
      mHide = false;
      // Restore child visibility when reopening and let file browsers re-apply loaded/empty button state.
      ForAllChildrenFunc([](int, IControl* pChild) {
        pChild->Hide(false);
        if (auto* pFileBrowser = dynamic_cast<NAMFileBrowserControl*>(pChild))
          pFileBrowser->RefreshBrowserStateVisibility();
      });
    }
    else // hide subcontrols immediately
    {
      ForAllChildrenFunc([hide](int childIdx, IControl* pChild) { pChild->Hide(hide); });
    }

    SetAnimation(
      [&](IControl* pCaller) {
        auto progress = static_cast<float>(pCaller->GetAnimationProgress());

        if (mWillHide)
          SetBlend(IBlend(EBlend::Default, 1.0f - progress));
        else
          SetBlend(IBlend(EBlend::Default, progress));

        if (progress > 1.0f)
        {
          pCaller->OnEndAnimation();
          IContainerBase::Hide(mWillHide);
          GetUI()->SetAllControlsDirty();
          return;
        }
      },
      mAnimationTime);

    SetDirty(true);
  }

  void OnAttached() override
  {
    const float pad = 20.0f;
    const IVStyle titleStyle = DEFAULT_STYLE.WithValueText(IText(30, COLOR_WHITE, "Michroma-Regular"))
                                 .WithDrawFrame(false)
                                 .WithShadowOffset(2.f);
    const auto text = IText(DEFAULT_TEXT_SIZE, EAlign::Center, PluginColors::HELP_TEXT);
    const auto leftText = text.WithAlign(EAlign::Near);
    const auto style = mStyle.WithDrawFrame(false).WithValueText(text);
    const IVStyle leftStyle = style.WithValueText(leftText);

    AddNamedChildControl(new IBitmapControl(GetRECT(), mBitmap), mControlNames.bitmap)->SetIgnoreMouse(true);
    const auto titleArea = GetRECT().GetPadded(-(pad + 10.0f)).GetFromTop(50.0f);
    AddNamedChildControl(new IVLabelControl(titleArea, "SETTINGS", titleStyle), mControlNames.title);

    // Attach output mode with input calibration stacked below it (right column)
    {
      const auto settingsBody = GetRECT().GetPadded(-(pad + 10.0f));
      const auto controlsArea = settingsBody.GetFromTop(310.0f).GetTranslated(0.0f, 70.0f);
      const auto rightArea = controlsArea.GetFromRight(0.5f * controlsArea.W());
      const float knobWidth = 87.0f; // HACK based on looking at the main page knobs.

      const auto outputRadioArea = rightArea.GetFromTop(138.0f).GetHPadded(-8.0f);
      const float buttonSize = 10.0f;
      auto* outputModeControl =
        AddNamedChildControl(new OutputModeControl(outputRadioArea, kOutputMode, mRadioButtonStyle, buttonSize),
                             mControlNames.outputMode, kCtrlTagOutputMode);
      outputModeControl->SetTooltip(
        "How to adjust the level of the output.\nRaw=No adjustment.\nNormalized=Adjust the level so that all models "
        "are about the same loudness.\nCalibrated=Match the input's digital-analog calibration.");

      const float inputLevelTop = outputRadioArea.B + 16.0f;
      const auto inputLevelArea =
        IRECT(rightArea.L, inputLevelTop, rightArea.R, inputLevelTop + 30.0f).GetMidHPadded(0.5f * knobWidth);
      const auto inputSwitchArea =
        IRECT(rightArea.L, inputLevelArea.B + 10.0f, rightArea.R, inputLevelArea.B + 10.0f + NAM_SWTICH_HEIGHT)
          .GetMidHPadded(0.5f * knobWidth);

      auto* inputLevelControl = AddNamedChildControl(
        new InputLevelControl(inputLevelArea, kInputCalibrationLevel, mInputLevelBackgroundBitmap, text),
        mControlNames.inputCalibrationLevel, kCtrlTagInputCalibrationLevel);
      inputLevelControl->SetTooltip(
        "The analog level, in dBu RMS, that corresponds to digital level of 0 dBFS peak in the host as its signal "
        "enters this plugin.");
      AddNamedChildControl(
        new NAMSwitchControl(inputSwitchArea, kCalibrateInput, "Calibrate Input", mStyle, mSwitchBitmap),
        mControlNames.calibrateInput, kCtrlTagCalibrateInput);
    }

    const float halfWidth = PLUG_WIDTH / 2.0f - pad;
    const auto bottomArea = GetRECT().GetPadded(-pad).GetFromBottom(78.0f);
    const float lineHeight = 15.0f;
    const auto modelInfoArea = bottomArea.GetFromLeft(halfWidth).GetFromTop(4 * lineHeight);
    AddNamedChildControl(new ModelInfoControl(modelInfoArea, leftStyle), mControlNames.modelInfo);

    auto closeAction = [&](IControl* pCaller) {
      static_cast<NAMSettingsPageControl*>(pCaller->GetParent())->HideAnimated(true);
    };
    AddNamedChildControl(
      new NAMSquareButtonControl(CornerButtonArea(GetRECT()), closeAction, mCloseSVG), mControlNames.close);

    OnResize();
  }

  void SetModelInfo(const ModelInfo& modelInfo)
  {
    auto* modelInfoControl = static_cast<ModelInfoControl*>(GetNamedChild(mControlNames.modelInfo));
    assert(modelInfoControl != nullptr);
    modelInfoControl->SetModelInfo(modelInfo);
  };

private:
  IBitmap mBitmap;
  IBitmap mInputLevelBackgroundBitmap;
  IBitmap mSwitchBitmap;
  IVStyle mStyle;
  IVStyle mRadioButtonStyle;
  ISVG mCloseSVG;
  int mAnimationTime = 200;
  bool mWillHide = false;

  // Names for controls
  // Make sure that these are all unique and that you use them with AddNamedChildControl
  struct ControlNames
  {
    const std::string bitmap = "Bitmap";
    const std::string calibrateInput = "CalibrateInput";
    const std::string close = "Close";
    const std::string inputCalibrationLevel = "InputCalibrationLevel";
    const std::string modelInfo = "ModelInfo";
    const std::string outputMode = "OutputMode";
    const std::string title = "Title";
  } mControlNames;

  class InputLevelControl : public IEditableTextControl
  {
  public:
    InputLevelControl(const IRECT& bounds, int paramIdx, const IBitmap& bitmap, const IText& text = DEFAULT_TEXT,
                      const IColor& BGColor = DEFAULT_BGCOLOR)
    : IEditableTextControl(bounds, "", text, BGColor)
    , mBitmap(bitmap)
    {
      SetParamIdx(paramIdx);
    };

    void Draw(IGraphics& g) override
    {
      g.DrawFittedBitmap(mBitmap, mRECT);
      ITextControl::Draw(g);
    };

    void SetValueFromUserInput(double normalizedValue, int valIdx) override
    {
      IControl::SetValueFromUserInput(normalizedValue, valIdx);
      const std::string s = ConvertToString(normalizedValue);
      OnTextEntryCompletion(s.c_str(), valIdx);
    };

    void SetValueFromDelegate(double normalizedValue, int valIdx) override
    {
      IControl::SetValueFromDelegate(normalizedValue, valIdx);
      const std::string s = ConvertToString(normalizedValue);
      SetStr(s.c_str());
      SetDirty(false);
    };

  private:
    std::string ConvertToString(const double normalizedValue)
    {
      const double naturalValue = GetParam()->FromNormalized(normalizedValue);
      // And make the value to display
      std::stringstream ss;
      ss << naturalValue << " dBu";
      std::string s = ss.str();
      return s;
    };

    IBitmap mBitmap;
  };

};
