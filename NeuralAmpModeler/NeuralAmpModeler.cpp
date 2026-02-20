#include <algorithm> // std::clamp, std::min
#include <cmath> // pow
#include <cstring> // strcmp
#include <filesystem>
#include <iostream>
#include <utility>

#include "Colors.h"
#include "../NeuralAmpModelerCore/NAM/activations.h"
#include "../NeuralAmpModelerCore/NAM/get_dsp.h"
// clang-format off
// These includes need to happen in this order or else the latter won't know
// a bunch of stuff.
#include "NeuralAmpModeler.h"
#include "IPlug_include_in_plug_src.h"
// clang-format on
#include "architecture.hpp"

#include "NeuralAmpModelerControls.h"

using namespace iplug;
using namespace igraphics;

const double kDCBlockerFrequency = 5.0;

namespace
{
struct AsymmetricPreGainShape : public IParam::Shape
{
  IParam::Shape* Clone() const override { return new AsymmetricPreGainShape(*this); }
  IParam::EDisplayType GetDisplayType() const override { return IParam::kDisplayLinear; }

  double NormalizedToValue(double normalizedValue, const IParam& param) const override
  {
    const double minVal = param.GetMin();
    const double maxVal = param.GetMax();
    constexpr double pivotNorm = 0.5;
    constexpr double pivotValue = 0.0;
    const double n = std::clamp(normalizedValue, 0.0, 1.0);

    if (minVal >= pivotValue || maxVal <= pivotValue)
      return minVal + n * (maxVal - minVal);

    if (n <= pivotNorm)
    {
      const double t = n / pivotNorm;
      return minVal + t * (pivotValue - minVal);
    }

    const double t = (n - pivotNorm) / (1.0 - pivotNorm);
    return pivotValue + t * (maxVal - pivotValue);
  }

  double ValueToNormalized(double nonNormalizedValue, const IParam& param) const override
  {
    const double minVal = param.GetMin();
    const double maxVal = param.GetMax();
    constexpr double pivotNorm = 0.5;
    constexpr double pivotValue = 0.0;
    const double v = std::clamp(nonNormalizedValue, minVal, maxVal);

    if (minVal >= pivotValue || maxVal <= pivotValue)
      return (v - minVal) / (maxVal - minVal);

    if (v <= pivotValue)
    {
      const double denom = (pivotValue - minVal);
      const double t = (denom > 0.0) ? ((v - minVal) / denom) : 0.0;
      return t * pivotNorm;
    }

    const double denom = (maxVal - pivotValue);
    const double t = (denom > 0.0) ? ((v - pivotValue) / denom) : 0.0;
    return pivotNorm + t * (1.0 - pivotNorm);
  }
};
} // namespace

// Styles
const IVColorSpec colorSpec{
  DEFAULT_BGCOLOR, // Background
  PluginColors::NAM_THEMECOLOR, // Foreground
  PluginColors::NAM_THEMECOLOR.WithOpacity(0.3f), // Pressed
  PluginColors::NAM_THEMECOLOR.WithOpacity(0.4f), // Frame
  PluginColors::MOUSEOVER, // Highlight
  DEFAULT_SHCOLOR, // Shadow
  PluginColors::NAM_THEMECOLOR, // Extra 1
  COLOR_RED, // Extra 2 --> color for clipping in meters
  PluginColors::NAM_THEMECOLOR.WithContrast(0.1f), // Extra 3
};

const IVStyle style =
  IVStyle{true, // Show label
          true, // Show value
          colorSpec,
          {DEFAULT_TEXT_SIZE + 3.f, EVAlign::Middle, PluginColors::NAM_THEMEFONTCOLOR}, // Knob label text5
          {DEFAULT_TEXT_SIZE + 3.f, EVAlign::Bottom, PluginColors::NAM_THEMEFONTCOLOR}, // Knob value text
          DEFAULT_HIDE_CURSOR,
          DEFAULT_DRAW_FRAME,
          false,
          DEFAULT_EMBOSS,
          0.2f,
          2.f,
          DEFAULT_SHADOW_OFFSET,
          DEFAULT_WIDGET_FRAC,
          DEFAULT_WIDGET_ANGLE};
const IVStyle utilityStyle = style.WithLabelText(
  IText(DEFAULT_TEXT_SIZE + 1.f, PluginColors::NAM_THEMEFONTCOLOR, "ArialNarrow-Bold", EAlign::Center, EVAlign::Middle))
                                .WithValueText(IText(DEFAULT_TEXT_SIZE + 0.f,
                                                     PluginColors::NAM_THEMEFONTCOLOR.WithOpacity(0.9f),
                                                     "ArialNarrow-Bold",
                                                     EAlign::Center,
                                                     EVAlign::Bottom));
const IVStyle ampKnobStyle = style.WithShowValue(false).WithLabelText(
  IText(DEFAULT_TEXT_SIZE + -4.f, COLOR_BLACK, "ArialNarrow-Bold", EAlign::Center, EVAlign::Middle));
const IVStyle radioButtonStyle =
  style
    .WithColor(EVColor::kON, PluginColors::NAM_THEMECOLOR) // Pressed buttons and their labels
    .WithColor(EVColor::kOFF, PluginColors::NAM_THEMECOLOR.WithOpacity(0.1f)) // Unpressed buttons
    .WithColor(EVColor::kX1, PluginColors::NAM_THEMECOLOR.WithOpacity(0.6f)); // Unpressed buttons' labels

EMsgBoxResult _ShowMessageBox(iplug::igraphics::IGraphics* pGraphics, const char* str, const char* caption,
                              EMsgBoxType type)
{
#ifdef OS_MAC
  // macOS is backwards?
  return pGraphics->ShowMessageBox(caption, str, type);
#else
  return pGraphics->ShowMessageBox(str, caption, type);
#endif
}

const std::string kCalibrateInputParamName = "CalibrateInput";
const bool kDefaultCalibrateInput = false;
const std::string kInputCalibrationLevelParamName = "InputCalibrationLevel";
const double kDefaultInputCalibrationLevel = 12.0;


