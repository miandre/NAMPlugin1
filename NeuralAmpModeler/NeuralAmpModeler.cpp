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

#if __has_include("third_party/rubberband/single/RubberBandSingle.cpp")
// Build Rubber Band as a single translation unit without modifying project files.
#include "third_party/rubberband/single/RubberBandSingle.cpp"
#endif

using namespace iplug;
using namespace igraphics;

const double kDCBlockerFrequency = 5.0;
constexpr double kPi = 3.14159265358979323846;

namespace
{
constexpr int kAmpSlotSwitchDeClickSamples = 96;

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
const IVStyle fxEqSliderStyle =
  utilityStyle.WithShowValue(false)
    .WithColor(EVColor::kFG, COLOR_DARK_GRAY.WithOpacity(0.95f))
    .WithColor(EVColor::kFR, COLOR_DARK_GRAY.WithOpacity(0.85f))
    .WithColor(EVColor::kPR, COLOR_DARK_GRAY.WithOpacity(0.75f))
    .WithColor(EVColor::kHL, COLOR_DARK_GRAY.WithOpacity(0.80f))
    .WithColor(EVColor::kX1, COLOR_DARK_GRAY.WithOpacity(0.95f))
    .WithLabelText(IText(DEFAULT_TEXT_SIZE - 1.f, COLOR_BLACK.WithOpacity(0.90f), "ArialNarrow-Bold", EAlign::Center, EVAlign::Middle))
    .WithValueText(IText(DEFAULT_TEXT_SIZE - 1.f, COLOR_GRAY.WithOpacity(0.85f), "ArialNarrow-Bold", EAlign::Center, EVAlign::Bottom));
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
  GetParam(kTransposeSemitones)->InitDouble("Transpose", 0.0, -8.0, 8.0, 1.0, "");
  GetParam(kOutputLevel)->InitGain("Output", 0.0, -40.0, 40.0, 0.1);
  GetParam(kNoiseGateThreshold)->InitGain("Threshold", -50.0, -80.0, 0.0, 0.1);
  GetParam(kNoiseGateReleaseMs)->InitDouble("Gate Release", 40.0, 1.0, 100.0, 1.0, "");
  GetParam(kNoiseGateActive)->InitBool("NoiseGateActive", true);
  GetParam(kStompBoostLevel)->InitGain("Boost Level", 0.0, -20.0, 20.0, 0.1);
  GetParam(kStompBoostActive)->InitBool("BoostActive", false);
  GetParam(kFXEQActive)->InitBool("FX EQ", false);
  GetParam(kFXEQBand31Hz)->InitDouble("FX EQ 31Hz", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kFXEQBand62Hz)->InitDouble("FX EQ 62Hz", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kFXEQBand125Hz)->InitDouble("FX EQ 125Hz", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kFXEQBand250Hz)->InitDouble("FX EQ 250Hz", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kFXEQBand500Hz)->InitDouble("FX EQ 500Hz", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kFXEQBand1kHz)->InitDouble("FX EQ 1kHz", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kFXEQBand2kHz)->InitDouble("FX EQ 2kHz", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kFXEQBand4kHz)->InitDouble("FX EQ 4kHz", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kFXEQBand8kHz)->InitDouble("FX EQ 8kHz", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kFXEQBand16kHz)->InitDouble("FX EQ 16kHz", 0.0, -12.0, 12.0, 0.1, "dB");
  GetParam(kFXEQOutputGain)->InitGain("FX EQ Out", 0.0, -18.0, 18.0, 0.1);
  GetParam(kFXDelayActive)->InitBool("FX Delay", false);
  GetParam(kFXDelayMix)->InitDouble("FX Delay Mix", 25.0, 0.0, 100.0, 0.1, "%");
  GetParam(kFXDelayTimeMs)->InitDouble("FX Delay Time", 420.0, 1.0, 2000.0, 1.0, "ms");
  GetParam(kFXDelayFeedback)->InitDouble("FX Delay Feedback", 35.0, 0.0, 80.0, 0.1, "%");
  GetParam(kFXReverbActive)->InitBool("FX Reverb", false);
  GetParam(kFXReverbMix)->InitDouble("FX Reverb Mix", 20.0, 0.0, 100.0, 0.1, "%");
  GetParam(kFXReverbDecay)->InitDouble("FX Reverb Decay", 1.8, 0.1, 10.0, 0.1, "s");
  GetParam(kFXReverbPreDelayMs)->InitDouble("FX Reverb PreDelay", 25.0, 0.0, 250.0, 1.0, "ms");
  GetParam(kFXReverbTone)->InitDouble("FX Reverb Tone", 50.0, 0.0, 100.0, 0.1, "%");
  GetParam(kFXDelayLowCutHz)->InitDouble("FX Delay LoCut", 120.0, 20.0, 2000.0, 1.0, "Hz");
  GetParam(kFXDelayHighCutHz)->InitDouble("FX Delay HiCut", 12000.0, 1000.0, 20000.0, 10.0, "Hz");
  GetParam(kFXReverbLowCutHz)->InitDouble("FX Reverb LoCut", 120.0, 20.0, 2000.0, 1.0, "Hz");
  GetParam(kFXReverbHighCutHz)->InitDouble("FX Reverb HiCut", 12000.0, 1000.0, 20000.0, 10.0, "Hz");
  GetParam(kFXReverbMode)->InitBool("FX Reverb Hall", true);
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