NeuralAmpModeler::NeuralAmpModeler(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  _InitToneStack();
  nam::activations::Activation::enable_fast_tanh();
  GetParam(kInputLevel)->InitGain("Input", 0.0, -20.0, 20.0, 0.1);
  GetParam(kPreModelGain)->InitDouble(
    "Pre Gain", 0.0, -40.0, 20.0, 0.1, "dB", 0, "", AsymmetricPreGainShape(), IParam::kUnitDB);
  GetParam(kToneBass)->InitDouble("Bass", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneMid)->InitDouble("Middle", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneTreble)->InitDouble("Treble", 5.0, 0.0, 10.0, 0.1);
  GetParam(kTonePresence)->InitDouble("Presence", 5.0, 0.0, 10.0, 0.1);
  GetParam(kToneDepth)->InitDouble("Depth", 5.0, 0.0, 10.0, 0.1);
  GetParam(kMasterVolume)->InitDouble("Master", 5.0, 0.0, 10.0, 0.1);
  GetParam(kTunerActive)->InitBool("Tuner", false);
  GetParam(kTunerMonitorMode)->InitEnum("Tuner Monitor", 1, {"Mute", "Bypass", "Full"});
  GetParam(kOutputLevel)->InitGain("Output", 0.0, -40.0, 40.0, 0.1);
  GetParam(kNoiseGateThreshold)->InitGain("Threshold", -80.0, -100.0, 0.0, 0.1);
  GetParam(kNoiseGateActive)->InitBool("NoiseGateActive", true);
  GetParam(kEQActive)->InitBool("ToneStack", true);
  GetParam(kOutputMode)->InitEnum("OutputMode", 1, {"Raw", "Normalized", "Calibrated"}); // TODO DRY w/ control
  GetParam(kIRToggle)->InitBool("IRToggle", true);
  GetParam(kModelToggle)->InitBool("ModelToggle", false);
  GetParam(kCabIRBlend)->InitDouble("Cab Blend", 50.0, 0.0, 100.0, 0.1, "%");
  GetParam(kUserHPFFrequency)->InitDouble("HPF", 20.0, 20.0, 500.0, 1.0, "Hz");
  GetParam(kUserLPFFrequency)->InitDouble("LPF", 22000.0, 5000.0, 22000.0, 10.0, "Hz");
  GetParam(kCalibrateInput)->InitBool(kCalibrateInputParamName.c_str(), kDefaultCalibrateInput);
  GetParam(kInputCalibrationLevel)
    ->InitDouble(kInputCalibrationLevelParamName.c_str(), kDefaultInputCalibrationLevel, -60.0, 60.0, 0.1, "dBu");
  _SetMasterGain();

  mNoiseGateTrigger.AddListener(&mNoiseGateGain);

  mMakeGraphicsFunc = [&]() {

#ifdef OS_IOS
    auto scaleFactor = GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT) * 0.85f;
#else
    auto scaleFactor = 1.0f;
#endif

    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, scaleFactor);
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
    pGraphics->AttachTextEntryControl();
    pGraphics->EnableMouseOver(true);
    pGraphics->EnableTooltips(true);
    pGraphics->EnableMultiTouch(true);

    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    pGraphics->LoadFont("Michroma-Regular", MICHROMA_FN);
    if (!pGraphics->LoadFont("ArialNarrow-Bold", "Arial Narrow", ETextStyle::Bold))
      pGraphics->LoadFont("ArialNarrow-Bold", ROBOTO_FN);

    const auto gearSVG = pGraphics->LoadSVG(GEAR_FN);
    const auto fileSVG = pGraphics->LoadSVG(FILE_FN);
    const auto globeSVG = pGraphics->LoadSVG(GLOBE_ICON_FN);
    const auto crossSVG = pGraphics->LoadSVG(CLOSE_BUTTON_FN);
    const auto rightArrowSVG = pGraphics->LoadSVG(RIGHT_ARROW_FN);
    const auto leftArrowSVG = pGraphics->LoadSVG(LEFT_ARROW_FN);
    const auto irIconOnSVG = pGraphics->LoadSVG(IR_ICON_ON_FN);
    const auto irIconOffSVG = pGraphics->LoadSVG(IR_ICON_OFF_FN);
    const auto ampActiveSVG = pGraphics->LoadSVG(AMP_ACTIVE_SVG_FN);
    const auto stompActiveSVG = pGraphics->LoadSVG(STOMP_ACTIVE_SVG_FN);
    const auto cabActiveSVG = pGraphics->LoadSVG(CAB_ACTIVE_SVG_FN);
    const auto fxActiveSVG = pGraphics->LoadSVG(FX_ACTIVE_SVG_FN);
    const auto tunerActiveSVG = pGraphics->LoadSVG(TUNER_ACTIVE_SVG_FN);
    const auto outerKnobBackgroundSVG = pGraphics->LoadSVG(FLATKNOBBACKGROUND_SVG_FN);

    const auto backgroundBitmap = pGraphics->LoadBitmap(BACKGROUND_FN);
    const auto settingsBackgroundBitmap = pGraphics->LoadBitmap(SETTINGSBACKGROUND_FN);
    const auto fileBackgroundBitmap = pGraphics->LoadBitmap(FILEBACKGROUND_FN);
    const auto inputLevelBackgroundBitmap = pGraphics->LoadBitmap(INPUTLEVELBACKGROUND_FN);
    const auto linesBitmap = pGraphics->LoadBitmap(LINES_FN);
    const auto ampKnobBackgroundBitmap = pGraphics->LoadBitmap(KNOBBACKGROUND_FN);
    const auto switchOffBitmap = pGraphics->LoadBitmap(SWITCH_OFF_FN);
    const auto switchOnBitmap = pGraphics->LoadBitmap(SWITCH_ON_FN);
    const auto switchHandleBitmap = pGraphics->LoadBitmap(SLIDESWITCHHANDLE_FN);
    const auto meterBackgroundBitmap = pGraphics->LoadBitmap(METERBACKGROUND_FN);

    // Top/section icons are SVG-only now.

    const auto b = pGraphics->GetBounds();
    // Global layout tokens for consistent spacing and sectioning.
    constexpr float kOuterPad = 20.0f;
    constexpr float kInnerPad = 10.0f;
    constexpr float kTopBarHeight = 162.0f;
    constexpr float kBottomBarHeight = 62.0f;
    const auto mainArea = b.GetPadded(-kOuterPad);
    const auto contentArea = mainArea.GetPadded(-kInnerPad);
    const auto topBarArea = IRECT(contentArea.L, contentArea.T, contentArea.R, contentArea.T + kTopBarHeight);
    const auto bottomBarArea = IRECT(contentArea.L, contentArea.B - kBottomBarHeight, contentArea.R, contentArea.B);
    const auto heroArea = IRECT(contentArea.L, topBarArea.B, contentArea.R, bottomBarArea.T);
    // Dedicated amp-face anchor region. Front-panel controls reference this instead of top bar sizing.
    const auto ampFaceArea = IRECT(contentArea.L + 66.0f, contentArea.T + 215.0f, contentArea.R - 66.0f, contentArea.T + 496.0f);
    // Phase 1 top layout: primary section row + compact utility row.
    constexpr float kTopRowOuterPad = -2.0f;
    constexpr float kTopRowsGap = 8.0f;
    constexpr float kTopMainRowHeight = 60.0f;
    constexpr float kTopUtilityRowHeight = 40.0f;
    const auto topMainRowArea =
      IRECT(topBarArea.L, topBarArea.T + kTopRowOuterPad, topBarArea.R, topBarArea.T + kTopRowOuterPad + kTopMainRowHeight);
    const auto topUtilityRowArea = IRECT(
      topBarArea.L, topMainRowArea.B + kTopRowsGap, topBarArea.R, topMainRowArea.B + kTopRowsGap + kTopUtilityRowHeight);

    // Areas for knobs
    // Keep these as explicit anchors so each group can be tuned independently.
    const float knobWidth = 80.0f;
    auto makeKnobArea = [&](const float centerX, const float topY) {
      return IRECT(centerX - knobWidth * 0.5f, topY, centerX + knobWidth * 0.5f, topY + NAM_KNOB_HEIGHT);
    };
    // Top-bar side I/O group (left/right mirrored).
    const float topSideKnobTop = topMainRowArea.MH() - 0.5f * NAM_KNOB_HEIGHT - 8.0f;
    // SVG flat knob art has tighter bounds than the old PNG, so compensate to match visual size.
    const float topSideKnobScale = 0.70f;
    const float topSideMeterWidth = 10.0f;
    const float topSideMeterHeight = 60.0f;
    const float topSideMeterTop = topMainRowArea.MH() - 0.5f * topSideMeterHeight + -6.0f;
    const float topSideMeterCenterInset = 8.0f;
    const float topSideKnobCenterInset = 56.0f;
    const float topSideFilterGapX = 82.0f;
    const float leftInputCenterX = contentArea.L + topSideKnobCenterInset;
    const float rightOutputCenterX = contentArea.R - topSideKnobCenterInset;
    const float leftFilterCenterX = leftInputCenterX + topSideFilterGapX;
    const float rightFilterCenterX = rightOutputCenterX - topSideFilterGapX;

    const auto inputKnobArea = makeKnobArea(leftInputCenterX, topSideKnobTop);
    const auto outputKnobArea = makeKnobArea(rightOutputCenterX, topSideKnobTop);

    // Amp-face controls (independent group)
    const float frontKnobTop = ampFaceArea.T + 150.0f;
    const float frontRowCenterX = ampFaceArea.MW() - 55.0f;
    const float frontKnobSpacing = 80.0f;
    const auto noiseGateArea = makeKnobArea(frontRowCenterX - 3.0f * frontKnobSpacing, frontKnobTop);
    const auto preModelGainArea = makeKnobArea(frontRowCenterX - 2.0f * frontKnobSpacing, frontKnobTop);
    const auto bassKnobArea = makeKnobArea(frontRowCenterX - 1.0f * frontKnobSpacing, frontKnobTop);
    const auto midKnobArea = makeKnobArea(frontRowCenterX, frontKnobTop);
    const auto trebleKnobArea = makeKnobArea(frontRowCenterX + 1.0f * frontKnobSpacing, frontKnobTop);
    const auto presenceKnobArea = makeKnobArea(frontRowCenterX + 2.0f * frontKnobSpacing, frontKnobTop);
    const auto depthKnobArea = makeKnobArea(frontRowCenterX + 3.0f * frontKnobSpacing, frontKnobTop);
    const auto masterKnobArea = makeKnobArea(frontRowCenterX + 4.0f * frontKnobSpacing, frontKnobTop);
    const auto noiseGateLEDRect = noiseGateArea.GetFromBLHC(14.0f, 14.0f).GetTranslated(3.0f, -25.0f);
    const float modelSwitchScale = 0.20f;
    const float modelSwitchWidth = switchOffBitmap.W() * modelSwitchScale;
    const float modelSwitchHeight = switchOffBitmap.H() * modelSwitchScale;
    const float modelSwitchCenterX = std::min(b.W() - 120.0f, masterKnobArea.MW() + 130.0f);
    const float modelSwitchCenterY = noiseGateArea.MH();
    const auto modelToggleArea = IRECT(modelSwitchCenterX - 0.5f * modelSwitchWidth,
                                       modelSwitchCenterY - 0.5f * modelSwitchHeight,
                                       modelSwitchCenterX + 0.5f * modelSwitchWidth,
                                       modelSwitchCenterY + 0.5f * modelSwitchHeight);

    // Gate/EQ toggle row (independent group)
    const float toggleTop = frontKnobTop + 86.0f;
    const auto ngToggleArea =
      IRECT(noiseGateArea.MW() - 17.0f, toggleTop, noiseGateArea.MW() + 17.0f, toggleTop + 24.0f);
    const auto eqToggleArea = IRECT(midKnobArea.MW() - 17.0f, toggleTop, midKnobArea.MW() + 17.0f, toggleTop + 24.0f);

    // Top-bar filter controls live with input/output controls.
    const auto hpfKnobArea = makeKnobArea(leftFilterCenterX, topSideKnobTop);
    const auto lpfKnobArea = makeKnobArea(rightFilterCenterX, topSideKnobTop);

    constexpr float kSettingsIconHeight = 24.0f;
    constexpr float kSettingsRightPad = 8.0f;
    const float topUtilityIconCenterY = topUtilityRowArea.MH();
    const auto settingsButtonArea =
      IRECT(topBarArea.R - kSettingsRightPad - kSettingsIconHeight,
            topUtilityIconCenterY - 0.5f * kSettingsIconHeight,
            topBarArea.R - kSettingsRightPad,
            topUtilityIconCenterY + 0.5f * kSettingsIconHeight);
    // Top nav uses fixed icon height; width follows each bitmap aspect ratio.
    constexpr float topNavIconHeight = 60.0f;
    constexpr float kTunerToolIconHeight = 43.0f;
    constexpr float kTopNavRowYOffset = -6.0f;
    const float topNavIconGap = 38.0f;
    // Areas for model and IR
    // Top bar has two visual rows: icon row + primary control row.
    const float topBarIconRowTop = topMainRowArea.MH() - 0.5f * topNavIconHeight + kTopNavRowYOffset;
    const float topBarControlRowTopBase = topUtilityRowArea.T;
    constexpr float kModelPickerWidth = 320.0f;
    constexpr float kModelPickerHeight = 30.0f;
    // Temporary model picker placement near the bottom of the amp body.
    const float modelPickerTop = ampFaceArea.B + 25.0f;
    const auto modelArea = IRECT(heroArea.MW() - 0.5f * kModelPickerWidth,
                                 modelPickerTop,
                                 heroArea.MW() + 0.5f * kModelPickerWidth,
                                 modelPickerTop + kModelPickerHeight);
    const float tunerPanelWidth = 700.0f;
    const float tunerPanelHeight = 150.0f;
    const float tunerPanelTop = topUtilityRowArea.B + 90.0f;
    const auto tunerReadoutArea =
      IRECT(heroArea.MW() - 0.5f * tunerPanelWidth, tunerPanelTop, heroArea.MW() + 0.5f * tunerPanelWidth, tunerPanelTop + tunerPanelHeight);
    const float tunerMonitorTop = tunerReadoutArea.T + 10.0f;
    const auto tunerMonitorArea =
      IRECT(tunerReadoutArea.L + 12.0f, tunerMonitorTop, tunerReadoutArea.L + 134.0f, tunerMonitorTop + 22.0f);
    const auto tunerCloseArea = tunerReadoutArea.GetFromTRHC(18.0f, 18.0f).GetTranslated(-10.0f, 10.0f);
    const auto scaledWidthForHeightSVG = [&](const ISVG& svg, const float targetHeight) {
      return (svg.IsValid() && svg.H() > 0.0f) ? (svg.W() * (targetHeight / svg.H())) : targetHeight;
    };
    const float topNavTunerWidth = scaledWidthForHeightSVG(tunerActiveSVG, kTunerToolIconHeight);
    const float topNavStompWidth = scaledWidthForHeightSVG(stompActiveSVG, topNavIconHeight);
    const float topNavAmpWidth = scaledWidthForHeightSVG(ampActiveSVG, topNavIconHeight);
    const float topNavCabWidth = scaledWidthForHeightSVG(cabActiveSVG, topNavIconHeight);
    const float topNavFxWidth = scaledWidthForHeightSVG(fxActiveSVG, topNavIconHeight);
    const float topNavRowWidth = topNavStompWidth + topNavAmpWidth + topNavCabWidth + topNavFxWidth + 3.0f * topNavIconGap;
    // Keep section icons on the same header strip as the settings cog, centered as a group.
    const float topNavLeft = topBarArea.MW() - 0.5f * topNavRowWidth;
    const float topNavTop = topBarIconRowTop;
    // Visual order: Stomp -> Amp -> Cab -> FX
    const auto topNavStompArea = IRECT(topNavLeft, topNavTop, topNavLeft + topNavStompWidth, topNavTop + topNavIconHeight);
    const auto topNavAmpArea = IRECT(topNavStompArea.R + topNavIconGap, topNavTop,
                                     topNavStompArea.R + topNavIconGap + topNavAmpWidth, topNavTop + topNavIconHeight);
    const auto topNavCabArea = IRECT(topNavAmpArea.R + topNavIconGap, topNavTop,
                                     topNavAmpArea.R + topNavIconGap + topNavCabWidth, topNavTop + topNavIconHeight);
    const auto topNavFxArea = IRECT(topNavCabArea.R + topNavIconGap, topNavTop,
                                    topNavCabArea.R + topNavIconGap + topNavFxWidth, topNavTop + topNavIconHeight);
    // Tuner is a tool icon, placed to the left of settings rather than in the section strip.
    constexpr float kTunerToolGap = 20.0f;
    const float tunerToolRight = settingsButtonArea.L - kTunerToolGap;
    const float tunerToolLeft = std::max(contentArea.L, tunerToolRight - topNavTunerWidth);
    const float topToolRowTop = topUtilityIconCenterY - 0.5f * kTunerToolIconHeight;
    const auto topNavTunerArea =
      IRECT(
        tunerToolLeft, topToolRowTop, tunerToolLeft + topNavTunerWidth, topToolRowTop + kTunerToolIconHeight);
    // Preset strip in utility row (centered), with previous/next buttons and current preset text.
    constexpr float kPresetStripWidth = 340.0f;
    constexpr float kPresetStripHeight = 25.0f;
    constexpr float kPresetButtonSize = 22.0f;
    const float presetStripLeft = topUtilityRowArea.MW() - 0.5f * kPresetStripWidth;
    const float presetStripTop = topUtilityRowArea.MH() - 0.5f * kPresetStripHeight;
    const auto presetStripArea = IRECT(
      presetStripLeft, presetStripTop, presetStripLeft + kPresetStripWidth, presetStripTop + kPresetStripHeight);
    const auto presetPrevArea = presetStripArea.GetFromLeft(kPresetButtonSize);
    const auto presetNextArea = presetStripArea.GetFromRight(kPresetButtonSize);
    const auto presetLabelArea = IRECT(presetPrevArea.R + 10.0f,
                                       presetStripArea.T,
                                       presetNextArea.L - 10.0f,
                                       presetStripArea.B);
    // Footer IR strip: align both pickers and blend control on one visual baseline.
    const float irRowHeight = 30.0f;
    const float irRowTop = bottomBarArea.MH() - 0.5f * irRowHeight + 17.0f;
    const float irPickerWidth = 292.0f;
    const float irCenterGap = 132.0f;
    const float leftIRRight = b.MW() - 0.5f * irCenterGap;
    const float rightIRLeft = b.MW() + 0.5f * irCenterGap;
    const auto irLeftArea = IRECT(leftIRRight - irPickerWidth, irRowTop, leftIRRight, irRowTop + irRowHeight);
    const auto irRightArea = IRECT(rightIRLeft, irRowTop, rightIRLeft + irPickerWidth, irRowTop + irRowHeight);
    const auto irSwitchArea = irLeftArea.GetFromLeft(30.0f).GetHShifted(-36.0f).GetVShifted(-1.0f).GetScaledAboutCentre(0.6f);
    const float blendSliderWidth = 130.0f;
    const float blendSliderHeight = 60.0f;
    const float blendSliderTop = irRowTop - 12.0f;
    const auto cabBlendArea = IRECT(heroArea.MW() - 0.5f * blendSliderWidth, blendSliderTop, heroArea.MW() + 0.5f * blendSliderWidth,
                                    blendSliderTop + blendSliderHeight);
    // Footer placeholder amp selector strip (uses existing amp icon assets).
    const float footerAmpIconHeight = 43.0f;
    const float footerAmpIconWidth =
      (ampActiveSVG.IsValid() && ampActiveSVG.H() > 0.0f) ? (ampActiveSVG.W() * (footerAmpIconHeight / ampActiveSVG.H()))
                                                           : footerAmpIconHeight;
    const float footerAmpIconGap = 36.0f;
    const float footerAmpRowWidth = 3.0f * footerAmpIconWidth + 2.0f * footerAmpIconGap;
    const float footerAmpRowLeft = bottomBarArea.MW() - 0.5f * footerAmpRowWidth;
    const float footerAmpRowTop = irRowTop - 4.0f;
    const auto footerAmpSlot1Area =
      IRECT(footerAmpRowLeft, footerAmpRowTop, footerAmpRowLeft + footerAmpIconWidth, footerAmpRowTop + footerAmpIconHeight);
    const auto footerAmpSlot2Area = IRECT(footerAmpSlot1Area.R + footerAmpIconGap,
                                          footerAmpRowTop,
                                          footerAmpSlot1Area.R + footerAmpIconGap + footerAmpIconWidth,
                                          footerAmpRowTop + footerAmpIconHeight);
    const auto footerAmpSlot3Area = IRECT(footerAmpSlot2Area.R + footerAmpIconGap,
                                          footerAmpRowTop,
                                          footerAmpSlot2Area.R + footerAmpIconGap + footerAmpIconWidth,
                                          footerAmpRowTop + footerAmpIconHeight);

    // Areas for meters (aligned under input/output knobs, and low enough to avoid overlap)
    const auto inputMeterArea =
      IRECT(contentArea.L + topSideMeterCenterInset - 0.5f * topSideMeterWidth,
            topSideMeterTop,
            contentArea.L + topSideMeterCenterInset + 0.5f * topSideMeterWidth,
            topSideMeterTop + topSideMeterHeight);
    const auto outputMeterArea = IRECT(
      contentArea.R - topSideMeterCenterInset - 0.5f * topSideMeterWidth,
      topSideMeterTop,
      contentArea.R - topSideMeterCenterInset + 0.5f * topSideMeterWidth,
      topSideMeterTop + topSideMeterHeight);

    // Model loader button
    auto loadModelCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
      {
        // Sets mNAMPath and mStagedNAM
        const std::string msg = _StageModel(fileName);
        // TODO error messages like the IR loader.
        if (msg.size())
        {
          std::stringstream ss;
          ss << "Failed to load NAM model. Message:\n\n" << msg;
          _ShowMessageBox(GetUI(), ss.str().c_str(), "Failed to load model!", kMB_OK);
          GetParam(kModelToggle)->Set(0.0);
        }
        else
          GetParam(kModelToggle)->Set(1.0);
        SendParameterValueFromDelegate(kModelToggle, GetParam(kModelToggle)->GetNormalized(), true);
        std::cout << "Loaded: " << fileName.Get() << std::endl;
      }
    };

    // IR loader button
    auto loadIRLeftCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
      {
        mIRPath = fileName;
        const dsp::wav::LoadReturnCode retCode = _StageIRLeft(fileName);
        if (retCode != dsp::wav::LoadReturnCode::SUCCESS)
        {
          std::stringstream message;
          message << "Failed to load left IR file " << fileName.Get() << ":\n";
          message << dsp::wav::GetMsgForLoadReturnCode(retCode);

          _ShowMessageBox(GetUI(), message.str().c_str(), "Failed to load left IR!", kMB_OK);
        }
      }
    };

    auto loadIRRightCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
      {
        mIRPathRight = fileName;
        const dsp::wav::LoadReturnCode retCode = _StageIRRight(fileName);
        if (retCode != dsp::wav::LoadReturnCode::SUCCESS)
        {
          std::stringstream message;
          message << "Failed to load right IR file " << fileName.Get() << ":\n";
          message << dsp::wav::GetMsgForLoadReturnCode(retCode);

          _ShowMessageBox(GetUI(), message.str().c_str(), "Failed to load right IR!", kMB_OK);
        }
      }
    };

    pGraphics->AttachControl(new NAMBackgroundBitmapControl(b, BACKGROUND_FN, backgroundBitmap), kCtrlTagMainBackground);
    pGraphics->AttachControl(new IBitmapControl(b, linesBitmap));
    // Subtle utility-zone overlays to anchor top and footer controls visually.
    const IColor topBarOverlayColor = IColor(72, 6, 6, 8);
    const IColor bottomBarOverlayColor = IColor(82, 6, 6, 8);
    pGraphics->AttachControl(new IPanelControl(topBarArea, topBarOverlayColor));
    pGraphics->AttachControl(new IPanelControl(bottomBarArea, bottomBarOverlayColor));
    // Single subtle top separator (kept above amp image); no footer separator.
    const IColor separatorColor = IColor(70, 255, 255, 255);
    const float topSeparatorY = topBarControlRowTopBase - 2.0f;
    const auto topSeparatorArea = IRECT(contentArea.L, topSeparatorY, contentArea.R, topSeparatorY + 1.0f);
    pGraphics->AttachControl(new IPanelControl(topSeparatorArea, separatorColor));
    // Utility row lower boundary.
    const auto topUtilityBottomSeparatorArea = IRECT(contentArea.L, topUtilityRowArea.B, contentArea.R, topUtilityRowArea.B + 1.0f);
    pGraphics->AttachControl(new IPanelControl(topUtilityBottomSeparatorArea, separatorColor));

#ifdef NAM_PICK_DIRECTORY
    const std::string defaultNamFileString = "Select model directory...";
#else
    const std::string defaultNamFileString = "Select model...";
#endif
    // Getting started page listing additional resources
    const char* const getUrl = "https://www.neuralampmodeler.com/users#comp-marb84o5";
    pGraphics->AttachControl(
      new NAMFileBrowserControl(modelArea, kMsgTagClearModel, defaultNamFileString.c_str(), "nam",
                                loadModelCompletionHandler, utilityStyle, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
                                fileBackgroundBitmap, globeSVG, "Get NAM Models", getUrl),
      kCtrlTagModelFileBrowser);
    pGraphics->AttachControl(new NAMTunerDisplayControl(tunerReadoutArea), kCtrlTagTunerReadout);
    pGraphics->AttachControl(
      new NAMTunerMonitorControl(tunerMonitorArea, kTunerMonitorMode, utilityStyle),
                              kCtrlTagTunerMute)
      ->SetTooltip("Tuner monitor mode: Mute / Bypass / Full");
    auto updatePresetLabel = [this](IGraphics* pG) {
      if (auto* pText = dynamic_cast<ITextControl*>(pG->GetControlWithTag(kCtrlTagPresetLabel)))
      {
        if (NPresets() <= 0)
        {
          pText->SetStr("No Presets");
          pText->SetDirty(false);
          return;
        }

        const int presetIdx = std::clamp(GetCurrentPresetIdx(), 0, NPresets() - 1);
        const char* presetName = GetPresetName(presetIdx);
        WDL_String label;
        if (presetName && std::strlen(presetName) > 0)
          label.SetFormatted(256, "%d. %s", presetIdx + 1, presetName);
        else
          label.SetFormatted(64, "Preset %d", presetIdx + 1);
        pText->SetStr(label.Get());
        pText->SetDirty(false);
      }
    };
    pGraphics->AttachControl(new IPanelControl(presetStripArea, IColor(40, 255, 255, 255).WithOpacity(0.10f)));
    pGraphics->AttachControl(new NAMSquareButtonControl(presetPrevArea, [this, updatePresetLabel](IControl*) {
      if (NPresets() <= 0)
        return;
      const int count = NPresets();
      int idx = GetCurrentPresetIdx();
      if (idx < 0 || idx >= count)
        idx = 0;
      idx = (idx - 1 + count) % count;
      RestorePreset(idx);
      if (auto* pG = GetUI())
        updatePresetLabel(pG);
    }, leftArrowSVG));
    pGraphics->AttachControl(new NAMSquareButtonControl(presetNextArea, [this, updatePresetLabel](IControl*) {
      if (NPresets() <= 0)
        return;
      const int count = NPresets();
      int idx = GetCurrentPresetIdx();
      if (idx < 0 || idx >= count)
        idx = 0;
      idx = (idx + 1) % count;
      RestorePreset(idx);
      if (auto* pG = GetUI())
        updatePresetLabel(pG);
    }, rightArrowSVG));
    pGraphics->AttachControl(new ITextControl(
      presetLabelArea, "Preset", IText(13.0f, COLOR_WHITE.WithOpacity(0.92f), "ArialNarrow-Bold", EAlign::Center, EVAlign::Middle)),
                             kCtrlTagPresetLabel);
    updatePresetLabel(pGraphics);
    pGraphics->AttachControl(new NAMSquareButtonControl(
                               tunerCloseArea,
                               [this](IControl*) {
                                 const auto tunerIdx = static_cast<size_t>(TopNavSection::Tuner);
                                 if (tunerIdx < mTopNavBypassed.size())
                                 {
                                   mTopNavBypassed[tunerIdx] = true;
                                   _SyncTunerParamToTopNav();
                                   _RefreshTopNavControls();
                                 }
                               },
                               crossSVG),
                             kCtrlTagTunerClose)
      ->SetTooltip("Close tuner");
    IControl* pAmpTopIcon = new NAMTopIconControl(topNavAmpArea, ampActiveSVG, ampActiveSVG, ampActiveSVG,
                                                  [this]() { _SetTopNavActiveSection(TopNavSection::Amp); },
                                                  [this]() { _ToggleTopNavSectionBypass(TopNavSection::Amp); });
    pGraphics->AttachControl(pAmpTopIcon, kCtrlTagTopNavAmp)->SetTooltip("Amp");

    IControl* pStompTopIcon = new NAMTopIconControl(topNavStompArea, stompActiveSVG, stompActiveSVG, stompActiveSVG,
                                                    [this]() { _SetTopNavActiveSection(TopNavSection::Stomp); },
                                                    [this]() { _ToggleTopNavSectionBypass(TopNavSection::Stomp); });
    pGraphics->AttachControl(pStompTopIcon, kCtrlTagTopNavStomp)->SetTooltip("Stomp");

    IControl* pCabTopIcon = new NAMTopIconControl(topNavCabArea, cabActiveSVG, cabActiveSVG, cabActiveSVG,
                                                  [this]() { _SetTopNavActiveSection(TopNavSection::Cab); },
                                                  [this]() { _ToggleTopNavSectionBypass(TopNavSection::Cab); });
    pGraphics->AttachControl(pCabTopIcon, kCtrlTagTopNavCab)->SetTooltip("Cab");

    IControl* pFxTopIcon = new NAMTopIconControl(topNavFxArea, fxActiveSVG, fxActiveSVG, fxActiveSVG,
                                                 [this]() { _SetTopNavActiveSection(TopNavSection::Fx); },
                                                 [this]() { _ToggleTopNavSectionBypass(TopNavSection::Fx); });
    pGraphics->AttachControl(pFxTopIcon, kCtrlTagTopNavFx)->SetTooltip("FX");
    pGraphics->AttachControl(
      new NAMTopIconControl(topNavTunerArea, tunerActiveSVG, tunerActiveSVG, tunerActiveSVG,
                            [this]() {
                              const auto tunerIdx = static_cast<size_t>(TopNavSection::Tuner);
                              if (tunerIdx < mTopNavBypassed.size())
                              {
                                // Tuner behaves as a regular on/off toggle on normal click.
                                mTopNavBypassed[tunerIdx] = !mTopNavBypassed[tunerIdx];
                                _SyncTunerParamToTopNav();
                                _RefreshTopNavControls();
                              }
                            },
                            [this]() {
                              const auto tunerIdx = static_cast<size_t>(TopNavSection::Tuner);
                              if (tunerIdx < mTopNavBypassed.size())
                              {
                                // Keep Ctrl/Right-click behavior consistent with left-click toggle.
                                mTopNavBypassed[tunerIdx] = !mTopNavBypassed[tunerIdx];
                                _SyncTunerParamToTopNav();
                                _RefreshTopNavControls();
                              }
                            },
                            false),
      kCtrlTagTopNavTuner)
      ->SetTooltip("Tuner");
    pGraphics->AttachControl(new NAMBitmapToggleControl(modelToggleArea, kModelToggle, switchOffBitmap, switchOnBitmap))
      ->SetTooltip("Model On/Off");
    pGraphics->AttachControl(new ISVGSwitchControl(irSwitchArea, {irIconOffSVG, irIconOnSVG}, kIRToggle), kCtrlTagIRToggle);
    pGraphics->AttachControl(new NAMFileBrowserControl(irLeftArea, kMsgTagClearIRLeft, "Select cab IR L...", "wav",
                                                       loadIRLeftCompletionHandler, utilityStyle, fileSVG, crossSVG,
                                                       leftArrowSVG, rightArrowSVG, fileBackgroundBitmap, globeSVG,
                                                       "Get IRs", getUrl),
                             kCtrlTagIRFileBrowserLeft);
    pGraphics->AttachControl(
      new NAMFileBrowserControl(irRightArea, kMsgTagClearIRRight, "Select cab IR R...", "wav",
                                 loadIRRightCompletionHandler, utilityStyle, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
                                 fileBackgroundBitmap, globeSVG, "Get IRs", getUrl),
      kCtrlTagIRFileBrowserRight);
    pGraphics->AttachControl(new NAMBlendSliderControl(cabBlendArea, kCabIRBlend, utilityStyle));
    pGraphics->AttachControl(new NAMTopIconControl(footerAmpSlot1Area, ampActiveSVG, ampActiveSVG, ampActiveSVG,
                                                   [this]() {
                                                     mAmpSelectorIndex = 0;
                                                     _RefreshTopNavControls();
                                                   },
                                                   {}),
                             kCtrlTagAmpSlot1)
      ->SetTooltip("Amp Slot 1");
    pGraphics->AttachControl(new NAMTopIconControl(footerAmpSlot2Area, ampActiveSVG, ampActiveSVG, ampActiveSVG,
                                                   [this]() {
                                                     mAmpSelectorIndex = 1;
                                                     _RefreshTopNavControls();
                                                   },
                                                   {}),
                             kCtrlTagAmpSlot2)
      ->SetTooltip("Amp Slot 2");
    pGraphics->AttachControl(new NAMTopIconControl(footerAmpSlot3Area, ampActiveSVG, ampActiveSVG, ampActiveSVG,
                                                   [this]() {
                                                     mAmpSelectorIndex = 2;
                                                     _RefreshTopNavControls();
                                                   },
                                                   {}),
                             kCtrlTagAmpSlot3)
      ->SetTooltip("Amp Slot 3");
    pGraphics->AttachControl(new NAMSwitchControl(ngToggleArea, kNoiseGateActive, "Noise Gate", style, switchHandleBitmap))
      ->Hide(true);
    pGraphics->AttachControl(new NAMLEDControl(noiseGateLEDRect), kCtrlTagNoiseGateLED);
    pGraphics->AttachControl(new NAMSwitchControl(eqToggleArea, kEQActive, "EQ", style, switchHandleBitmap))->Hide(true);

    // The knobs
    constexpr float kSideLabelYOffset = 18.0f;
    constexpr float kSideValueYOffset = -24.0f;
    pGraphics->AttachControl(new NAMKnobControl(
      inputKnobArea, kInputLevel, "INPUT", utilityStyle, outerKnobBackgroundSVG, true, false, topSideKnobScale, kSideLabelYOffset,
      kSideValueYOffset));
    pGraphics->AttachControl(new NAMKnobControl(noiseGateArea, kNoiseGateThreshold, "GATE", ampKnobStyle,
                                                ampKnobBackgroundBitmap, false, true, 0.75f, AP_KNOP_OFFSET));
    pGraphics->AttachControl(new NAMKnobControl(preModelGainArea, kPreModelGain, "PRE GAIN", ampKnobStyle,
                                                ampKnobBackgroundBitmap, false, true, 0.7f, AP_KNOP_OFFSET));
    pGraphics->AttachControl(
      new NAMKnobControl(bassKnobArea, kToneBass, "BASS", ampKnobStyle, ampKnobBackgroundBitmap, false, true, 0.7f,
                         AP_KNOP_OFFSET),
      -1,
      "EQ_KNOBS");
    pGraphics->AttachControl(
      new NAMKnobControl(midKnobArea, kToneMid, "MIDDLE", ampKnobStyle, ampKnobBackgroundBitmap, false, true, 0.7f,
                         AP_KNOP_OFFSET),
      -1,
      "EQ_KNOBS");
    pGraphics->AttachControl(
      new NAMKnobControl(
        trebleKnobArea, kToneTreble, "TREBLE", ampKnobStyle, ampKnobBackgroundBitmap, false, true, 0.7f,
        AP_KNOP_OFFSET),
      -1, "EQ_KNOBS");
    pGraphics->AttachControl(
      new NAMKnobControl(presenceKnobArea, kTonePresence, "PRESENCE", ampKnobStyle, ampKnobBackgroundBitmap, false, true,
                         0.7f, AP_KNOP_OFFSET),
      -1,
      "EQ_KNOBS");
    pGraphics->AttachControl(
      new NAMKnobControl(depthKnobArea, kToneDepth, "DEPTH", ampKnobStyle, ampKnobBackgroundBitmap, false, true, 0.7f,
                         AP_KNOP_OFFSET),
      -1,
      "EQ_KNOBS");
    pGraphics->AttachControl(
      new NAMKnobControl(masterKnobArea, kMasterVolume, "MASTER", ampKnobStyle, ampKnobBackgroundBitmap, false, true,
                         0.7f, AP_KNOP_OFFSET));
    pGraphics->AttachControl(new NAMKnobControl(
      hpfKnobArea, kUserHPFFrequency, "HPF", utilityStyle, outerKnobBackgroundSVG, true, false, topSideKnobScale,
      kSideLabelYOffset,
      kSideValueYOffset));
    pGraphics->AttachControl(new NAMKnobControl(
      lpfKnobArea, kUserLPFFrequency, "LPF", utilityStyle, outerKnobBackgroundSVG, true, false, topSideKnobScale,
      kSideLabelYOffset,
      kSideValueYOffset));
    pGraphics->AttachControl(new NAMKnobControl(
      outputKnobArea, kOutputLevel, "OUTPUT", utilityStyle, outerKnobBackgroundSVG, true, false, topSideKnobScale,
      kSideLabelYOffset,
      kSideValueYOffset));

    // The meters
    pGraphics->AttachControl(new NAMMeterControl(inputMeterArea, meterBackgroundBitmap, style), kCtrlTagInputMeter);
    pGraphics->AttachControl(new NAMMeterControl(outputMeterArea, meterBackgroundBitmap, style), kCtrlTagOutputMeter);

    // Settings/help/about box
    pGraphics->AttachControl(new NAMCircleButtonControl(
      settingsButtonArea,
      [pGraphics](IControl* pCaller) {
        pGraphics->GetControlWithTag(kCtrlTagSettingsBox)->As<NAMSettingsPageControl>()->HideAnimated(false);
      },
      gearSVG));

    pGraphics
      ->AttachControl(new NAMSettingsPageControl(b, settingsBackgroundBitmap, inputLevelBackgroundBitmap, switchHandleBitmap,
                                                 crossSVG, style, radioButtonStyle),
                      kCtrlTagSettingsBox)
      ->Hide(true);

    pGraphics->ForAllControlsFunc([](IControl* pControl) {
      pControl->SetMouseEventsWhenDisabled(true);
      pControl->SetMouseOverWhenDisabled(true);
    });

    mTopNavActiveSection = TopNavSection::Amp;
    mTopNavBypassed[static_cast<size_t>(TopNavSection::Tuner)] = !GetParam(kTunerActive)->Bool();
    _RefreshTopNavControls();
    _SyncTunerParamToTopNav();

    // pGraphics->GetControlWithTag(kCtrlTagOutNorm)->SetMouseEventsWhenDisabled(false);
    // pGraphics->GetControlWithTag(kCtrlTagCalibrateInput)->SetMouseEventsWhenDisabled(false);
  };
}