    const auto amp2BackgroundBitmap = pGraphics->LoadBitmap(AMP2BACKGROUND_FN);
    const auto settingsBackgroundBitmap = pGraphics->LoadBitmap(SETTINGSBACKGROUND_FN);
    const auto fileBackgroundBitmap = pGraphics->LoadBitmap(FILEBACKGROUND_FN);
    const auto inputLevelBackgroundBitmap = pGraphics->LoadBitmap(INPUTLEVELBACKGROUND_FN);
    const auto linesBitmap = pGraphics->LoadBitmap(LINES_FN);
    const auto ampKnobBackgroundBitmap = pGraphics->LoadBitmap(KNOBBACKGROUND_FN);
    const auto switchOffBitmap = pGraphics->LoadBitmap(SWITCH_OFF_FN);
    const auto switchOnBitmap = pGraphics->LoadBitmap(SWITCH_ON_FN);
    const auto switchHandleBitmap = pGraphics->LoadBitmap(SLIDESWITCHHANDLE_FN);
    const auto meterBackgroundBitmap = pGraphics->LoadBitmap(METERBACKGROUND_FN);
    const auto pedalKnobBitmap = pGraphics->LoadBitmap(PEDALKNOB_FN);
    const auto pedalKnobShadowBitmap = pGraphics->LoadBitmap(PEDALKNOBSHADOW_FN);
    const auto stompButtonUpBitmap = pGraphics->LoadBitmap(STOMPBUTTONUP_FN);
    const auto stompButtonDownBitmap = pGraphics->LoadBitmap(STOMPBUTTONDOWN_FN);
    const auto greenLedOnBitmap = pGraphics->LoadBitmap(GREENLEDON_FN);
    const auto greenLedOffBitmap = pGraphics->LoadBitmap(GREENLEDOFF_FN);
    const auto redLedOnBitmap = pGraphics->LoadBitmap(REDLEDON_FN);
    const auto redLedOffBitmap = pGraphics->LoadBitmap(REDLEDOFF_FN);

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
    constexpr float kPedalKnobScale = 0.15f;
    auto makePedalKnobArea = [&](const float centerX, const float centerY) {
      const float w = pedalKnobBitmap.IsValid() ? static_cast<float>(pedalKnobBitmap.W()) * kPedalKnobScale : knobWidth;
      const float h = pedalKnobBitmap.IsValid() ? static_cast<float>(pedalKnobBitmap.H()) * kPedalKnobScale : knobWidth;
      constexpr float kPedalLabelValuePad = 38.0f;
      return IRECT(centerX - 0.5f * w, centerY - 0.5f * h, centerX + 0.5f * w, centerY + 0.5f * h + kPedalLabelValuePad);
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
    const float leftTransposeCenterX = leftInputCenterX + topSideFilterGapX;
    const float leftFilterCenterX = leftTransposeCenterX + topSideFilterGapX;
    const float rightFilterCenterX = rightOutputCenterX - topSideFilterGapX;

    const auto inputKnobArea = makeKnobArea(leftInputCenterX, topSideKnobTop);
    const auto transposeKnobArea = makeKnobArea(leftTransposeCenterX, topSideKnobTop);
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
    const float modelSwitchScale = 0.20f;
    const float modelSwitchWidth = switchOffBitmap.W() * modelSwitchScale;
    const float modelSwitchHeight = switchOffBitmap.H() * modelSwitchScale;
    const float modelSwitchCenterX = std::min(b.W() - 120.0f, masterKnobArea.MW() + 130.0f);
    const float modelSwitchCenterY = noiseGateArea.MH();
    const auto modelToggleArea = IRECT(modelSwitchCenterX - 0.5f * modelSwitchWidth,
                                       modelSwitchCenterY - 0.5f * modelSwitchHeight,
                                       modelSwitchCenterX + 0.5f * modelSwitchWidth,
                                       modelSwitchCenterY + 0.5f * modelSwitchHeight);

    // Stomp section coordinates come from the user's 3x design canvas.
    constexpr float kDesignW = 3117.0f; // 3 * 1039
    constexpr float kDesignH = 1998.0f; // 3 * 666
    // Global background-space offset (if the whole design reference needs nudging).
    constexpr float kBackgroundOffsetY = 0.0f;
    // Knob controls include label/value layout, so allow a separate anchor tweak.
    constexpr float kStompKnobAnchorOffsetY = -20.0f;
    // Buttons are pure bitmap controls and should stay center-mapped to coordinates.
    constexpr float kStompButtonAnchorOffsetY = 0.0f;
    const auto designToUIX = [&](const float x) { return b.L + (x / kDesignW) * b.W(); };
    const auto designToUIY = [&](const float y) { return b.T + kBackgroundOffsetY + (y / kDesignH) * b.H(); };
    const float stompGateThresholdX = designToUIX(1020.0f);
    const float stompGateReleaseX = designToUIX(1267.0f);
    const float stompBoostLevelX = designToUIX(1975.0f);
    const float stompKnobY = designToUIY(975.0f) + kStompKnobAnchorOffsetY;
    const float stompGateSwitchX = designToUIX(1140.0f);
    const float stompBoostSwitchX = designToUIX(1976.0f);
    const float stompSwitchY = designToUIY(1476.0f) + kStompButtonAnchorOffsetY;
    const auto stompGateThresholdArea = makePedalKnobArea(stompGateThresholdX, stompKnobY);
    const auto stompGateReleaseArea = makePedalKnobArea(stompGateReleaseX, stompKnobY);
    const auto stompBoostLevelArea = makePedalKnobArea(stompBoostLevelX, stompKnobY);
    const float stompButtonScale = 0.15f;
    const float stompButtonW =
      stompButtonUpBitmap.IsValid() ? static_cast<float>(stompButtonUpBitmap.W()) * stompButtonScale : 46.0f;
    const float stompButtonH =
      stompButtonUpBitmap.IsValid() ? static_cast<float>(stompButtonUpBitmap.H()) * stompButtonScale : 30.0f;
    const auto stompGateSwitchArea =
      IRECT(stompGateSwitchX - 0.5f * stompButtonW, stompSwitchY - 0.5f * stompButtonH, stompGateSwitchX + 0.5f * stompButtonW,
            stompSwitchY + 0.5f * stompButtonH);
    const auto stompBoostSwitchArea =
      IRECT(stompBoostSwitchX - 0.5f * stompButtonW, stompSwitchY - 0.5f * stompButtonH, stompBoostSwitchX + 0.5f * stompButtonW,
            stompSwitchY + 0.5f * stompButtonH);
    const auto stompGateOnLedArea = IRECT(stompGateSwitchArea.MW() - 7.0f, stompGateSwitchArea.B + 11.0f,
                                          stompGateSwitchArea.MW() + 7.0f, stompGateSwitchArea.B + 25.0f);
    const auto stompGateActiveLedArea = IRECT(stompGateSwitchArea.MW() - 7.0f, stompGateThresholdArea.B + 6.0f,
                                              stompGateSwitchArea.MW() + 7.0f, stompGateThresholdArea.B + 20.0f);
    const auto stompBoostOnLedArea = IRECT(stompBoostSwitchArea.MW() - 7.0f, stompBoostSwitchArea.B + 11.0f,
                                           stompBoostSwitchArea.MW() + 7.0f, stompBoostSwitchArea.B + 25.0f);
    // FX section coordinates come from the same 3x design canvas used by the stomp section.
    constexpr float kFXEqSliderW = 16.0f;
    constexpr float kFXEqSliderH = 97.0f;
    constexpr float kFXEqSliderGap = 46.5f;
    const float fxEqSliderTopY = designToUIY(715.0f);
    const float fxEqSliderStartX = designToUIX(640.0f);
    auto makeFXEqSliderArea = [&](const int index) {
      const float cx = fxEqSliderStartX + static_cast<float>(index) * kFXEqSliderGap;
      return IRECT(cx - 0.5f * kFXEqSliderW, fxEqSliderTopY, cx + 0.5f * kFXEqSliderW, fxEqSliderTopY + kFXEqSliderH);
    };
    const auto fxEqBand31Area = makeFXEqSliderArea(0);
    const auto fxEqBand62Area = makeFXEqSliderArea(1);
    const auto fxEqBand125Area = makeFXEqSliderArea(2);
    const auto fxEqBand250Area = makeFXEqSliderArea(3);
    const auto fxEqBand500Area = makeFXEqSliderArea(4);
    const auto fxEqBand1kArea = makeFXEqSliderArea(5);
    const auto fxEqBand2kArea = makeFXEqSliderArea(6);
    const auto fxEqBand4kArea = makeFXEqSliderArea(7);
    const auto fxEqBand8kArea = makeFXEqSliderArea(8);
    const auto fxEqBand16kArea = makeFXEqSliderArea(9);
    const auto fxEqOutputArea = makePedalKnobArea(designToUIX(2307.0f), designToUIY(840.0f));

    const float fxEqSwitchX = designToUIX(2660.0f);
    const float fxEqSwitchY = designToUIY(880.0f);
    const auto fxEqSwitchArea =
      IRECT(fxEqSwitchX - 0.5f * stompButtonW, fxEqSwitchY - 0.5f * stompButtonH, fxEqSwitchX + 0.5f * stompButtonW,
            fxEqSwitchY + 0.5f * stompButtonH);
    const auto fxEqOnLedArea =
      IRECT(fxEqSwitchArea.MW() + 41.0f, fxEqSwitchArea.B - 34.0f, fxEqSwitchArea.MW() + 55.0f, fxEqSwitchArea.B - 20.0f);

    const float fxReverbKnobY = designToUIY(1215.0f);
    const auto fxReverbMixArea = makePedalKnobArea(designToUIX(1840.0f), fxReverbKnobY);
    const auto fxReverbDecayArea = makePedalKnobArea(designToUIX(2064.0f), fxReverbKnobY);
    const auto fxReverbPreDelayArea = makePedalKnobArea(designToUIX(2259.0f), fxReverbKnobY);
    const auto fxReverbToneArea = makePedalKnobArea(designToUIX(2445.0f), fxReverbKnobY);
    const auto fxReverbLowCutArea = makePedalKnobArea(designToUIX(820.0f), fxReverbKnobY);
    const auto fxReverbHighCutArea = makePedalKnobArea(designToUIX(1210.0f), fxReverbKnobY);
    constexpr float kFXModeSwitchScale = 0.18f;
    const float fxModeSwitchW = switchOffBitmap.IsValid() ? static_cast<float>(switchOffBitmap.W()) * kFXModeSwitchScale : 88.0f;
    const float fxModeSwitchH = switchOffBitmap.IsValid() ? static_cast<float>(switchOffBitmap.H()) * kFXModeSwitchScale : 52.0f;
    const float fxReverbModeCenterX = designToUIX(1485.0f);
    const float fxReverbModeCenterY = designToUIY(1250.0f);
    const auto fxReverbModeArea =
      IRECT(fxReverbModeCenterX - 0.5f * fxModeSwitchW, fxReverbModeCenterY - 0.5f * fxModeSwitchH,
            fxReverbModeCenterX + 0.5f * fxModeSwitchW, fxReverbModeCenterY + 0.5f * fxModeSwitchH);
    const float fxReverbSwitchX = designToUIX(2660.0f);
    const float fxReverbSwitchY = designToUIY(1250.0f);
    const auto fxReverbSwitchArea =
      IRECT(fxReverbSwitchX - 0.5f * stompButtonW, fxReverbSwitchY - 0.5f * stompButtonH,
            fxReverbSwitchX + 0.5f * stompButtonW, fxReverbSwitchY + 0.5f * stompButtonH);
    const auto fxReverbOnLedArea = IRECT(fxReverbSwitchArea.MW() + 41.0f, fxReverbSwitchArea.B - 34.0f,
                                         fxReverbSwitchArea.MW() + 55.0f, fxReverbSwitchArea.B - 20.0f);

    const float fxDelayKnobY = designToUIY(1575.0f);
    const auto fxDelayMixArea = makePedalKnobArea(designToUIX(820.0f), fxDelayKnobY);
    const auto fxDelayTimeArea = makePedalKnobArea(designToUIX(1210.0f), fxDelayKnobY);
    const auto fxDelayFeedbackArea = makePedalKnobArea(designToUIX(1600.0f), fxDelayKnobY);
    const auto fxDelayLowCutArea = makePedalKnobArea(designToUIX(1990.0f), fxDelayKnobY);
    const auto fxDelayHighCutArea = makePedalKnobArea(designToUIX(2380.0f), fxDelayKnobY);
    const float fxDelaySwitchX = designToUIX(2660.0f);
    const float fxDelaySwitchY = designToUIY(1610.0f);
    const auto fxDelaySwitchArea =
      IRECT(fxDelaySwitchX - 0.5f * stompButtonW, fxDelaySwitchY - 0.5f * stompButtonH, fxDelaySwitchX + 0.5f * stompButtonW,
            fxDelaySwitchY + 0.5f * stompButtonH);
    const auto fxDelayOnLedArea =
      IRECT(fxDelaySwitchArea.MW() + 41.0f, fxDelaySwitchArea.B - 34.0f, fxDelaySwitchArea.MW() + 55.0f, fxDelaySwitchArea.B - 20.0f);


    // Gate/EQ toggle row (independent group)
    const float toggleTop = frontKnobTop + 86.0f;
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
    constexpr float kSettingsPad = 20.0f;
    constexpr float kModelPickerWidth = 320.0f;
    constexpr float kModelPickerHeight = 30.0f;
    const auto settingsInnerArea = b.GetPadded(-(kSettingsPad + 10.0f));
    const float settingsLoaderLeft = settingsInnerArea.L + 22.0f;
    const float settingsLoaderRight = settingsLoaderLeft + kModelPickerWidth;
    const float settingsLoaderTop = settingsInnerArea.T + 108.0f;
    const auto settingsAmpModelArea1 =
      IRECT(settingsLoaderLeft, settingsLoaderTop, settingsLoaderRight, settingsLoaderTop + kModelPickerHeight);
    const auto settingsAmpModelArea2 = settingsAmpModelArea1.GetTranslated(0.0f, 56.0f);
    const auto settingsAmpModelArea3 = settingsAmpModelArea2.GetTranslated(0.0f, 56.0f);
    const auto settingsStompModelArea = settingsAmpModelArea3.GetTranslated(0.0f, 56.0f);
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
    auto loadAmpModelForSlot = [this](const int slotIndex, const int ctrlTag, const WDL_String& fileName) {
      if (fileName.GetLength())
      {
        if (mAmpSelectorIndex == slotIndex)
        {
          const std::string msg = _StageModel(fileName, slotIndex, ctrlTag);
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
        }
        else
        {
          mAmpNAMPaths[slotIndex] = fileName;
          SendControlMsgFromDelegate(ctrlTag, kMsgTagLoadedModel, fileName.GetLength(), fileName.Get());
        }
      }
    };
    auto loadModelCompletionHandlerSlot1 = [loadAmpModelForSlot](const WDL_String& fileName, const WDL_String&) {
      loadAmpModelForSlot(0, kCtrlTagModelFileBrowser, fileName);
    };
    auto loadModelCompletionHandlerSlot2 = [loadAmpModelForSlot](const WDL_String& fileName, const WDL_String&) {
      loadAmpModelForSlot(1, kCtrlTagModelFileBrowser2, fileName);
    };
    auto loadModelCompletionHandlerSlot3 = [loadAmpModelForSlot](const WDL_String& fileName, const WDL_String&) {
      loadAmpModelForSlot(2, kCtrlTagModelFileBrowser3, fileName);
    };

    // IR loader button
    auto loadStompModelCompletionHandler = [&](const WDL_String& fileName, const WDL_String&) {
      if (fileName.GetLength())
      {
        const std::string msg = _StageStompModel(fileName);
        if (msg.size())
        {
          std::stringstream ss;
          ss << "Failed to load boost model. Message:\n\n" << msg;
          _ShowMessageBox(GetUI(), ss.str().c_str(), "Failed to load boost model!", kMB_OK);
          GetParam(kStompBoostActive)->Set(0.0);
        }
        else
        {
          GetParam(kStompBoostActive)->Set(1.0);
        }
        SendParameterValueFromDelegate(kStompBoostActive, GetParam(kStompBoostActive)->GetNormalized(), true);
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

    pGraphics->AttachControl(new NAMBackgroundBitmapControl(b, AMP2BACKGROUND_FN, amp2BackgroundBitmap), kCtrlTagMainBackground);
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

    // Getting started page listing additional resources
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
                                                       leftArrowSVG, rightArrowSVG, fileBackgroundBitmap),
                             kCtrlTagIRFileBrowserLeft);
    pGraphics->AttachControl(
      new NAMFileBrowserControl(irRightArea, kMsgTagClearIRRight, "Select cab IR R...", "wav",
                                loadIRRightCompletionHandler, utilityStyle, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
                                fileBackgroundBitmap),
      kCtrlTagIRFileBrowserRight);
    pGraphics->AttachControl(new NAMBlendSliderControl(cabBlendArea, kCabIRBlend, utilityStyle));
    pGraphics->AttachControl(new NAMTopIconControl(footerAmpSlot1Area, ampActiveSVG, ampActiveSVG, ampActiveSVG,
                                                   [this]() {
                                                     _SelectAmpSlot(0);
                                                   },
                                                   {}),
                             kCtrlTagAmpSlot1)
      ->SetTooltip("Amp Slot 1");
    pGraphics->AttachControl(new NAMTopIconControl(footerAmpSlot2Area, ampActiveSVG, ampActiveSVG, ampActiveSVG,
                                                   [this]() {
                                                     _SelectAmpSlot(1);
                                                   },
                                                   {}),
                             kCtrlTagAmpSlot2)
      ->SetTooltip("Amp Slot 2");
    pGraphics->AttachControl(new NAMTopIconControl(footerAmpSlot3Area, ampActiveSVG, ampActiveSVG, ampActiveSVG,
                                                   [this]() {
                                                     _SelectAmpSlot(2);
                                                   },
                                                   {}),
                             kCtrlTagAmpSlot3)
      ->SetTooltip("Amp Slot 3");
    pGraphics->AttachControl(
      new NAMMomentaryBitmapButtonControl(stompGateSwitchArea, kNoiseGateActive, stompButtonUpBitmap, stompButtonDownBitmap),
      -1,
      "STOMP_CONTROLS");
    pGraphics->AttachControl(new NAMBitmapLEDControl(stompGateOnLedArea, redLedOnBitmap, redLedOffBitmap),
                             kCtrlTagGateOnLED,
                             "STOMP_CONTROLS");
    pGraphics->AttachControl(new NAMBitmapLEDControl(stompGateActiveLedArea, greenLedOnBitmap, greenLedOffBitmap),
                             kCtrlTagNoiseGateLED,
                             "STOMP_CONTROLS");
    pGraphics->AttachControl(
      new NAMMomentaryBitmapButtonControl(stompBoostSwitchArea, kStompBoostActive, stompButtonUpBitmap, stompButtonDownBitmap),
      -1,
      "STOMP_CONTROLS");
    pGraphics->AttachControl(new NAMBitmapLEDControl(stompBoostOnLedArea, redLedOnBitmap, redLedOffBitmap),
                             kCtrlTagBoostOnLED,
                             "STOMP_CONTROLS");
    pGraphics->AttachControl(
      new NAMMomentaryBitmapButtonControl(fxEqSwitchArea, kFXEQActive, stompButtonUpBitmap, stompButtonDownBitmap),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(new NAMBitmapLEDControl(fxEqOnLedArea, redLedOnBitmap, redLedOffBitmap),
                             kCtrlTagFXEQOnLED,
                             "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMMomentaryBitmapButtonControl(fxDelaySwitchArea, kFXDelayActive, stompButtonUpBitmap, stompButtonDownBitmap),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(new NAMBitmapLEDControl(fxDelayOnLedArea, redLedOnBitmap, redLedOffBitmap),
                             kCtrlTagFXDelayOnLED,
                             "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMMomentaryBitmapButtonControl(fxReverbSwitchArea, kFXReverbActive, stompButtonUpBitmap, stompButtonDownBitmap),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(new NAMBitmapLEDControl(fxReverbOnLedArea, redLedOnBitmap, redLedOffBitmap),
                             kCtrlTagFXReverbOnLED,
                             "FX_CONTROLS");
    pGraphics->AttachControl(new NAMSwitchControl(eqToggleArea, kEQActive, "EQ", style, switchHandleBitmap))->Hide(true);

    // The knobs
    constexpr float kSideLabelYOffset = 18.0f;
    constexpr float kSideValueYOffset = -24.0f;
    pGraphics->AttachControl(new NAMKnobControl(
      inputKnobArea, kInputLevel, "INPUT", utilityStyle, outerKnobBackgroundSVG, true, false, topSideKnobScale, kSideLabelYOffset,
      kSideValueYOffset));
    pGraphics->AttachControl(new NAMKnobControl(
      transposeKnobArea, kTransposeSemitones, "TRANSPOSE", utilityStyle, outerKnobBackgroundSVG, true, false,
      topSideKnobScale, kSideLabelYOffset, kSideValueYOffset));
    pGraphics->AttachControl(
      new NAMPedalKnobControl(stompGateThresholdArea, kNoiseGateThreshold, "THRESH", utilityStyle, pedalKnobBitmap,
                              pedalKnobShadowBitmap, kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "STOMP_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(stompGateReleaseArea, kNoiseGateReleaseMs, "RELEASE", utilityStyle, pedalKnobBitmap,
                              pedalKnobShadowBitmap, kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "STOMP_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(stompBoostLevelArea, kStompBoostLevel, "LEVEL", utilityStyle, pedalKnobBitmap,
                              pedalKnobShadowBitmap, kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "STOMP_CONTROLS");
    pGraphics->AttachControl(new IVSliderControl(
      fxEqBand31Area, kFXEQBand31Hz, "31Hz", fxEqSliderStyle, false, EDirection::Vertical, DEFAULT_GEARING, 6.0f, 3.0f, true),
                             -1,
                             "FX_CONTROLS");
    pGraphics->AttachControl(new IVSliderControl(
      fxEqBand62Area, kFXEQBand62Hz, "62Hz", fxEqSliderStyle, false, EDirection::Vertical, DEFAULT_GEARING, 6.0f, 3.0f, true),
                             -1,
                             "FX_CONTROLS");
    pGraphics->AttachControl(new IVSliderControl(fxEqBand125Area, kFXEQBand125Hz, "125Hz", fxEqSliderStyle,
                                                 false, EDirection::Vertical, DEFAULT_GEARING, 6.0f, 3.0f, true),
                             -1,
                             "FX_CONTROLS");
    pGraphics->AttachControl(new IVSliderControl(fxEqBand250Area, kFXEQBand250Hz, "250Hz", fxEqSliderStyle,
                                                 false, EDirection::Vertical, DEFAULT_GEARING, 6.0f, 3.0f, true),
                             -1,
                             "FX_CONTROLS");
    pGraphics->AttachControl(new IVSliderControl(fxEqBand500Area, kFXEQBand500Hz, "500Hz", fxEqSliderStyle,
                                                 false, EDirection::Vertical, DEFAULT_GEARING, 6.0f, 3.0f, true),
                             -1,
                             "FX_CONTROLS");
    pGraphics->AttachControl(new IVSliderControl(
      fxEqBand1kArea, kFXEQBand1kHz, "1kHz", fxEqSliderStyle, false, EDirection::Vertical, DEFAULT_GEARING, 6.0f, 3.0f, true),
                             -1,
                             "FX_CONTROLS");
    pGraphics->AttachControl(new IVSliderControl(
      fxEqBand2kArea, kFXEQBand2kHz, "2kHz", fxEqSliderStyle, false, EDirection::Vertical, DEFAULT_GEARING, 6.0f, 3.0f, true),
                             -1,
                             "FX_CONTROLS");
    pGraphics->AttachControl(new IVSliderControl(
      fxEqBand4kArea, kFXEQBand4kHz, "4kHz", fxEqSliderStyle, false, EDirection::Vertical, DEFAULT_GEARING, 6.0f, 3.0f, true),
                             -1,
                             "FX_CONTROLS");
    pGraphics->AttachControl(new IVSliderControl(
      fxEqBand8kArea, kFXEQBand8kHz, "8kHz", fxEqSliderStyle, false, EDirection::Vertical, DEFAULT_GEARING, 6.0f, 3.0f, true),
                             -1,
                             "FX_CONTROLS");
    pGraphics->AttachControl(new IVSliderControl(
      fxEqBand16kArea, kFXEQBand16kHz, "16kHz", fxEqSliderStyle, false, EDirection::Vertical, DEFAULT_GEARING, 6.0f, 3.0f, true),
                             -1,
                             "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxEqOutputArea, kFXEQOutputGain, "OUT", utilityStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxReverbMixArea, kFXReverbMix, "DRY/WET", utilityStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxReverbDecayArea, kFXReverbDecay, "DECAY", utilityStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(new NAMPedalKnobControl(fxReverbPreDelayArea, kFXReverbPreDelayMs, "PRE-DLY", utilityStyle,
                                                     pedalKnobBitmap, pedalKnobShadowBitmap, kPedalKnobScale, 8.0f, -5.0f),
                             -1,
                             "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxReverbToneArea, kFXReverbTone, "TONE", utilityStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxReverbLowCutArea, kFXReverbLowCutHz, "LO CUT", utilityStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxReverbHighCutArea, kFXReverbHighCutHz, "HI CUT", utilityStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(new NAMBitmapToggleControl(fxReverbModeArea, kFXReverbMode, switchOffBitmap, switchOnBitmap),
                             -1,
                             "FX_CONTROLS")
      ->SetTooltip("Reverb mode: Room (OFF) / Hall (ON)");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxDelayMixArea, kFXDelayMix, "DRY/WET", utilityStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxDelayTimeArea, kFXDelayTimeMs, "TIME", utilityStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(new NAMPedalKnobControl(fxDelayFeedbackArea, kFXDelayFeedback, "FDBK", utilityStyle, pedalKnobBitmap,
                                                     pedalKnobShadowBitmap, kPedalKnobScale, 8.0f, -5.0f),
                             -1,
                             "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxDelayLowCutArea, kFXDelayLowCutHz, "LO CUT", utilityStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxDelayHighCutArea, kFXDelayHighCutHz, "HI CUT", utilityStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
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

    auto* pSettingsBox =
      new NAMSettingsPageControl(b, settingsBackgroundBitmap, inputLevelBackgroundBitmap, switchHandleBitmap, crossSVG, style,
                                 radioButtonStyle);
    pGraphics->AttachControl(pSettingsBox, kCtrlTagSettingsBox);
    pSettingsBox->AddChildControl(
      new NAMFileBrowserControl(settingsAmpModelArea1, kMsgTagClearModel, "Select amp 1 model...", "nam",
                                loadModelCompletionHandlerSlot1, utilityStyle, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
                                fileBackgroundBitmap),
      kCtrlTagModelFileBrowser);
    pSettingsBox->AddChildControl(
      new NAMFileBrowserControl(settingsAmpModelArea2, kMsgTagClearModel, "Select amp 2 model...", "nam",
                                loadModelCompletionHandlerSlot2, utilityStyle, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
                                fileBackgroundBitmap),
      kCtrlTagModelFileBrowser2);
    pSettingsBox->AddChildControl(
      new NAMFileBrowserControl(settingsAmpModelArea3, kMsgTagClearModel, "Select amp 3 model...", "nam",
                                loadModelCompletionHandlerSlot3, utilityStyle, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
                                fileBackgroundBitmap),
      kCtrlTagModelFileBrowser3);
    pSettingsBox->AddChildControl(
      new NAMFileBrowserControl(settingsStompModelArea, kMsgTagClearStompModel, "Select stomp NAM...", "nam",
                                loadStompModelCompletionHandler, utilityStyle, fileSVG, crossSVG, leftArrowSVG, rightArrowSVG,
                                fileBackgroundBitmap),
      kCtrlTagStompModelFileBrowser);
    pSettingsBox->HideAnimated(true);

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
  const bool stompBypassed = mTopNavBypassed[static_cast<size_t>(TopNavSection::Stomp)];
  const bool noiseGateActive = GetParam(kNoiseGateActive)->Value() && !stompBypassed;
  const bool boostEnabled = GetParam(kStompBoostActive)->Bool() && !stompBypassed && (mStompModel != nullptr);
  const bool toneStackActive = GetParam(kEQActive)->Value();
  const bool modelActive = GetParam(kModelToggle)->Bool();
  const bool tunerActive = GetParam(kTunerActive)->Bool();
  const int transposeSemitones = static_cast<int>(std::lround(GetParam(kTransposeSemitones)->Value()));
  const double gateReleaseValue = GetParam(kNoiseGateReleaseMs)->Value() * 0.2;
  const double boostLevelGain = DBToAmp(GetParam(kStompBoostLevel)->Value());
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

  // Lightweight semitone transposer with internal click-free crossfade on 0<->nonzero transitions.
  // We call this every block so fade-out to bypass can complete smoothly.
  mTransposeShifter.ProcessBlock(mInputPointers[0], numFrames, transposeSemitones);

  // Noise gate trigger
  sample** triggerOutput = mInputPointers;
  if (noiseGateActive)
  {
    const double time = 0.03;
    const double threshold = GetParam(kNoiseGateThreshold)->Value(); // GetParam...
    const double ratio = 1.5; // Quadratic...
    const double openTime = 0.03;
    const double holdTime = gateReleaseValue * 0.2;
    const double closeTime = gateReleaseValue;
    const dsp::noise_gate::TriggerParams triggerParams(time, threshold, ratio, openTime, holdTime, closeTime);
    mNoiseGateTrigger.SetParams(triggerParams);
    mNoiseGateTrigger.SetSampleRate(sampleRate);
    triggerOutput = mNoiseGateTrigger.Process(mInputPointers, numChannelsInternal, numFrames);
  }
  mNoiseGateIsAttenuating.store(noiseGateActive && mNoiseGateTrigger.IsAttenuating(10.0), std::memory_order_relaxed);

  sample** modelInputPointers =
    noiseGateActive ? mNoiseGateGain.Process(triggerOutput, numChannelsInternal, numFrames) : triggerOutput;

  if (boostEnabled)
  {
    sample** boostOutPointers = (modelInputPointers == mInputPointers) ? mOutputPointers : mInputPointers;
    mStompModel->process(modelInputPointers, boostOutPointers, nFrames);
    modelInputPointers = boostOutPointers;
  }

  if (boostEnabled && boostLevelGain != 1.0)
  {
    for (size_t c = 0; c < numChannelsInternal; ++c)
      for (size_t s = 0; s < numFrames; ++s)
        modelInputPointers[c][s] *= boostLevelGain;
  }

  if (modelActive && (mModel != nullptr))
  {
    if (preModelGain != 1.0)
    {
      for (size_t c = 0; c < numChannelsInternal; ++c)
        for (size_t s = 0; s < numFrames; ++s)
          modelInputPointers[c][s] *= preModelGain;
    }
    sample** modelOutPointers = (modelInputPointers == mInputPointers) ? mOutputPointers : mInputPointers;
    mModel->process(modelInputPointers, modelOutPointers, nFrames);
    modelInputPointers = modelOutPointers;
  }
  else
  {
    _FallbackDSP(modelInputPointers, mOutputPointers, numChannelsInternal, numFrames);
    modelInputPointers = mOutputPointers;
  }
  sample** toneStackOutPointers = (toneStackActive && mToneStack != nullptr)
                                    ? mToneStack->Process(modelInputPointers, numChannelsInternal, nFrames)
                                    : modelInputPointers;
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

  sample** fxEqPointers = userLowPassPointers2;
  const bool fxBypassed = mTopNavBypassed[static_cast<size_t>(TopNavSection::Fx)];
  const bool fxEQActive = GetParam(kFXEQActive)->Bool() && !fxBypassed;
  if (fxEQActive && sampleRate > 0.0)
  {
    constexpr std::array<int, 10> kFXEQParamIdx = {
      kFXEQBand31Hz, kFXEQBand62Hz, kFXEQBand125Hz, kFXEQBand250Hz, kFXEQBand500Hz,
      kFXEQBand1kHz, kFXEQBand2kHz, kFXEQBand4kHz, kFXEQBand8kHz, kFXEQBand16kHz
    };
    constexpr std::array<double, 10> kFXEQCenterHz = {31.0, 62.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0};
    constexpr double kFXEQQ = 1.41421356237;
    constexpr double kFXEQSmoothingMs = 30.0;
    const double smoothingAlpha = 1.0 - std::exp(-(static_cast<double>(nFrames) / (sampleRate * kFXEQSmoothingMs * 0.001)));
    const double nyquistGuardHz = 0.49 * sampleRate;
    const double targetEQOutputGain = DBToAmp(GetParam(kFXEQOutputGain)->Value());
    mFXEQSmoothedOutputGain += smoothingAlpha * (targetEQOutputGain - mFXEQSmoothedOutputGain);

    for (size_t band = 0; band < kFXEQCenterHz.size(); ++band)
    {
      const double targetGainDb = GetParam(kFXEQParamIdx[band])->Value();
      const double smoothedGainDb = mFXEQSmoothedGainDB[band] + smoothingAlpha * (targetGainDb - mFXEQSmoothedGainDB[band]);
      mFXEQSmoothedGainDB[band] = smoothedGainDb;

      const double gainA = std::pow(10.0, smoothedGainDb / 40.0);
      const double freqHz = std::min(kFXEQCenterHz[band], nyquistGuardHz);
      const double w0 = 2.0 * kPi * freqHz / sampleRate;
      const double cosW0 = std::cos(w0);
      const double sinW0 = std::sin(w0);
      const double alpha = sinW0 / (2.0 * kFXEQQ);

      const double b0 = 1.0 + alpha * gainA;
      const double b1 = -2.0 * cosW0;
      const double b2 = 1.0 - alpha * gainA;
      const double a0 = 1.0 + alpha / gainA;
      const double a1 = -2.0 * cosW0;
      const double a2 = 1.0 - alpha / gainA;
      const double invA0 = (a0 != 0.0) ? (1.0 / a0) : 1.0;

      mFXEQB0[band] = b0 * invA0;
      mFXEQB1[band] = b1 * invA0;
      mFXEQB2[band] = b2 * invA0;
      mFXEQA1[band] = a1 * invA0;
      mFXEQA2[band] = a2 * invA0;
    }

    for (size_t c = 0; c < numChannelsInternal; ++c)
    {
      auto& z1 = mFXEQZ1[c];
      auto& z2 = mFXEQZ2[c];
      for (size_t s = 0; s < numFrames; ++s)
      {
        double x = fxEqPointers[c][s];
        for (size_t band = 0; band < kFXEQCenterHz.size(); ++band)
        {
          const double y = mFXEQB0[band] * x + z1[band];
          z1[band] = mFXEQB1[band] * x - mFXEQA1[band] * y + z2[band];
          z2[band] = mFXEQB2[band] * x - mFXEQA2[band] * y;
          x = y;
        }
        fxEqPointers[c][s] = static_cast<sample>(x);
      }
    }

    if (std::abs(mFXEQSmoothedOutputGain - 1.0) > 1e-6)
    {
      for (size_t c = 0; c < numChannelsInternal; ++c)
        for (size_t s = 0; s < numFrames; ++s)
          fxEqPointers[c][s] = static_cast<sample>(fxEqPointers[c][s] * mFXEQSmoothedOutputGain);
    }
  }

  sample** fxDelayPointers = fxEqPointers;
  const bool fxDelayActive = GetParam(kFXDelayActive)->Bool() && !fxBypassed;
  if (mFXDelayBufferSamples > 2 && sampleRate > 0.0)
  {
    const double targetTimeSamples = std::clamp(
      GetParam(kFXDelayTimeMs)->Value() * 0.001 * sampleRate, 1.0, static_cast<double>(mFXDelayBufferSamples - 2));
    const double targetFeedback = std::clamp(GetParam(kFXDelayFeedback)->Value() * 0.01, 0.0, 0.80);
    const double targetMix = std::clamp(GetParam(kFXDelayMix)->Value() * 0.01, 0.0, 1.0);
    const double maxCutHz = std::max(40.0, 0.45 * sampleRate);
    const double targetLowCutHz = std::clamp(GetParam(kFXDelayLowCutHz)->Value(), 20.0, maxCutHz);
    const double targetHighCutHz =
      std::clamp(std::max(GetParam(kFXDelayHighCutHz)->Value(), targetLowCutHz + 20.0), 20.0, maxCutHz);
    constexpr double kFXDelayTimeSmoothingMs = 120.0;
    constexpr double kFXDelayControlSmoothingMs = 30.0;
    const double timeSmoothingAlpha = 1.0 - std::exp(-1.0 / (sampleRate * kFXDelayTimeSmoothingMs * 0.001));
    const double controlSmoothingAlpha = 1.0 - std::exp(-1.0 / (sampleRate * kFXDelayControlSmoothingMs * 0.001));
    double smoothedTimeSamples = mFXDelaySmoothedTimeSamples;
    double smoothedFeedback = mFXDelaySmoothedFeedback;
    double smoothedMix = mFXDelaySmoothedMix;
    double smoothedLowCutHz = mFXDelaySmoothedLowCutHz;
    double smoothedHighCutHz = mFXDelaySmoothedHighCutHz;
    size_t writeIndex = mFXDelayWriteIndex;

    for (size_t s = 0; s < numFrames; ++s)
    {
      smoothedTimeSamples += timeSmoothingAlpha * (targetTimeSamples - smoothedTimeSamples);
      smoothedFeedback += controlSmoothingAlpha * (targetFeedback - smoothedFeedback);
      smoothedMix += controlSmoothingAlpha * (targetMix - smoothedMix);
      smoothedLowCutHz += controlSmoothingAlpha * (targetLowCutHz - smoothedLowCutHz);
      smoothedHighCutHz += controlSmoothingAlpha * (targetHighCutHz - smoothedHighCutHz);
      if (smoothedHighCutHz < smoothedLowCutHz + 20.0)
        smoothedHighCutHz = std::min(maxCutHz, smoothedLowCutHz + 20.0);
      const double lowCutAlpha = 1.0 - std::exp(-2.0 * kPi * smoothedLowCutHz / sampleRate);
      const double highCutAlpha = 1.0 - std::exp(-2.0 * kPi * smoothedHighCutHz / sampleRate);

      for (size_t c = 0; c < numChannelsInternal; ++c)
      {
        auto& delayBuffer = mFXDelayBuffer[c];
        const double dry = fxDelayPointers[c][s];

        double readPos = static_cast<double>(writeIndex) - smoothedTimeSamples;
        if (readPos < 0.0)
          readPos += static_cast<double>(mFXDelayBufferSamples);
        const auto readIndex0 = static_cast<size_t>(readPos);
        const auto readIndex1 = (readIndex0 + 1 < mFXDelayBufferSamples) ? (readIndex0 + 1) : 0;
        const double frac = readPos - static_cast<double>(readIndex0);
        const double delayed =
          static_cast<double>(delayBuffer[readIndex0]) * (1.0 - frac) + static_cast<double>(delayBuffer[readIndex1]) * frac;

        auto& lowCutState = mFXDelayLowCutLPState[c];
        auto& highCutState = mFXDelayHighCutLPState[c];
        lowCutState += lowCutAlpha * (delayed - lowCutState);
        const double lowCutDelayed = delayed - lowCutState;
        highCutState += highCutAlpha * (lowCutDelayed - highCutState);
        const double filteredDelayed = highCutState;

        const double writeValue = dry + smoothedFeedback * filteredDelayed;
        delayBuffer[writeIndex] = static_cast<sample>(writeValue);

        if (fxDelayActive)
          // "Amount" behavior: keep dry at unity and add wet signal.
          fxDelayPointers[c][s] = static_cast<sample>(dry + filteredDelayed * smoothedMix);
      }

      ++writeIndex;
      if (writeIndex >= mFXDelayBufferSamples)
        writeIndex = 0;
    }
    mFXDelaySmoothedTimeSamples = smoothedTimeSamples;
    mFXDelaySmoothedFeedback = smoothedFeedback;
    mFXDelaySmoothedMix = smoothedMix;
    mFXDelaySmoothedLowCutHz = smoothedLowCutHz;
    mFXDelaySmoothedHighCutHz = smoothedHighCutHz;
    mFXDelayWriteIndex = writeIndex;
  }

  sample** fxReverbPointers = fxDelayPointers;
  const bool fxReverbActive = GetParam(kFXReverbActive)->Bool() && !fxBypassed;
  if (fxReverbActive && sampleRate > 0.0 && mFXReverbPreDelayBufferSamples > 2)
  {
    const double targetMix = std::clamp(GetParam(kFXReverbMix)->Value() * 0.01, 0.0, 1.0);
    const double targetDecaySeconds = std::clamp(GetParam(kFXReverbDecay)->Value(), 0.1, 10.0);
    const double targetPreDelaySamples = std::clamp(
      GetParam(kFXReverbPreDelayMs)->Value() * 0.001 * sampleRate, 0.0, static_cast<double>(mFXReverbPreDelayBufferSamples - 2));
    const double targetTone = std::clamp(GetParam(kFXReverbTone)->Value() * 0.01, 0.0, 1.0);
    const double targetMode = GetParam(kFXReverbMode)->Bool() ? 1.0 : 0.0;
    const double maxCutHz = std::max(40.0, 0.45 * sampleRate);
    const double targetLowCutHz = std::clamp(GetParam(kFXReverbLowCutHz)->Value(), 20.0, maxCutHz);
    const double targetHighCutHz =
      std::clamp(std::max(GetParam(kFXReverbHighCutHz)->Value(), targetLowCutHz + 20.0), 20.0, maxCutHz);

    constexpr double kReverbMixSmoothingMs = 40.0;
    constexpr double kReverbDecaySmoothingMs = 80.0;
    constexpr double kReverbPreDelaySmoothingMs = 120.0;
    constexpr double kReverbToneSmoothingMs = 60.0;
    constexpr double kReverbModeSmoothingMs = 120.0;
    constexpr double kReverbCutSmoothingMs = 60.0;
    constexpr double kReverbEarlyLevelSmoothingMs = 70.0;
    constexpr double kReverbEarlyToneSmoothingMs = 70.0;
    const double mixAlpha = 1.0 - std::exp(-1.0 / (sampleRate * kReverbMixSmoothingMs * 0.001));
    const double decayAlpha = 1.0 - std::exp(-1.0 / (sampleRate * kReverbDecaySmoothingMs * 0.001));
    const double preDelayAlpha = 1.0 - std::exp(-1.0 / (sampleRate * kReverbPreDelaySmoothingMs * 0.001));
    const double toneAlphaParam = 1.0 - std::exp(-1.0 / (sampleRate * kReverbToneSmoothingMs * 0.001));
    const double modeAlpha = 1.0 - std::exp(-1.0 / (sampleRate * kReverbModeSmoothingMs * 0.001));
    const double cutAlphaParam = 1.0 - std::exp(-1.0 / (sampleRate * kReverbCutSmoothingMs * 0.001));
    const double earlyLevelAlpha = 1.0 - std::exp(-1.0 / (sampleRate * kReverbEarlyLevelSmoothingMs * 0.001));
    const double earlyToneAlphaParam = 1.0 - std::exp(-1.0 / (sampleRate * kReverbEarlyToneSmoothingMs * 0.001));

    double smoothedMix = mFXReverbSmoothedMix;
    double smoothedDecaySeconds = mFXReverbSmoothedDecaySeconds;
    double smoothedPreDelaySamples = mFXReverbSmoothedPreDelaySamples;
    double smoothedTone = mFXReverbSmoothedTone;
    double smoothedMode = mFXReverbSmoothedMode;
    double smoothedEarlyLevel = mFXReverbSmoothedEarlyLevel;
    double smoothedEarlyToneHz = mFXReverbSmoothedEarlyToneHz;
    double smoothedLowCutHz = mFXReverbSmoothedLowCutHz;
    double smoothedHighCutHz = mFXReverbSmoothedHighCutHz;
    size_t preDelayWriteIndex = mFXReverbPreDelayWriteIndex;

    constexpr double kRoomWetGain = 0.44;
    constexpr double kHallWetGain = 0.52;
    constexpr double kRoomEarlyGain = 1.18;
    constexpr double kHallEarlyGain = 1.04;
    constexpr double kRoomEarlyDirect = 0.10;
    constexpr double kHallEarlyDirect = 0.14;
    constexpr double kRoomEarlyLevelBase = 1.08;
    constexpr double kHallEarlyLevelBase = 1.02;
    constexpr double kRoomPreDiffAllpassGain = 0.40;
    constexpr double kHallPreDiffAllpassGain = 0.56;
    constexpr double kRoomDecayScale = 0.62;
    constexpr double kHallDecayScale = 1.45;
    constexpr double kRoomCombFeedbackMax = 0.90;
    constexpr double kHallCombFeedbackMax = 0.988;
    constexpr double kRoomToneTilt = 1.00;
    constexpr double kHallToneTilt = 0.92;
    constexpr double kRoomCombDampTilt = 0.90;
    constexpr double kHallCombDampTilt = 0.80;
    constexpr double kRoomLateDiffusionGain = 0.16;
    constexpr double kHallLateDiffusionGain = 0.23;
    constexpr double kRoomLateInputGain = 0.32;
    constexpr double kHallLateInputGain = 0.46;
    constexpr double kHallExtraPreDelayMs = 6.0;
    constexpr double kRoomOutputTrim = 1.00;
    constexpr double kHallOutputTrim = 0.86;
    constexpr std::array<double, 8> kRoomEarlyTapGains = {1.10, 0.92, 0.80, 0.66, 0.50, 0.36, 0.25, 0.16};
    constexpr std::array<double, 8> kHallEarlyTapGains = {1.02, 0.86, 0.74, 0.60, 0.46, 0.34, 0.23, 0.15};
    constexpr std::array<double, 8> kRoomCombModRatesHz = {0.07, 0.09, 0.11, 0.13, 0.15, 0.18, 0.21, 0.24};
    constexpr std::array<double, 8> kHallCombModRatesHz = {0.025, 0.032, 0.039, 0.047, 0.056, 0.066, 0.078, 0.091};
    constexpr std::array<double, 8> kRoomCombModDepthSamples = {0.04, 0.06, 0.08, 0.10, 0.12, 0.15, 0.18, 0.22};
    constexpr std::array<double, 8> kHallCombModDepthSamples = {0.22, 0.30, 0.38, 0.48, 0.58, 0.70, 0.84, 1.00};
    std::array<double, 8> combModPhase = mFXReverbCombModPhase;

    for (size_t s = 0; s < numFrames; ++s)
    {
      smoothedMix += mixAlpha * (targetMix - smoothedMix);
      smoothedDecaySeconds += decayAlpha * (targetDecaySeconds - smoothedDecaySeconds);
      smoothedPreDelaySamples += preDelayAlpha * (targetPreDelaySamples - smoothedPreDelaySamples);
      smoothedTone += toneAlphaParam * (targetTone - smoothedTone);
      smoothedMode += modeAlpha * (targetMode - smoothedMode);
      smoothedLowCutHz += cutAlphaParam * (targetLowCutHz - smoothedLowCutHz);
      smoothedHighCutHz += cutAlphaParam * (targetHighCutHz - smoothedHighCutHz);
      if (smoothedHighCutHz < smoothedLowCutHz + 20.0)
        smoothedHighCutHz = std::min(maxCutHz, smoothedLowCutHz + 20.0);
      const double hallAmount = std::clamp(smoothedMode, 0.0, 1.0);
      const double roomAmount = 1.0 - hallAmount;
      const double targetEarlyLevel = roomAmount * kRoomEarlyLevelBase + hallAmount * kHallEarlyLevelBase;
      smoothedEarlyLevel += earlyLevelAlpha * (targetEarlyLevel - smoothedEarlyLevel);
      const double targetEarlyToneHz =
        roomAmount * (1700.0 + smoothedTone * 3600.0) + hallAmount * (1400.0 + smoothedTone * 3000.0);
      smoothedEarlyToneHz += earlyToneAlphaParam * (targetEarlyToneHz - smoothedEarlyToneHz);
      smoothedEarlyToneHz = std::clamp(smoothedEarlyToneHz, 900.0, 7000.0);
      const double wetCoreGain = roomAmount * kRoomWetGain + hallAmount * kHallWetGain;
      const double earlyGain = roomAmount * kRoomEarlyGain + hallAmount * kHallEarlyGain;
      const double preDiffAllpassGain = roomAmount * kRoomPreDiffAllpassGain + hallAmount * kHallPreDiffAllpassGain;
      const double effectiveDecaySeconds =
        std::max(0.1, smoothedDecaySeconds * (roomAmount * kRoomDecayScale + hallAmount * kHallDecayScale));
      const double combFeedbackMax = roomAmount * kRoomCombFeedbackMax + hallAmount * kHallCombFeedbackMax;
      const double toneTilt = roomAmount * kRoomToneTilt + hallAmount * kHallToneTilt;
      const double combDampTilt = roomAmount * kRoomCombDampTilt + hallAmount * kHallCombDampTilt;
      const double lateDiffusionGain = roomAmount * kRoomLateDiffusionGain + hallAmount * kHallLateDiffusionGain;
      const double lateInputGain = roomAmount * kRoomLateInputGain + hallAmount * kHallLateInputGain;
      const double earlyDirectMix = roomAmount * kRoomEarlyDirect + hallAmount * kHallEarlyDirect;
      const double modeOutputTrim = roomAmount * kRoomOutputTrim + hallAmount * kHallOutputTrim;
      const double effectivePreDelaySamples = std::clamp(
        smoothedPreDelaySamples + hallAmount * (kHallExtraPreDelayMs * 0.001 * sampleRate),
        0.0,
        static_cast<double>(mFXReverbPreDelayBufferSamples - 2));
      const double wetMix = std::pow(std::clamp(smoothedMix, 0.0, 1.0), 1.25);
      const double dryMix = std::sqrt(std::max(0.0, 1.0 - wetMix));
      const double wetGain = 1.5 * wetMix;
      const double mixNormalize = 1.0 / std::max(1.0, dryMix + wetGain);
      const double toneCutoffHz = std::clamp((2000.0 + smoothedTone * 12000.0) * toneTilt, 1000.0, 16000.0);
      const double toneAlpha = 1.0 - std::exp(-2.0 * kPi * toneCutoffHz / sampleRate);
      const double combDampCutoffHz = std::clamp((900.0 + smoothedTone * 2500.0) * combDampTilt, 500.0, 4500.0);
      const double combDampAlpha = 1.0 - std::exp(-2.0 * kPi * combDampCutoffHz / sampleRate);
      const double wetLowCutAlpha = 1.0 - std::exp(-2.0 * kPi * smoothedLowCutHz / sampleRate);
      const double wetHighCutAlpha = 1.0 - std::exp(-2.0 * kPi * smoothedHighCutHz / sampleRate);
      const double earlyToneAlpha = 1.0 - std::exp(-2.0 * kPi * smoothedEarlyToneHz / sampleRate);
      std::array<double, 8> combModOffset = {};
      for (size_t i = 0; i < combModOffset.size(); ++i)
      {
        const double combModDepth = roomAmount * kRoomCombModDepthSamples[i] + hallAmount * kHallCombModDepthSamples[i];
        const double combModRateHz = roomAmount * kRoomCombModRatesHz[i] + hallAmount * kHallCombModRatesHz[i];
        combModOffset[i] = combModDepth * std::sin(combModPhase[i]);
        combModPhase[i] += 2.0 * kPi * combModRateHz / sampleRate;
        if (combModPhase[i] >= 2.0 * kPi)
          combModPhase[i] -= 2.0 * kPi;
      }

      for (size_t c = 0; c < numChannelsInternal; ++c)
      {
        auto& preDelayBuffer = mFXReverbPreDelayBuffer[c];
        if (preDelayBuffer.empty())
          continue;

        const double dry = fxReverbPointers[c][s];
        preDelayBuffer[preDelayWriteIndex] = static_cast<sample>(dry);

        double early = 0.0;
        for (size_t i = 0; i < kHallEarlyTapGains.size(); ++i)
        {
          const size_t roomTapDelay = mFXReverbEarlyTapSamples[0][i] % preDelayBuffer.size();
          const size_t hallTapDelay = mFXReverbEarlyTapSamples[1][i] % preDelayBuffer.size();
          const size_t roomTapIndex = (preDelayWriteIndex + preDelayBuffer.size() - roomTapDelay) % preDelayBuffer.size();
          const size_t hallTapIndex = (preDelayWriteIndex + preDelayBuffer.size() - hallTapDelay) % preDelayBuffer.size();
          const double tapSample =
            roomAmount * static_cast<double>(preDelayBuffer[roomTapIndex]) + hallAmount * static_cast<double>(preDelayBuffer[hallTapIndex]);
          const double tapGain = roomAmount * kRoomEarlyTapGains[i] + hallAmount * kHallEarlyTapGains[i];
          early += tapGain * tapSample;
        }
        auto& earlyToneState = mFXReverbEarlyToneState[c];
        earlyToneState += earlyToneAlpha * (early - earlyToneState);
        const double earlyShaped = earlyToneState * smoothedEarlyLevel;

        double preReadPos = static_cast<double>(preDelayWriteIndex) - effectivePreDelaySamples;
        if (preReadPos < 0.0)
          preReadPos += static_cast<double>(mFXReverbPreDelayBufferSamples);
        const auto preReadIndex0 = static_cast<size_t>(preReadPos);
        const auto preReadIndex1 = (preReadIndex0 + 1 < mFXReverbPreDelayBufferSamples) ? (preReadIndex0 + 1) : 0;
        const double preFrac = preReadPos - static_cast<double>(preReadIndex0);
        const double preDelayed = static_cast<double>(preDelayBuffer[preReadIndex0]) * (1.0 - preFrac)
                                  + static_cast<double>(preDelayBuffer[preReadIndex1]) * preFrac;

        double lateInput = preDelayed;
        for (size_t i = 0; i < 2; ++i)
        {
          auto& preDiffBuffer = mFXReverbPreDiffAllpassBuffer[c][i];
          if (preDiffBuffer.empty())
            continue;
          auto& preDiffWriteIndex = mFXReverbPreDiffAllpassWriteIndex[c][i];
          const size_t preDiffDelaySamples = mFXReverbPreDiffAllpassDelaySamples[c][i];
          const size_t readIndex =
            (preDiffWriteIndex + preDiffBuffer.size() - (preDiffDelaySamples % preDiffBuffer.size())) % preDiffBuffer.size();
          const double delayed = static_cast<double>(preDiffBuffer[readIndex]);
          const double out = -preDiffAllpassGain * lateInput + delayed;
          preDiffBuffer[preDiffWriteIndex] = static_cast<sample>(lateInput + preDiffAllpassGain * out);
          lateInput = out;

          ++preDiffWriteIndex;
          if (preDiffWriteIndex >= preDiffBuffer.size())
            preDiffWriteIndex = 0;
        }

        std::array<double, 8> combOut = {};
        double combSum = 0.0;
        for (size_t i = 0; i < combOut.size(); ++i)
        {
          auto& combBuffer = mFXReverbCombBuffer[c][i];
          if (combBuffer.empty())
            continue;
          auto& combWriteIndex = mFXReverbCombWriteIndex[c][i];
          const size_t combDelaySamples = mFXReverbCombDelaySamples[c][i];
          const double modulatedDelay = std::clamp(static_cast<double>(combDelaySamples) + combModOffset[i], 1.0,
                                                   static_cast<double>(combBuffer.size() - 2));
          double readPos = static_cast<double>(combWriteIndex) - modulatedDelay;
          if (readPos < 0.0)
            readPos += static_cast<double>(combBuffer.size());
          const auto readIndex0 = static_cast<size_t>(readPos);
          const auto readIndex1 = (readIndex0 + 1 < combBuffer.size()) ? (readIndex0 + 1) : 0;
          const double frac = readPos - static_cast<double>(readIndex0);
          const double delayed = static_cast<double>(combBuffer[readIndex0]) * (1.0 - frac)
                                 + static_cast<double>(combBuffer[readIndex1]) * frac;
          auto& combDampState = mFXReverbCombDampState[c][i];
          combDampState += combDampAlpha * (delayed - combDampState);
          combOut[i] = combDampState;
          combSum += combDampState;
        }

        constexpr double kFDNHouseholderScale = 0.25; // 2 / 8
        const double fdnInput = lateInput * lateInputGain;
        for (size_t i = 0; i < combOut.size(); ++i)
        {
          auto& combBuffer = mFXReverbCombBuffer[c][i];
          if (combBuffer.empty())
            continue;
          auto& combWriteIndex = mFXReverbCombWriteIndex[c][i];
          const size_t combDelaySamples = mFXReverbCombDelaySamples[c][i];
          const double delaySeconds = static_cast<double>(combDelaySamples) / sampleRate;
          const double combFeedback = std::clamp(std::pow(10.0, (-3.0 * delaySeconds) / effectiveDecaySeconds), 0.0, combFeedbackMax);
          const double mixed = kFDNHouseholderScale * combSum - combOut[i];
          combBuffer[combWriteIndex] = static_cast<sample>(fdnInput + combFeedback * mixed);

          ++combWriteIndex;
          if (combWriteIndex >= combBuffer.size())
            combWriteIndex = 0;
        }

        const double wet = combSum * 0.125;
        double diffusedWet = wet;
        for (size_t i = 0; i < 2; ++i)
        {
          auto& allpassBuffer = mFXReverbAllpassBuffer[c][i];
          if (allpassBuffer.empty())
            continue;
          auto& allpassWriteIndex = mFXReverbAllpassWriteIndex[c][i];
          const size_t allpassDelaySamples = mFXReverbAllpassDelaySamples[c][i];
          const size_t readIndex =
            (allpassWriteIndex + allpassBuffer.size() - (allpassDelaySamples % allpassBuffer.size())) % allpassBuffer.size();
          const double delayed = static_cast<double>(allpassBuffer[readIndex]);
          const double out = -lateDiffusionGain * diffusedWet + delayed;
          allpassBuffer[allpassWriteIndex] = static_cast<sample>(diffusedWet + lateDiffusionGain * out);
          diffusedWet = out;

          ++allpassWriteIndex;
          if (allpassWriteIndex >= allpassBuffer.size())
            allpassWriteIndex = 0;
        }

        mFXReverbToneState[c] += toneAlpha * (diffusedWet - mFXReverbToneState[c]);
        const double tonedWet = mFXReverbToneState[c] * wetCoreGain;
        const double rawWet = tonedWet + earlyShaped * earlyGain;
        auto& lowCutState = mFXReverbLowCutLPState[c];
        auto& highCutState = mFXReverbHighCutLPState[c];
        lowCutState += wetLowCutAlpha * (rawWet - lowCutState);
        const double lowCutWet = rawWet - lowCutState;
        highCutState += wetHighCutAlpha * (lowCutWet - highCutState);
        const double earlyDirect = earlyShaped * earlyDirectMix;
        fxReverbPointers[c][s] =
          static_cast<sample>(mixNormalize * (dryMix * dry + wetGain * (modeOutputTrim * highCutState + earlyDirect)));
      }

      ++preDelayWriteIndex;
      if (preDelayWriteIndex >= mFXReverbPreDelayBufferSamples)
        preDelayWriteIndex = 0;
    }

    mFXReverbSmoothedMix = smoothedMix;
    mFXReverbSmoothedDecaySeconds = smoothedDecaySeconds;
    mFXReverbSmoothedPreDelaySamples = smoothedPreDelaySamples;
    mFXReverbSmoothedTone = smoothedTone;
    mFXReverbSmoothedMode = smoothedMode;
    mFXReverbSmoothedEarlyLevel = smoothedEarlyLevel;
    mFXReverbSmoothedEarlyToneHz = smoothedEarlyToneHz;
    mFXReverbSmoothedLowCutHz = smoothedLowCutHz;
    mFXReverbSmoothedHighCutHz = smoothedHighCutHz;
    mFXReverbPreDelayWriteIndex = preDelayWriteIndex;
    mFXReverbCombModPhase = combModPhase;
  }

  // And the HPF for DC offset (Issue 271)
  const double highPassCutoffFreq = kDCBlockerFrequency;
  // const double lowPassCutoffFreq = 20000.0;
  const recursive_linear_filter::HighPassParams highPassParams(sampleRate, highPassCutoffFreq);
  // const recursive_linear_filter::LowPassParams lowPassParams(sampleRate, lowPassCutoffFreq);
  mHighPass.SetParams(highPassParams);
  // mLowPass.SetParams(lowPassParams);
  sample** hpfPointers = mHighPass.Process(fxReverbPointers, numChannelsInternal, numFrames);
  // sample** lpfPointers = mLowPass.Process(hpfPointers, numChannelsInternal, numFrames);

  // Smooth the first samples after an amp slot switch to reduce hard-step clicks.
  if (numFrames > 0)
  {
    int declickRemaining = mAmpSwitchDeClickSamplesRemaining.load(std::memory_order_relaxed);
    if (declickRemaining > 0)
    {
      const int framesToSmooth = static_cast<int>(std::min(numFrames, static_cast<size_t>(declickRemaining)));
      for (size_t c = 0; c < numChannelsInternal; ++c)
      {
        double prev = mAmpSwitchDeClickPrevSample[c];
        int channelRemaining = declickRemaining;
        for (int s = 0; s < framesToSmooth; ++s)
        {
          const double t = 1.0 - static_cast<double>(channelRemaining - 1) / static_cast<double>(kAmpSlotSwitchDeClickSamples);
          const double blended = (1.0 - t) * prev + t * static_cast<double>(hpfPointers[c][s]);
          hpfPointers[c][s] = static_cast<sample>(blended);
          prev = blended;
          --channelRemaining;
        }
        if (numFrames > static_cast<size_t>(framesToSmooth))
          prev = static_cast<double>(hpfPointers[c][numFrames - 1]);
        mAmpSwitchDeClickPrevSample[c] = prev;
      }
      declickRemaining -= framesToSmooth;
      mAmpSwitchDeClickSamplesRemaining.store(std::max(0, declickRemaining), std::memory_order_relaxed);
    }
    else
    {
      for (size_t c = 0; c < numChannelsInternal; ++c)
        mAmpSwitchDeClickPrevSample[c] = static_cast<double>(hpfPointers[c][numFrames - 1]);
    }
  }

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
  constexpr double kFXDelayMaxSeconds = 2.0;
  constexpr double kFXReverbMaxPreDelaySeconds = 0.30;
  constexpr std::array<int, 8> kFXReverbCombBaseDelay = {1423, 1597, 1789, 1999, 2239, 2473, 2741, 3049};
  constexpr std::array<int, 2> kFXReverbPreDiffAllpassBaseDelay = {113, 337};
  constexpr std::array<int, 2> kFXReverbAllpassBaseDelay = {307, 503};
  constexpr std::array<double, 8> kFXReverbRoomEarlyTapMs = {0.0, 1.6, 3.0, 4.7, 6.8, 9.6, 12.9, 16.4};
  constexpr std::array<double, 8> kFXReverbHallEarlyTapMs = {0.0, 2.8, 5.2, 8.1, 11.6, 15.9, 21.0, 27.0};
  constexpr std::array<int, 10> kFXEQParamIdx = {
    kFXEQBand31Hz, kFXEQBand62Hz, kFXEQBand125Hz, kFXEQBand250Hz, kFXEQBand500Hz,
    kFXEQBand1kHz, kFXEQBand2kHz, kFXEQBand4kHz, kFXEQBand8kHz, kFXEQBand16kHz
  };

  // Tail is because the HPF DC blocker has a decay.
  // 10 cycles should be enough to pass the VST3 tests checking tail behavior.
  // I'm ignoring the model & IR, but it's not the end of the world.
  const int tailCycles = 10;
  SetTailSize(tailCycles * (int)(sampleRate / kDCBlockerFrequency));
  mInputSender.Reset(sampleRate);
  mOutputSender.Reset(sampleRate);
  // If there is a model or IR loaded, they need to be checked for resampling.
  _ResetModelAndIR(sampleRate, GetBlockSize());
  mTransposeShifter.Reset(sampleRate, maxBlockSize);
  mToneStack->Reset(sampleRate, maxBlockSize);
  // Pre-size internal mono buffers to the current host max block size.
  // ProcessBlock() should then only write/clear active frames.
  _PrepareBuffers(kNumChannelsInternal, (size_t)maxBlockSize);
  for (size_t band = 0; band < mFXEQSmoothedGainDB.size(); ++band)
    mFXEQSmoothedGainDB[band] = GetParam(kFXEQParamIdx[band])->Value();
  mFXEQSmoothedOutputGain = DBToAmp(GetParam(kFXEQOutputGain)->Value());
  for (auto& channelState : mFXEQZ1)
    channelState.fill(0.0);
  for (auto& channelState : mFXEQZ2)
    channelState.fill(0.0);
  mFXDelayBufferSamples =
    std::max<size_t>(2, static_cast<size_t>(std::ceil(kFXDelayMaxSeconds * sampleRate)) + static_cast<size_t>(maxBlockSize) + 2);
  for (auto& channelBuffer : mFXDelayBuffer)
    channelBuffer.assign(mFXDelayBufferSamples, 0.0f);
  mFXDelayWriteIndex = 0;
  mFXDelaySmoothedTimeSamples =
    std::clamp(GetParam(kFXDelayTimeMs)->Value() * 0.001 * sampleRate, 1.0, static_cast<double>(mFXDelayBufferSamples - 2));
  mFXDelaySmoothedFeedback = std::clamp(GetParam(kFXDelayFeedback)->Value() * 0.01, 0.0, 0.80);
  mFXDelaySmoothedMix = std::clamp(GetParam(kFXDelayMix)->Value() * 0.01, 0.0, 1.0);
  const double maxDelayCutHz = std::max(40.0, 0.45 * sampleRate);
  mFXDelaySmoothedLowCutHz = std::clamp(GetParam(kFXDelayLowCutHz)->Value(), 20.0, maxDelayCutHz);
  mFXDelaySmoothedHighCutHz =
    std::clamp(std::max(GetParam(kFXDelayHighCutHz)->Value(), mFXDelaySmoothedLowCutHz + 20.0), 20.0, maxDelayCutHz);
  mFXDelayLowCutLPState.fill(0.0);
  mFXDelayHighCutLPState.fill(0.0);

  mFXReverbPreDelayBufferSamples = std::max<size_t>(
    2, static_cast<size_t>(std::ceil(kFXReverbMaxPreDelaySeconds * sampleRate)) + static_cast<size_t>(maxBlockSize) + 2);
  for (auto& channelBuffer : mFXReverbPreDelayBuffer)
    channelBuffer.assign(mFXReverbPreDelayBufferSamples, 0.0f);
  mFXReverbPreDelayWriteIndex = 0;

  const double reverbDelayScale = sampleRate / 44100.0;
  for (size_t i = 0; i < kFXReverbRoomEarlyTapMs.size(); ++i)
  {
    mFXReverbEarlyTapSamples[0][i] = static_cast<size_t>(std::llround(kFXReverbRoomEarlyTapMs[i] * 0.001 * sampleRate));
    mFXReverbEarlyTapSamples[1][i] = static_cast<size_t>(std::llround(kFXReverbHallEarlyTapMs[i] * 0.001 * sampleRate));
  }
  for (size_t c = 0; c < kNumChannelsInternal; ++c)
  {
    for (size_t i = 0; i < kFXReverbPreDiffAllpassBaseDelay.size(); ++i)
    {
      const size_t delaySamples =
        std::max<size_t>(1, static_cast<size_t>(std::llround(kFXReverbPreDiffAllpassBaseDelay[i] * reverbDelayScale)));
      mFXReverbPreDiffAllpassDelaySamples[c][i] = delaySamples;
      mFXReverbPreDiffAllpassBuffer[c][i].assign(delaySamples + 1, 0.0f);
      mFXReverbPreDiffAllpassWriteIndex[c][i] = 0;
    }

    for (size_t i = 0; i < kFXReverbCombBaseDelay.size(); ++i)
    {
      const size_t delaySamples = std::max<size_t>(1, static_cast<size_t>(std::llround(kFXReverbCombBaseDelay[i] * reverbDelayScale)));
      mFXReverbCombDelaySamples[c][i] = delaySamples;
      mFXReverbCombBuffer[c][i].assign(delaySamples + 8, 0.0f);
      mFXReverbCombWriteIndex[c][i] = 0;
    }

    for (size_t i = 0; i < kFXReverbAllpassBaseDelay.size(); ++i)
    {
      const size_t delaySamples =
        std::max<size_t>(1, static_cast<size_t>(std::llround(kFXReverbAllpassBaseDelay[i] * reverbDelayScale)));
      mFXReverbAllpassDelaySamples[c][i] = delaySamples;
      mFXReverbAllpassBuffer[c][i].assign(delaySamples + 1, 0.0f);
      mFXReverbAllpassWriteIndex[c][i] = 0;
    }

    mFXReverbToneState[c] = 0.0;
    mFXReverbEarlyToneState[c] = 0.0;
    mFXReverbCombDampState[c].fill(0.0);
  }
  mFXReverbCombModPhase = {0.0, 0.78, 1.57, 2.35, 3.14, 3.93, 4.71, 5.50};

  mFXReverbSmoothedMix = std::clamp(GetParam(kFXReverbMix)->Value() * 0.01, 0.0, 1.0);
  mFXReverbSmoothedDecaySeconds = std::clamp(GetParam(kFXReverbDecay)->Value(), 0.1, 10.0);
  mFXReverbSmoothedPreDelaySamples = std::clamp(
    GetParam(kFXReverbPreDelayMs)->Value() * 0.001 * sampleRate, 0.0, static_cast<double>(mFXReverbPreDelayBufferSamples - 2));
  mFXReverbSmoothedTone = std::clamp(GetParam(kFXReverbTone)->Value() * 0.01, 0.0, 1.0);
  mFXReverbSmoothedMode = GetParam(kFXReverbMode)->Bool() ? 1.0 : 0.0;
  const double roomAmount = 1.0 - std::clamp(mFXReverbSmoothedMode, 0.0, 1.0);
  const double hallAmount = 1.0 - roomAmount;
  constexpr double kInitRoomEarlyLevel = 1.10;
  constexpr double kInitHallEarlyLevel = 0.96;
  mFXReverbSmoothedEarlyLevel = roomAmount * kInitRoomEarlyLevel + hallAmount * kInitHallEarlyLevel;
  const double roomEarlyToneHz = 1700.0 + mFXReverbSmoothedTone * 3600.0;
  const double hallEarlyToneHz = 1400.0 + mFXReverbSmoothedTone * 3000.0;
  mFXReverbSmoothedEarlyToneHz = roomAmount * roomEarlyToneHz + hallAmount * hallEarlyToneHz;
  const double maxReverbCutHz = std::max(40.0, 0.45 * sampleRate);
  mFXReverbSmoothedLowCutHz = std::clamp(GetParam(kFXReverbLowCutHz)->Value(), 20.0, maxReverbCutHz);
  mFXReverbSmoothedHighCutHz =
    std::clamp(std::max(GetParam(kFXReverbHighCutHz)->Value(), mFXReverbSmoothedLowCutHz + 20.0), 20.0, maxReverbCutHz);
  mFXReverbLowCutLPState.fill(0.0);
  mFXReverbHighCutLPState.fill(0.0);
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

  if (auto* pGraphics = GetUI())
  {
    if (auto* pGateOnLED = pGraphics->GetControlWithTag(kCtrlTagGateOnLED))
      pGateOnLED->SetValueFromDelegate(GetParam(kNoiseGateActive)->Bool() ? 1.0 : 0.0, 0);
    if (auto* pBoostOnLED = pGraphics->GetControlWithTag(kCtrlTagBoostOnLED))
      pBoostOnLED->SetValueFromDelegate(GetParam(kStompBoostActive)->Bool() ? 1.0 : 0.0, 0);
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

  for (int slotIndex = 0; slotIndex < static_cast<int>(mAmpNAMPaths.size()); ++slotIndex)
  {
    const int ctrlTag = _GetAmpModelCtrlTagForSlot(slotIndex);
    const WDL_String& slotPath = mAmpNAMPaths[slotIndex];
    if (slotPath.GetLength())
      SendControlMsgFromDelegate(ctrlTag, kMsgTagLoadedModel, slotPath.GetLength(), slotPath.Get());
  }
  // If it's not loaded yet, then mark active slot as failed.
  // If it's yet to be loaded, then the completion handler will set us straight once it runs.
  if (mAmpNAMPaths[mAmpSelectorIndex].GetLength() && mModel == nullptr && mStagedModel == nullptr)
    SendControlMsgFromDelegate(_GetAmpModelCtrlTagForSlot(mAmpSelectorIndex), kMsgTagLoadFailed);

  if (mStompNAMPath.GetLength())
  {
    SendControlMsgFromDelegate(
      kCtrlTagStompModelFileBrowser, kMsgTagLoadedStompModel, mStompNAMPath.GetLength(), mStompNAMPath.Get());
    if (mStompModel == nullptr && mStagedStompModel == nullptr)
      SendControlMsgFromDelegate(kCtrlTagStompModelFileBrowser, kMsgTagLoadFailed);
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
      case kNoiseGateActive:
        if (auto* pGateOnLED = pGraphics->GetControlWithTag(kCtrlTagGateOnLED))
          pGateOnLED->SetValueFromDelegate(active ? 1.0 : 0.0, 0);
        break;
      case kStompBoostActive:
        if (auto* pBoostOnLED = pGraphics->GetControlWithTag(kCtrlTagBoostOnLED))
          pBoostOnLED->SetValueFromDelegate(active ? 1.0 : 0.0, 0);
        if (active && (mStompModel == nullptr) && (mStagedStompModel == nullptr))
        {
          WDL_String fileName;
          WDL_String path;
          if (mStompNAMPath.GetLength())
          {
            path.Set(mStompNAMPath.Get());
            path.remove_filepart();
          }
          pGraphics->PromptForFile(
            fileName, path, EFileAction::Open, "nam", [this](const WDL_String& chosenFileName, const WDL_String&) {
              if (chosenFileName.GetLength())
              {
                const std::string msg = _StageStompModel(chosenFileName);
                if (msg.size())
                {
                  std::stringstream ss;
                  ss << "Failed to load boost model. Message:\n\n" << msg;
                  _ShowMessageBox(GetUI(), ss.str().c_str(), "Failed to load boost model!", kMB_OK);
                  GetParam(kStompBoostActive)->Set(0.0);
                }
                else
                {
                  GetParam(kStompBoostActive)->Set(1.0);
                }
              }
              else
              {
                GetParam(kStompBoostActive)->Set(0.0);
              }
              SendParameterValueFromDelegate(kStompBoostActive, GetParam(kStompBoostActive)->GetNormalized(), true);
            });
        }
        break;
      case kFXEQActive:
      {
        if (auto* pFXEQOnLED = pGraphics->GetControlWithTag(kCtrlTagFXEQOnLED))
          pFXEQOnLED->SetValueFromDelegate(active ? 1.0 : 0.0, 0);
        const int eqBandParams[] = {
          kFXEQBand31Hz, kFXEQBand62Hz, kFXEQBand125Hz, kFXEQBand250Hz, kFXEQBand500Hz,
          kFXEQBand1kHz, kFXEQBand2kHz, kFXEQBand4kHz, kFXEQBand8kHz, kFXEQBand16kHz, kFXEQOutputGain
        };
        for (const int eqBandParam : eqBandParams)
          if (auto* pControl = pGraphics->GetControlWithParamIdx(eqBandParam))
            pControl->SetDisabled(!active);
        break;
      }
      case kFXDelayActive:
      {
        if (auto* pFXDelayOnLED = pGraphics->GetControlWithTag(kCtrlTagFXDelayOnLED))
          pFXDelayOnLED->SetValueFromDelegate(active ? 1.0 : 0.0, 0);
        const int delayParams[] = {kFXDelayMix, kFXDelayTimeMs, kFXDelayFeedback, kFXDelayLowCutHz, kFXDelayHighCutHz};
        for (const int delayParam : delayParams)
          if (auto* pControl = pGraphics->GetControlWithParamIdx(delayParam))
            pControl->SetDisabled(!active);
        break;
      }
      case kFXReverbActive:
      {
        if (auto* pFXReverbOnLED = pGraphics->GetControlWithTag(kCtrlTagFXReverbOnLED))
          pFXReverbOnLED->SetValueFromDelegate(active ? 1.0 : 0.0, 0);
        const int reverbParams[] = {kFXReverbMix, kFXReverbDecay, kFXReverbPreDelayMs, kFXReverbTone,
                                    kFXReverbLowCutHz, kFXReverbHighCutHz};
        for (const int reverbParam : reverbParams)
          if (auto* pControl = pGraphics->GetControlWithParamIdx(reverbParam))
            pControl->SetDisabled(!active);
        break;
      }
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
          if (mAmpNAMPaths[mAmpSelectorIndex].GetLength())
          {
            path.Set(mAmpNAMPaths[mAmpSelectorIndex].Get());
            path.remove_filepart();
          }
          pGraphics->PromptForFile(
            fileName, path, EFileAction::Open, "nam", [this](const WDL_String& chosenFileName, const WDL_String&) {
              if (chosenFileName.GetLength())
              {
                const int slotCtrlTag = _GetAmpModelCtrlTagForSlot(mAmpSelectorIndex);
                const std::string msg = _StageModel(chosenFileName, mAmpSelectorIndex, slotCtrlTag);
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
    case kMsgTagClearModel:
    {
      const int slotIndex = _GetAmpSlotForModelCtrlTag(ctrlTag);
      if (slotIndex < 0 || slotIndex >= static_cast<int>(mAmpNAMPaths.size()))
      {
        mNAMPath.Set("");
        mShouldRemoveModel = true;
        return true;
      }

      mAmpNAMPaths[slotIndex].Set("");
      if (slotIndex == mAmpSelectorIndex)
      {
        mNAMPath.Set("");
        mShouldRemoveModel = true;
      }
      return true;
    }
    case kMsgTagClearStompModel: mShouldRemoveStompModel = true; return true;
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

int NeuralAmpModeler::_GetAmpModelCtrlTagForSlot(const int slotIndex) const
{
  switch (slotIndex)
  {
    case 0: return kCtrlTagModelFileBrowser;
    case 1: return kCtrlTagModelFileBrowser2;
    case 2: return kCtrlTagModelFileBrowser3;
    default: return kCtrlTagModelFileBrowser;
  }
}

int NeuralAmpModeler::_GetAmpSlotForModelCtrlTag(const int ctrlTag) const
{
  switch (ctrlTag)
  {
    case kCtrlTagModelFileBrowser: return 0;
    case kCtrlTagModelFileBrowser2: return 1;
    case kCtrlTagModelFileBrowser3: return 2;
    default: return -1;
  }
}

void NeuralAmpModeler::_SelectAmpSlot(int slotIndex)
{
  slotIndex = std::clamp(slotIndex, 0, static_cast<int>(mAmpNAMPaths.size()) - 1);
  if (mAmpSelectorIndex == slotIndex)
  {
    _RefreshTopNavControls();
    return;
  }

  mAmpSelectorIndex = slotIndex;
  mAmpSwitchDeClickSamplesRemaining.store(kAmpSlotSwitchDeClickSamples, std::memory_order_relaxed);
  const int slotCtrlTag = _GetAmpModelCtrlTagForSlot(slotIndex);
  const WDL_String& slotPath = mAmpNAMPaths[slotIndex];
  if (slotPath.GetLength())
  {
    const std::string msg = _StageModel(slotPath, slotIndex, slotCtrlTag);
    if (msg.size() && GetParam(kModelToggle)->Bool())
    {
      GetParam(kModelToggle)->Set(0.0);
      SendParameterValueFromDelegate(kModelToggle, GetParam(kModelToggle)->GetNormalized(), true);
    }
  }
  else
  {
    mNAMPath.Set("");
    mShouldRemoveModel = true;
    if (GetParam(kModelToggle)->Bool())
    {
      GetParam(kModelToggle)->Set(0.0);
      SendParameterValueFromDelegate(kModelToggle, GetParam(kModelToggle)->GetNormalized(), true);
    }
  }

  _RefreshTopNavControls();
}

void NeuralAmpModeler::_RefreshTopNavControls()
{
  if (auto* pGraphics = GetUI())
  {
    const auto tunerIdx = static_cast<size_t>(TopNavSection::Tuner);
    const bool tunerActive = !mTopNavBypassed[tunerIdx];
    const bool showAmpSection = (mTopNavActiveSection == TopNavSection::Amp);
    const bool showStompSection = (mTopNavActiveSection == TopNavSection::Stomp);
    const bool showCabSection = (mTopNavActiveSection == TopNavSection::Cab);
    const bool showFxSection = (mTopNavActiveSection == TopNavSection::Fx);
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

    const char* backgroundResource = AMP2BACKGROUND_FN;
    if (mTopNavActiveSection == TopNavSection::Amp)
    {
      if (mAmpSelectorIndex == 0)
        backgroundResource = AMP1BACKGROUND_FN;
      else if (mAmpSelectorIndex == 2)
        backgroundResource = AMP3BACKGROUND_FN;
      else
        backgroundResource = AMP2BACKGROUND_FN;
    }
    else if (mTopNavActiveSection == TopNavSection::Stomp)
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

    if (auto* pModelToggle = pGraphics->GetControlWithParamIdx(kModelToggle))
      pModelToggle->Hide(!showAmpSection);
    if (auto* pNoiseGateLED = pGraphics->GetControlWithTag(kCtrlTagNoiseGateLED))
      pNoiseGateLED->Hide(!showStompSection);
    if (auto* pGateOnLED = pGraphics->GetControlWithTag(kCtrlTagGateOnLED))
      pGateOnLED->Hide(!showStompSection);
    if (auto* pBoostOnLED = pGraphics->GetControlWithTag(kCtrlTagBoostOnLED))
      pBoostOnLED->Hide(!showStompSection);
    pGraphics->ForControlInGroup("STOMP_CONTROLS", [showStompSection](IControl* pControl) {
      pControl->Hide(!showStompSection);
    });
    if (auto* pFXEQOnLED = pGraphics->GetControlWithTag(kCtrlTagFXEQOnLED))
      pFXEQOnLED->Hide(!showFxSection);
    if (auto* pFXDelayOnLED = pGraphics->GetControlWithTag(kCtrlTagFXDelayOnLED))
      pFXDelayOnLED->Hide(!showFxSection);
    if (auto* pFXReverbOnLED = pGraphics->GetControlWithTag(kCtrlTagFXReverbOnLED))
      pFXReverbOnLED->Hide(!showFxSection);
    pGraphics->ForControlInGroup("FX_CONTROLS", [showFxSection](IControl* pControl) {
      pControl->Hide(!showFxSection);
    });

    const auto hideAmpParamControl = [&](const int paramIdx) {
      if (auto* pControl = pGraphics->GetControlWithParamIdx(paramIdx))
        pControl->Hide(!showAmpSection);
    };
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
  if (mShouldRemoveStompModel)
  {
    mStompModel = nullptr;
    mStompNAMPath.Set("");
    mShouldRemoveStompModel = false;
    _UpdateLatency();
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
  if (mStagedStompModel != nullptr)
  {
    mStompModel = std::move(mStagedStompModel);
    mStagedStompModel = nullptr;
    _UpdateLatency();
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
      outputs[c][s] = inputs[c][s];
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
  if (mStagedStompModel != nullptr)
  {
    mStagedStompModel->Reset(sampleRate, maxBlockSize);
  }
  else if (mStompModel != nullptr)
  {
    mStompModel->Reset(sampleRate, maxBlockSize);
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

std::string NeuralAmpModeler::_StageModel(const WDL_String& modelPath, int slotIndex, const int slotCtrlTag)
{
  slotIndex = std::clamp(slotIndex, 0, static_cast<int>(mAmpNAMPaths.size()) - 1);
  WDL_String previousNAMPath = mNAMPath;
  WDL_String previousSlotPath = mAmpNAMPaths[slotIndex];
  try
  {
    auto dspPath = std::filesystem::u8path(modelPath.Get());
    std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);
    std::unique_ptr<ResamplingNAM> temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), GetBlockSize());
    mStagedModel = std::move(temp);
    mAmpNAMPaths[slotIndex] = modelPath;
    if (mAmpSelectorIndex == slotIndex)
      mNAMPath = modelPath;
    SendControlMsgFromDelegate(slotCtrlTag, kMsgTagLoadedModel, modelPath.GetLength(), modelPath.Get());
  }
  catch (std::runtime_error& e)
  {
    SendControlMsgFromDelegate(slotCtrlTag, kMsgTagLoadFailed);

    if (mStagedModel != nullptr)
    {
      mStagedModel = nullptr;
    }
    mAmpNAMPaths[slotIndex] = previousSlotPath;
    mNAMPath = previousNAMPath;
    std::cerr << "Failed to read DSP module" << std::endl;
    std::cerr << e.what() << std::endl;
    return e.what();
  }
  return "";
}

std::string NeuralAmpModeler::_StageStompModel(const WDL_String& modelPath)
{
  WDL_String previousNAMPath = mStompNAMPath;
  try
  {
    auto dspPath = std::filesystem::u8path(modelPath.Get());
    std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);
    std::unique_ptr<ResamplingNAM> temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
    temp->Reset(GetSampleRate(), GetBlockSize());
    mStagedStompModel = std::move(temp);
    mStompNAMPath = modelPath;
    SendControlMsgFromDelegate(
      kCtrlTagStompModelFileBrowser, kMsgTagLoadedStompModel, mStompNAMPath.GetLength(), mStompNAMPath.Get());
  }
  catch (std::runtime_error& e)
  {
    SendControlMsgFromDelegate(kCtrlTagStompModelFileBrowser, kMsgTagLoadFailed);

    if (mStagedStompModel != nullptr)
      mStagedStompModel = nullptr;

    mStompNAMPath = previousNAMPath;
    std::cerr << "Failed to read stomp DSP module" << std::endl;
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
  if (mStompModel)
  {
    latency += mStompModel->GetLatency();
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