NeuralAmpModeler::~NeuralAmpModeler()
{
  _DeallocateIOPointers();
}

void NeuralAmpModeler::ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames)
{
  const size_t numChannelsExternalIn = (size_t)NInChansConnected();
  const size_t numChannelsExternalOut = (size_t)NOutChansConnected();
  const size_t numChannelsInternal = kNumChannelsInternal;
  const size_t numFrames = (size_t)nFrames;
  const double sampleRate = GetSampleRate();

  // Disable floating point denormals
  std::fenv_t fe_state;
  std::feholdexcept(&fe_state);
  disable_denormals();

  _PrepareBuffers(numChannelsInternal, numFrames);
  // Input is collapsed to mono in preparation for the NAM.
  _ProcessInput(inputs, numFrames, numChannelsExternalIn, numChannelsInternal);
  _ApplyDSPStaging();
  const bool noiseGateActive = GetParam(kNoiseGateActive)->Value();
  const bool toneStackActive = GetParam(kEQActive)->Value();
  const bool modelActive = GetParam(kModelToggle)->Bool();
  const bool tunerActive = GetParam(kTunerActive)->Bool();
  const double preModelGain = DBToAmp(GetParam(kPreModelGain)->Value());

  if (tunerActive)
  {
    // Capture post-input-gain mono for tuner analysis on UI thread.
    mTunerAnalyzer.PushInputMono(mInputPointers[0], numFrames);

    // 3-way tuner monitor mode while tuner is active:
    // 0 = Mute, 1 = Bypass (clean), 2 = Full processing.
    const int tunerMonitorMode = GetParam(kTunerMonitorMode)->Int();
    if (tunerMonitorMode == 0)
    {
      for (size_t c = 0; c < numChannelsExternalOut; ++c)
        std::fill(outputs[c], outputs[c] + numFrames, 0.0f);
      std::feupdateenv(&fe_state);
      _UpdateMeters(mInputPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
      return;
    }
    if (tunerMonitorMode == 1)
    {
      // Clean bypass while tuning, using post-input-gain mono signal.
      std::feupdateenv(&fe_state);
      _ProcessOutput(mInputPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
      _UpdateMeters(mInputPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
      return;
    }
    // tunerMonitorMode == 2 -> fall through to full processing path.
  }

  // Noise gate trigger
  sample** triggerOutput = mInputPointers;
  if (noiseGateActive)
  {
    const double time = 0.01;
    const double threshold = GetParam(kNoiseGateThreshold)->Value(); // GetParam...
    const double ratio = 0.1; // Quadratic...
    const double openTime = 0.005;
    const double holdTime = 0.01;
    const double closeTime = 0.05;
    const dsp::noise_gate::TriggerParams triggerParams(time, threshold, ratio, openTime, holdTime, closeTime);
    mNoiseGateTrigger.SetParams(triggerParams);
    mNoiseGateTrigger.SetSampleRate(sampleRate);
    triggerOutput = mNoiseGateTrigger.Process(mInputPointers, numChannelsInternal, numFrames);
  }
  mNoiseGateIsAttenuating.store(noiseGateActive && mNoiseGateTrigger.IsAttenuating(12.0), std::memory_order_relaxed);

  if (modelActive && (mModel != nullptr))
  {
    if (preModelGain != 1.0)
    {
      for (size_t c = 0; c < numChannelsInternal; ++c)
        for (size_t s = 0; s < numFrames; ++s)
          triggerOutput[c][s] *= preModelGain;
    }
    mModel->process(triggerOutput, mOutputPointers, nFrames);
  }
  else
  {
    _FallbackDSP(triggerOutput, mOutputPointers, numChannelsInternal, numFrames);
  }
  // Apply the noise gate after the NAM
  sample** gateGainOutput =
    noiseGateActive ? mNoiseGateGain.Process(mOutputPointers, numChannelsInternal, numFrames) : mOutputPointers;

  sample** toneStackOutPointers = (toneStackActive && mToneStack != nullptr)
                                    ? mToneStack->Process(gateGainOutput, numChannelsInternal, nFrames)
                                    : gateGainOutput;
  if (mMasterGain != 1.0)
  {
    for (size_t c = 0; c < numChannelsInternal; ++c)
      for (size_t s = 0; s < numFrames; ++s)
        toneStackOutPointers[c][s] *= mMasterGain;
  }

  sample** irPointers = toneStackOutPointers;
  if (GetParam(kIRToggle)->Value())
  {
    const bool haveLeftIR = (mIR != nullptr);
    const bool haveRightIR = (mIRRight != nullptr);
    if (haveLeftIR && haveRightIR)
    {
      sample** irLeftPointers = mIR->Process(toneStackOutPointers, numChannelsInternal, numFrames);
      sample** irRightPointers = mIRRight->Process(toneStackOutPointers, numChannelsInternal, numFrames);
      const double blend = GetParam(kCabIRBlend)->Value() * 0.01;
      const double leftGain = 1.0 - blend;
      const double rightGain = blend;
      for (size_t s = 0; s < numFrames; ++s)
        mOutputArray[0][s] = leftGain * irLeftPointers[0][s] + rightGain * irRightPointers[0][s];
      irPointers = mOutputPointers;
    }
    else if (haveLeftIR)
    {
      irPointers = mIR->Process(toneStackOutPointers, numChannelsInternal, numFrames);
    }
    else if (haveRightIR)
    {
      irPointers = mIRRight->Process(toneStackOutPointers, numChannelsInternal, numFrames);
    }
  }

  // User post-cab filters. Cascade two 1-pole stages each for approx 12 dB/oct.
  const double userHighPassCutoffFreq = GetParam(kUserHPFFrequency)->Value();
  const recursive_linear_filter::HighPassParams userHighPassParams(sampleRate, userHighPassCutoffFreq);
  mUserHighPass1.SetParams(userHighPassParams);
  mUserHighPass2.SetParams(userHighPassParams);
  sample** userHighPassPointers1 = mUserHighPass1.Process(irPointers, numChannelsInternal, numFrames);
  sample** userHighPassPointers2 = mUserHighPass2.Process(userHighPassPointers1, numChannelsInternal, numFrames);

  const double userLowPassCutoffFreq = GetParam(kUserLPFFrequency)->Value();
  const recursive_linear_filter::LowPassParams userLowPassParams(sampleRate, userLowPassCutoffFreq);
  mUserLowPass1.SetParams(userLowPassParams);
  mUserLowPass2.SetParams(userLowPassParams);
  sample** userLowPassPointers1 = mUserLowPass1.Process(userHighPassPointers2, numChannelsInternal, numFrames);
  sample** userLowPassPointers2 = mUserLowPass2.Process(userLowPassPointers1, numChannelsInternal, numFrames);

  // And the HPF for DC offset (Issue 271)
  const double highPassCutoffFreq = kDCBlockerFrequency;
  // const double lowPassCutoffFreq = 20000.0;
  const recursive_linear_filter::HighPassParams highPassParams(sampleRate, highPassCutoffFreq);
  // const recursive_linear_filter::LowPassParams lowPassParams(sampleRate, lowPassCutoffFreq);
  mHighPass.SetParams(highPassParams);
  // mLowPass.SetParams(lowPassParams);
  sample** hpfPointers = mHighPass.Process(userLowPassPointers2, numChannelsInternal, numFrames);
  // sample** lpfPointers = mLowPass.Process(hpfPointers, numChannelsInternal, numFrames);

  // restore previous floating point state
  std::feupdateenv(&fe_state);

  // Let's get outta here
  // This is where we exit mono for whatever the output requires.
  _ProcessOutput(hpfPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
  // _ProcessOutput(lpfPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
  // * Output of input leveling (inputs -> mInputPointers),
  // * Output of output leveling (mOutputPointers -> outputs)
  _UpdateMeters(mInputPointers, outputs, numFrames, numChannelsInternal, numChannelsExternalOut);
}

void NeuralAmpModeler::OnReset()
{
  const auto sampleRate = GetSampleRate();
  const int maxBlockSize = GetBlockSize();

  // Tail is because the HPF DC blocker has a decay.
  // 10 cycles should be enough to pass the VST3 tests checking tail behavior.
  // I'm ignoring the model & IR, but it's not the end of the world.
  const int tailCycles = 10;
  SetTailSize(tailCycles * (int)(sampleRate / kDCBlockerFrequency));
  mInputSender.Reset(sampleRate);
  mOutputSender.Reset(sampleRate);
  // If there is a model or IR loaded, they need to be checked for resampling.
  _ResetModelAndIR(sampleRate, GetBlockSize());
  mToneStack->Reset(sampleRate, maxBlockSize);
  // Pre-size internal mono buffers to the current host max block size.
  // ProcessBlock() should then only write/clear active frames.
  _PrepareBuffers(kNumChannelsInternal, (size_t)maxBlockSize);
  _UpdateLatency();
}

void NeuralAmpModeler::OnIdle()
{
  mInputSender.TransmitData(*this);
  mOutputSender.TransmitData(*this);

  if (auto* pGraphics = GetUI())
  {
    const bool tunerActive = GetParam(kTunerActive)->Bool();
    if (tunerActive)
      mTunerAnalyzer.Update(GetSampleRate());

    if (auto* pTunerDisplay = dynamic_cast<NAMTunerDisplayControl*>(pGraphics->GetControlWithTag(kCtrlTagTunerReadout)))
    {
      const bool hasPitch = tunerActive && mTunerAnalyzer.HasPitch();
      const int midi = hasPitch ? mTunerAnalyzer.MidiNote() : 0;
      const float cents = hasPitch ? mTunerAnalyzer.Cents() : 0.0f;
      pTunerDisplay->SetTunerState(tunerActive, hasPitch, midi, cents);
    }
  }

  const bool noiseGateIsAttenuating = mNoiseGateIsAttenuating.load(std::memory_order_relaxed);
  if (noiseGateIsAttenuating != mNoiseGateLEDState)
  {
    if (auto* pGraphics = GetUI())
    {
      if (auto* pNoiseGateLED = pGraphics->GetControlWithTag(kCtrlTagNoiseGateLED))
        pNoiseGateLED->SetValueFromDelegate(noiseGateIsAttenuating ? 1.0 : 0.0, 0);
    }
    mNoiseGateLEDState = noiseGateIsAttenuating;
  }

  if (mNewModelLoadedInDSP)
  {
    if (auto* pGraphics = GetUI())
    {
      _UpdateControlsFromModel();
      mNewModelLoadedInDSP = false;
    }
  }
  if (mModelCleared)
  {
    if (auto* pGraphics = GetUI())
    {
      // FIXME -- need to disable only the "normalized" model
      // pGraphics->GetControlWithTag(kCtrlTagOutputMode)->SetDisabled(false);
      static_cast<NAMSettingsPageControl*>(pGraphics->GetControlWithTag(kCtrlTagSettingsBox))->ClearModelInfo();
      if (GetParam(kModelToggle)->Bool())
      {
        GetParam(kModelToggle)->Set(0.0);
        SendParameterValueFromDelegate(kModelToggle, GetParam(kModelToggle)->GetNormalized(), true);
      }
      mModelCleared = false;
    }
  }
}

bool NeuralAmpModeler::SerializeState(IByteChunk& chunk) const
{
  // If this isn't here when unserializing, then we know we're dealing with something before v0.8.0.
  WDL_String header("###NeuralAmpModeler###"); // Don't change this!
  chunk.PutStr(header.Get());
  // Plugin version, so we can load legacy serialized states in the future!
  WDL_String version(PLUG_VERSION_STR);
  chunk.PutStr(version.Get());
  // Model directory (don't serialize the model itself; we'll just load it again
  // when we unserialize)
  chunk.PutStr(mNAMPath.Get());
  chunk.PutStr(mIRPath.Get()); // Left IR (legacy slot)
  chunk.PutStr(mIRPathRight.Get());
  return SerializeParams(chunk);
}

int NeuralAmpModeler::UnserializeState(const IByteChunk& chunk, int startPos)
{
  // Look for the expected header. If it's there, then we'll know what to do.
  WDL_String header;
  int pos = startPos;
  pos = chunk.GetStr(header, pos);

  const char* kExpectedHeader = "###NeuralAmpModeler###";
  if (strcmp(header.Get(), kExpectedHeader) == 0)
  {
    return _UnserializeStateWithKnownVersion(chunk, pos);
  }
  else
  {
    return _UnserializeStateWithUnknownVersion(chunk, startPos);
  }
}

void NeuralAmpModeler::OnUIOpen()
{
  Plugin::OnUIOpen();

  if (mNAMPath.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPath.GetLength(), mNAMPath.Get());
    // If it's not loaded yet, then mark as failed.
    // If it's yet to be loaded, then the completion handler will set us straight once it runs.
    if (mModel == nullptr && mStagedModel == nullptr)
      SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);
  }

  if (mIRPath.GetLength())
  {
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowserLeft, kMsgTagLoadedIRLeft, mIRPath.GetLength(), mIRPath.Get());
    if (mIR == nullptr && mStagedIR == nullptr)
      SendControlMsgFromDelegate(kCtrlTagIRFileBrowserLeft, kMsgTagLoadFailed);
  }
  if (mIRPathRight.GetLength())
  {
    SendControlMsgFromDelegate(
      kCtrlTagIRFileBrowserRight, kMsgTagLoadedIRRight, mIRPathRight.GetLength(), mIRPathRight.Get());
    if (mIRRight == nullptr && mStagedIRRight == nullptr)
      SendControlMsgFromDelegate(kCtrlTagIRFileBrowserRight, kMsgTagLoadFailed);
  }

  if (mModel != nullptr)
  {
    _UpdateControlsFromModel();
  }

  // If no model is available, force model toggle to OFF.
  if (mModel == nullptr && mStagedModel == nullptr && GetParam(kModelToggle)->Bool())
  {
    GetParam(kModelToggle)->Set(0.0);
    SendParameterValueFromDelegate(kModelToggle, GetParam(kModelToggle)->GetNormalized(), true);
  }

  if (GetParam(kTunerActive)->Bool())
  {
    mTopNavBypassed[static_cast<size_t>(TopNavSection::Tuner)] = false;
  }

  _RefreshTopNavControls();
}

void NeuralAmpModeler::OnParamChange(int paramIdx)
{
  switch (paramIdx)
  {
    // Changes to the input gain
    case kCalibrateInput:
    case kInputCalibrationLevel:
    case kInputLevel: _SetInputGain(); break;
    // Changes to the output gain
    case kOutputLevel:
    case kOutputMode: _SetOutputGain(); break;
    case kMasterVolume: _SetMasterGain(); break;
    case kTunerActive: mTunerAnalyzer.Reset(); break;
    // Tone stack:
    case kToneBass: mToneStack->SetParam("bass", GetParam(paramIdx)->Value()); break;
    case kToneMid: mToneStack->SetParam("middle", GetParam(paramIdx)->Value()); break;
    case kToneTreble: mToneStack->SetParam("treble", GetParam(paramIdx)->Value()); break;
    case kTonePresence: mToneStack->SetParam("presence", GetParam(paramIdx)->Value()); break;
    case kToneDepth: mToneStack->SetParam("depth", GetParam(paramIdx)->Value()); break;
    default: break;
  }
}

void NeuralAmpModeler::OnParamChangeUI(int paramIdx, EParamSource source)
{
  if (auto pGraphics = GetUI())
  {
    bool active = GetParam(paramIdx)->Bool();

    switch (paramIdx)
    {
      case kNoiseGateActive: pGraphics->GetControlWithParamIdx(kNoiseGateThreshold)->SetDisabled(!active); break;
      case kEQActive:
        pGraphics->ForControlInGroup("EQ_KNOBS", [active](IControl* pControl) { pControl->SetDisabled(!active); });
        break;
      case kIRToggle:
        pGraphics->GetControlWithTag(kCtrlTagIRFileBrowserLeft)->SetDisabled(!active);
        pGraphics->GetControlWithTag(kCtrlTagIRFileBrowserRight)->SetDisabled(!active);
        pGraphics->GetControlWithParamIdx(kCabIRBlend)->SetDisabled(!active);
        break;
      case kModelToggle:
        if (active && (mModel == nullptr) && (mStagedModel == nullptr))
        {
          WDL_String fileName;
          WDL_String path;
          if (mNAMPath.GetLength())
          {
            path.Set(mNAMPath.Get());
            path.remove_filepart();
          }
          pGraphics->PromptForFile(
            fileName, path, EFileAction::Open, "nam", [this](const WDL_String& chosenFileName, const WDL_String&) {
              if (chosenFileName.GetLength())
              {
                const std::string msg = _StageModel(chosenFileName);
                if (msg.size())
                {
                  std::stringstream ss;
                  ss << "Failed to load NAM model. Message:\n\n" << msg;
                  _ShowMessageBox(GetUI(), ss.str().c_str(), "Failed to load model!", kMB_OK);
                  GetParam(kModelToggle)->Set(0.0);
                }
                else
                  GetParam(kModelToggle)->Set(1.0);
              }
              else
              {
                GetParam(kModelToggle)->Set(0.0);
              }
              SendParameterValueFromDelegate(kModelToggle, GetParam(kModelToggle)->GetNormalized(), true);
            });
        }
        break;
      case kTunerActive:
      {
        const auto tunerIdx = static_cast<size_t>(TopNavSection::Tuner);
        mTopNavBypassed[tunerIdx] = !active;
        _RefreshTopNavControls();
        break;
      }
      default: break;
    }
  }
}

bool NeuralAmpModeler::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  switch (msgTag)
  {
    case kMsgTagClearModel: mShouldRemoveModel = true; return true;
    case kMsgTagClearIRLeft: mShouldRemoveIRLeft = true; return true;
    case kMsgTagClearIRRight: mShouldRemoveIRRight = true; return true;
    case kMsgTagHighlightColor:
    {
      mHighLightColor.Set((const char*)pData);

      if (GetUI())
      {
        GetUI()->ForStandardControlsFunc([&](IControl* pControl) {
          if (auto* pVectorBase = pControl->As<IVectorBase>())
          {
            IColor color = IColor::FromColorCodeStr(mHighLightColor.Get());

            pVectorBase->SetColor(kX1, color);
            pVectorBase->SetColor(kPR, color.WithOpacity(0.3f));
            pVectorBase->SetColor(kFR, color.WithOpacity(0.4f));
            pVectorBase->SetColor(kX3, color.WithContrast(0.1f));
          }
          pControl->GetUI()->SetAllControlsDirty();
        });
      }

      return true;
    }
    default: return false;
  }
}

// Private methods ============================================================

void NeuralAmpModeler::_SetTopNavActiveSection(const TopNavSection section)
{
  const auto idx = static_cast<size_t>(section);
  if (idx >= mTopNavBypassed.size())
    return;

  if (section == TopNavSection::Tuner)
  {
    mTopNavBypassed[idx] = false;
    _SyncTunerParamToTopNav();
    _RefreshTopNavControls();
    return;
  }

  mTopNavActiveSection = section;
  _SyncTunerParamToTopNav();
  _RefreshTopNavControls();
}

void NeuralAmpModeler::_ToggleTopNavSectionBypass(const TopNavSection section)
{
  const auto idx = static_cast<size_t>(section);
  if (idx >= mTopNavBypassed.size())
    return;

  mTopNavBypassed[idx] = !mTopNavBypassed[idx];

  _SyncTunerParamToTopNav();
  _RefreshTopNavControls();
}

void NeuralAmpModeler::_RefreshTopNavControls()
{
  if (auto* pGraphics = GetUI())
  {
    const auto tunerIdx = static_cast<size_t>(TopNavSection::Tuner);
    const bool tunerActive = !mTopNavBypassed[tunerIdx];
    const bool showAmpSection = (mTopNavActiveSection == TopNavSection::Amp);
    const bool showCabSection = (mTopNavActiveSection == TopNavSection::Cab);
    const auto updateIcon = [&](const int tag, const TopNavSection section) {
      if (auto* pIcon = dynamic_cast<NAMTopIconControl*>(pGraphics->GetControlWithTag(tag)))
      {
        const auto idx = static_cast<size_t>(section);
        if (section == TopNavSection::Tuner)
          pIcon->SetVisualState(tunerActive, false);
        else
          pIcon->SetVisualState(mTopNavActiveSection == section, mTopNavBypassed[idx]);
      }
    };

    updateIcon(kCtrlTagTopNavAmp, TopNavSection::Amp);
    updateIcon(kCtrlTagTopNavStomp, TopNavSection::Stomp);
    updateIcon(kCtrlTagTopNavCab, TopNavSection::Cab);
    updateIcon(kCtrlTagTopNavFx, TopNavSection::Fx);
    updateIcon(kCtrlTagTopNavTuner, TopNavSection::Tuner);

    const char* backgroundResource = BACKGROUND_FN;
    if (mTopNavActiveSection == TopNavSection::Stomp)
      backgroundResource = STOMPBACKGROUND_FN;
    else if (mTopNavActiveSection == TopNavSection::Cab)
      backgroundResource = CABBACKGROUND_FN;
    else if (mTopNavActiveSection == TopNavSection::Fx)
      backgroundResource = FXBACKGROUND_FN;
    if (auto* pBackground = dynamic_cast<NAMBackgroundBitmapControl*>(pGraphics->GetControlWithTag(kCtrlTagMainBackground)))
      pBackground->SetResourceName(backgroundResource);

    const bool showTunerReadout = tunerActive;
    if (auto* pTunerReadout = pGraphics->GetControlWithTag(kCtrlTagTunerReadout))
      pTunerReadout->Hide(!showTunerReadout);
    if (auto* pTunerMute = pGraphics->GetControlWithTag(kCtrlTagTunerMute))
      pTunerMute->Hide(!showTunerReadout);
    if (auto* pTunerClose = pGraphics->GetControlWithTag(kCtrlTagTunerClose))
      pTunerClose->Hide(!showTunerReadout);

    if (auto* pModelBrowser = pGraphics->GetControlWithTag(kCtrlTagModelFileBrowser))
      pModelBrowser->Hide(!showAmpSection);
    if (auto* pModelToggle = pGraphics->GetControlWithParamIdx(kModelToggle))
      pModelToggle->Hide(!showAmpSection);
    if (auto* pNoiseGateLED = pGraphics->GetControlWithTag(kCtrlTagNoiseGateLED))
      pNoiseGateLED->Hide(!showAmpSection);

    const auto hideAmpParamControl = [&](const int paramIdx) {
      if (auto* pControl = pGraphics->GetControlWithParamIdx(paramIdx))
        pControl->Hide(!showAmpSection);
    };
    hideAmpParamControl(kNoiseGateThreshold);
    hideAmpParamControl(kPreModelGain);
    hideAmpParamControl(kToneBass);
    hideAmpParamControl(kToneMid);
    hideAmpParamControl(kToneTreble);
    hideAmpParamControl(kTonePresence);
    hideAmpParamControl(kToneDepth);
    hideAmpParamControl(kMasterVolume);

    if (auto* pIRToggle = pGraphics->GetControlWithTag(kCtrlTagIRToggle))
      pIRToggle->Hide(!showCabSection);
    if (auto* pIRLeft = pGraphics->GetControlWithTag(kCtrlTagIRFileBrowserLeft))
      pIRLeft->Hide(!showCabSection);
    if (auto* pIRRight = pGraphics->GetControlWithTag(kCtrlTagIRFileBrowserRight))
      pIRRight->Hide(!showCabSection);
    if (auto* pCabBlend = pGraphics->GetControlWithParamIdx(kCabIRBlend))
      pCabBlend->Hide(!showCabSection);

    const auto updateAmpSlot = [&](const int tag, const int slotIndex) {
      if (auto* pAmpSlot = dynamic_cast<NAMTopIconControl*>(pGraphics->GetControlWithTag(tag)))
      {
        pAmpSlot->Hide(!showAmpSection);
        const bool isSelected = showAmpSection && (mAmpSelectorIndex == slotIndex);
        const bool dimUnselected = showAmpSection && (mAmpSelectorIndex != slotIndex);
        pAmpSlot->SetVisualState(isSelected, dimUnselected);
      }
    };
    updateAmpSlot(kCtrlTagAmpSlot1, 0);
    updateAmpSlot(kCtrlTagAmpSlot2, 1);
    updateAmpSlot(kCtrlTagAmpSlot3, 2);
  }
}

void NeuralAmpModeler::_SyncTunerParamToTopNav()
{
  const bool shouldTunerBeActive = !mTopNavBypassed[static_cast<size_t>(TopNavSection::Tuner)];

  if (GetParam(kTunerActive)->Bool() != shouldTunerBeActive)
  {
    GetParam(kTunerActive)->Set(shouldTunerBeActive ? 1.0 : 0.0);
    SendParameterValueFromDelegate(kTunerActive, GetParam(kTunerActive)->GetNormalized(), true);
    OnParamChange(kTunerActive);
  }
}

void NeuralAmpModeler::_AllocateIOPointers(const size_t nChans)
{
  if (mInputPointers != nullptr)
    throw std::runtime_error("Tried to re-allocate mInputPointers without freeing");
  mInputPointers = new sample*[nChans];
  if (mInputPointers == nullptr)
    throw std::runtime_error("Failed to allocate pointer to input buffer!\n");
  if (mOutputPointers != nullptr)
    throw std::runtime_error("Tried to re-allocate mOutputPointers without freeing");
  mOutputPointers = new sample*[nChans];
  if (mOutputPointers == nullptr)
    throw std::runtime_error("Failed to allocate pointer to output buffer!\n");
}

void NeuralAmpModeler::_ApplyDSPStaging()
{
  // Remove marked modules
  if (mShouldRemoveModel)
  {
    mModel = nullptr;
    mNAMPath.Set("");
    mShouldRemoveModel = false;
    mModelCleared = true;
    _UpdateLatency();
    _SetInputGain();
    _SetOutputGain();
  }
  if (mShouldRemoveIRLeft)
  {
    mIR = nullptr;
    mIRPath.Set("");
    mShouldRemoveIRLeft = false;
  }
  if (mShouldRemoveIRRight)
  {
    mIRRight = nullptr;
    mIRPathRight.Set("");
    mShouldRemoveIRRight = false;
  }
  // Move things from staged to live
  if (mStagedModel != nullptr)
  {
    mModel = std::move(mStagedModel);
    mStagedModel = nullptr;
    mNewModelLoadedInDSP = true;
    _UpdateLatency();
    _SetInputGain();
    _SetOutputGain();
  }
  if (mStagedIR != nullptr)
  {
    mIR = std::move(mStagedIR);
    mStagedIR = nullptr;
  }
  if (mStagedIRRight != nullptr)
  {
    mIRRight = std::move(mStagedIRRight);
    mStagedIRRight = nullptr;
  }
}

void NeuralAmpModeler::_DeallocateIOPointers()
{
  if (mInputPointers != nullptr)
  {
    delete[] mInputPointers;
    mInputPointers = nullptr;
  }
  if (mInputPointers != nullptr)
    throw std::runtime_error("Failed to deallocate pointer to input buffer!\n");
  if (mOutputPointers != nullptr)
  {
    delete[] mOutputPointers;
    mOutputPointers = nullptr;
  }
  if (mOutputPointers != nullptr)
    throw std::runtime_error("Failed to deallocate pointer to output buffer!\n");
}

void NeuralAmpModeler::_FallbackDSP(iplug::sample** inputs, iplug::sample** outputs, const size_t numChannels,
                                    const size_t numFrames)
{
  for (auto c = 0; c < numChannels; c++)
    for (auto s = 0; s < numFrames; s++)
      mOutputArray[c][s] = mInputArray[c][s];
}

void NeuralAmpModeler::_ResetModelAndIR(const double sampleRate, const int maxBlockSize)
{
  // Model
  if (mStagedModel != nullptr)
  {
    mStagedModel->Reset(sampleRate, maxBlockSize);
  }
  else if (mModel != nullptr)
  {
    mModel->Reset(sampleRate, maxBlockSize);
  }

  // IR
  if (mStagedIR != nullptr)
  {
    const double irSampleRate = mStagedIR->GetSampleRate();
    if (irSampleRate != sampleRate)
    {
      const auto irData = mStagedIR->GetData();
      mStagedIR = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
    }
  }
  else if (mIR != nullptr)
  {
    const double irSampleRate = mIR->GetSampleRate();
    if (irSampleRate != sampleRate)
    {
      const auto irData = mIR->GetData();
      mStagedIR = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
    }
  }
  if (mStagedIRRight != nullptr)
  {
    const double irSampleRate = mStagedIRRight->GetSampleRate();
    if (irSampleRate != sampleRate)
    {
      const auto irData = mStagedIRRight->GetData();
      mStagedIRRight = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
    }
  }
  else if (mIRRight != nullptr)
  {
    const double irSampleRate = mIRRight->GetSampleRate();
    if (irSampleRate != sampleRate)
    {
      const auto irData = mIRRight->GetData();
      mStagedIRRight = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
    }
  }
}

void NeuralAmpModeler::_SetInputGain()
{
  iplug::sample inputGainDB = GetParam(kInputLevel)->Value();
  // Input calibration
  if ((mModel != nullptr) && (mModel->HasInputLevel()) && GetParam(kCalibrateInput)->Bool())
  {
    inputGainDB += GetParam(kInputCalibrationLevel)->Value() - mModel->GetInputLevel();
  }
  mInputGain = DBToAmp(inputGainDB);
}

void NeuralAmpModeler::_SetOutputGain()
{
  double gainDB = GetParam(kOutputLevel)->Value();
  if (mModel != nullptr)
  {
    const int outputMode = GetParam(kOutputMode)->Int();
    switch (outputMode)
    {
      case 1: // Normalized
        if (mModel->HasLoudness())
        {
          const double loudness = mModel->GetLoudness();
          const double targetLoudness = -18.0;
          gainDB += (targetLoudness - loudness);
        }
        break;
      case 2: // Calibrated
        if (mModel->HasOutputLevel())
        {
          const double inputLevel = GetParam(kInputCalibrationLevel)->Value();
          const double outputLevel = mModel->GetOutputLevel();
          gainDB += (outputLevel - inputLevel);
        }
        break;
      case 0: // Raw
      default: break;
    }
  }
  mOutputGain = DBToAmp(gainDB);
}

void NeuralAmpModeler::_SetMasterGain()
{
  const double value = GetParam(kMasterVolume)->Value();
  const double masterGainDB = (value <= 5.0) ? (-40.0 + (value / 5.0) * 40.0) : (((value - 5.0) / 5.0) * 12.0);
  mMasterGain = DBToAmp(masterGainDB);
}

std::string NeuralAmpModeler::_StageModel(const WDL_String& modelPath)
{
  WDL_String previousNAMPath = mNAMPath;
  try
  {
    auto dspPath = std::filesystem::u8path(modelPath.Get());
    std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);
    std::unique_ptr<ResamplingNAM> temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), GetBlockSize());
    mStagedModel = std::move(temp);
    mNAMPath = modelPath;
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadedModel, mNAMPath.GetLength(), mNAMPath.Get());
  }
  catch (std::runtime_error& e)
  {
    SendControlMsgFromDelegate(kCtrlTagModelFileBrowser, kMsgTagLoadFailed);

    if (mStagedModel != nullptr)
    {
      mStagedModel = nullptr;
    }
    mNAMPath = previousNAMPath;
    std::cerr << "Failed to read DSP module" << std::endl;
    std::cerr << e.what() << std::endl;
    return e.what();
  }
  return "";
}

dsp::wav::LoadReturnCode NeuralAmpModeler::_StageIRLeft(const WDL_String& irPath)
{
  // FIXME it'd be better for the path to be "staged" as well. Just in case the
  // path and the model got caught on opposite sides of the fence...
  WDL_String previousIRPath = mIRPath;
  const double sampleRate = GetSampleRate();
  dsp::wav::LoadReturnCode wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
  try
  {
    auto irPathU8 = std::filesystem::u8path(irPath.Get());
    mStagedIR = std::make_unique<dsp::ImpulseResponse>(irPathU8.string().c_str(), sampleRate);
    wavState = mStagedIR->GetWavState();
  }
  catch (std::runtime_error& e)
  {
    wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
    std::cerr << "Caught unhandled exception while attempting to load IR:" << std::endl;
    std::cerr << e.what() << std::endl;
  }

  if (wavState == dsp::wav::LoadReturnCode::SUCCESS)
  {
    mIRPath = irPath;
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowserLeft, kMsgTagLoadedIRLeft, mIRPath.GetLength(), mIRPath.Get());
  }
  else
  {
    if (mStagedIR != nullptr)
    {
      mStagedIR = nullptr;
    }
    mIRPath = previousIRPath;
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowserLeft, kMsgTagLoadFailed);
  }

  return wavState;
}

dsp::wav::LoadReturnCode NeuralAmpModeler::_StageIRRight(const WDL_String& irPath)
{
  WDL_String previousIRPath = mIRPathRight;
  const double sampleRate = GetSampleRate();
  dsp::wav::LoadReturnCode wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
  try
  {
    auto irPathU8 = std::filesystem::u8path(irPath.Get());
    mStagedIRRight = std::make_unique<dsp::ImpulseResponse>(irPathU8.string().c_str(), sampleRate);
    wavState = mStagedIRRight->GetWavState();
  }
  catch (std::runtime_error& e)
  {
    wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
    std::cerr << "Caught unhandled exception while attempting to load right IR:" << std::endl;
    std::cerr << e.what() << std::endl;
  }

  if (wavState == dsp::wav::LoadReturnCode::SUCCESS)
  {
    mIRPathRight = irPath;
    SendControlMsgFromDelegate(
      kCtrlTagIRFileBrowserRight, kMsgTagLoadedIRRight, mIRPathRight.GetLength(), mIRPathRight.Get());
  }
  else
  {
    if (mStagedIRRight != nullptr)
      mStagedIRRight = nullptr;
    mIRPathRight = previousIRPath;
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowserRight, kMsgTagLoadFailed);
  }

  return wavState;
}

size_t NeuralAmpModeler::_GetBufferNumChannels() const
{
  // Assumes input=output (no mono->stereo effects)
  return mInputArray.size();
}

size_t NeuralAmpModeler::_GetBufferNumFrames() const
{
  if (_GetBufferNumChannels() == 0)
    return 0;
  return mInputArray[0].size();
}

void NeuralAmpModeler::_InitToneStack()
{
  // If you want to customize the tone stack, then put it here!
  mToneStack = std::make_unique<dsp::tone_stack::BasicNamToneStack>();
}
void NeuralAmpModeler::_PrepareBuffers(const size_t numChannels, const size_t numFrames)
{
  const bool updateChannels = numChannels != _GetBufferNumChannels();
  const bool growFrames = updateChannels || (_GetBufferNumFrames() < numFrames);

  if (updateChannels)
  {
    _PrepareIOPointers(numChannels);
    mInputArray.resize(numChannels);
    mOutputArray.resize(numChannels);
  }
  if (growFrames)
  {
    for (auto c = 0; c < mInputArray.size(); c++)
      mInputArray[c].resize(numFrames);
    for (auto c = 0; c < mOutputArray.size(); c++)
      mOutputArray[c].resize(numFrames);
  }
  // Always clear only the active frame range for this block.
  for (auto c = 0; c < mInputArray.size(); c++)
    std::fill_n(mInputArray[c].begin(), numFrames, 0.0);
  for (auto c = 0; c < mOutputArray.size(); c++)
    std::fill_n(mOutputArray[c].begin(), numFrames, 0.0);

  // Would these ever get changed by something?
  for (auto c = 0; c < mInputArray.size(); c++)
    mInputPointers[c] = mInputArray[c].data();
  for (auto c = 0; c < mOutputArray.size(); c++)
    mOutputPointers[c] = mOutputArray[c].data();
}

void NeuralAmpModeler::_PrepareIOPointers(const size_t numChannels)
{
  _DeallocateIOPointers();
  _AllocateIOPointers(numChannels);
}

void NeuralAmpModeler::_ProcessInput(iplug::sample** inputs, const size_t nFrames, const size_t nChansIn,
                                     const size_t nChansOut)
{
  // We'll assume that the main processing is mono for now. We'll handle dual amps later.
  if (nChansOut != 1)
    return;

  // On the standalone, we can probably assume that the user has plugged into only one input and they expect it to be
  // carried straight through. Don't apply any division over nChansIn because we're just "catching anything out there."
  // However, in a DAW, it's probably something providing stereo, and we want to take the average in order to avoid
  // doubling the loudness. (This would change w/ double mono processing)
  double gain = mInputGain;
#ifndef APP_API
  gain /= (float)nChansIn;
#endif
  // Assume _PrepareBuffers() was already called
  for (size_t c = 0; c < nChansIn; c++)
    for (size_t s = 0; s < nFrames; s++)
      if (c == 0)
        mInputArray[0][s] = gain * inputs[c][s];
      else
        mInputArray[0][s] += gain * inputs[c][s];
}

void NeuralAmpModeler::_ProcessOutput(iplug::sample** inputs, iplug::sample** outputs, const size_t nFrames,
                                      const size_t nChansIn, const size_t nChansOut)
{
  const double gain = mOutputGain;
  // Assume _PrepareBuffers() was already called
  if (nChansIn != 1)
  {
    for (auto cout = 0; cout < nChansOut; cout++)
      for (auto s = 0; s < nFrames; s++)
        outputs[cout][s] = 0.0;
    return;
  }
  // Broadcast the internal mono stream to all output channels.
  const size_t cin = 0;
  for (auto cout = 0; cout < nChansOut; cout++)
    for (auto s = 0; s < nFrames; s++)
#ifdef APP_API // Ensure valid output to interface
      outputs[cout][s] = std::clamp(gain * inputs[cin][s], -1.0, 1.0);
#else // In a DAW, other things may come next and should be able to handle large
      // values.
      outputs[cout][s] = gain * inputs[cin][s];
#endif
}

void NeuralAmpModeler::_UpdateControlsFromModel()
{
  if (mModel == nullptr)
  {
    return;
  }
  if (auto* pGraphics = GetUI())
  {
    ModelInfo modelInfo;
    modelInfo.sampleRate.known = true;
    modelInfo.sampleRate.value = mModel->GetEncapsulatedSampleRate();
    modelInfo.inputCalibrationLevel.known = mModel->HasInputLevel();
    modelInfo.inputCalibrationLevel.value = mModel->HasInputLevel() ? mModel->GetInputLevel() : 0.0;
    modelInfo.outputCalibrationLevel.known = mModel->HasOutputLevel();
    modelInfo.outputCalibrationLevel.value = mModel->HasOutputLevel() ? mModel->GetOutputLevel() : 0.0;

    static_cast<NAMSettingsPageControl*>(pGraphics->GetControlWithTag(kCtrlTagSettingsBox))->SetModelInfo(modelInfo);

    const bool disableInputCalibrationControls = !mModel->HasInputLevel();
    pGraphics->GetControlWithTag(kCtrlTagCalibrateInput)->SetDisabled(disableInputCalibrationControls);
    pGraphics->GetControlWithTag(kCtrlTagInputCalibrationLevel)->SetDisabled(disableInputCalibrationControls);
    {
      auto* c = static_cast<OutputModeControl*>(pGraphics->GetControlWithTag(kCtrlTagOutputMode));
      c->SetNormalizedDisable(!mModel->HasLoudness());
      c->SetCalibratedDisable(!mModel->HasOutputLevel());
    }
  }
}

void NeuralAmpModeler::_UpdateLatency()
{
  int latency = 0;
  if (mModel)
  {
    latency += mModel->GetLatency();
  }
  // Other things that add latency here...

  // Feels weird to have to do this.
  if (GetLatency() != latency)
  {
    SetLatency(latency);
  }
}

void NeuralAmpModeler::_UpdateMeters(sample** inputPointer, sample** outputPointer, const size_t nFrames,
                                     const size_t nChansIn, const size_t nChansOut)
{
  // Right now, we didn't specify MAXNC when we initialized these, so it's 1.
  const int nChansHack = 1;
  mInputSender.ProcessBlock(inputPointer, (int)nFrames, kCtrlTagInputMeter, nChansHack);
  mOutputSender.ProcessBlock(outputPointer, (int)nFrames, kCtrlTagOutputMeter, nChansHack);
}

// HACK
#include "Unserialization.cpp"
