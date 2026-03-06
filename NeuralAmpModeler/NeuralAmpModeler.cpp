#include <algorithm> // std::clamp, std::min
#include <cctype>
#include <cmath> // pow
#include <cstdint>
#include <cstdlib>
#include <cstring> // strcmp
#include <filesystem>
#include <functional>
#include <fstream>
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
#include "IPlugPaths.h"
// clang-format on
#include "architecture.hpp"

#include "NeuralAmpModelerControls.h"
#include "IPopupMenuControl.h"

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
constexpr int kAmpSlotSwitchDeClickSamples = 192;
constexpr int kAmpSlotModelStateEmpty = 0;
constexpr int kAmpSlotModelStateLoading = 1;
constexpr int kAmpSlotModelStateReady = 2;
constexpr int kAmpSlotModelStateFailed = 3;
constexpr int kSlotLoadUIEventNone = 0;
constexpr int kSlotLoadUIEventLoaded = 1;
constexpr int kSlotLoadUIEventFailed = 2;
constexpr const char* kStandaloneStateFileName = "plugin-state.bin";
constexpr const char* kStandalonePresetDirName = "Presets";
constexpr const char* kStandaloneUserPresetSubdirName = "User";
constexpr const char* kStandaloneFactoryPresetSubdirName = "Factory";
constexpr const char* kStandalonePresetExtension = "nampreset";
constexpr const char* kStandalonePresetDefaultFileName = "Preset.nampreset";
constexpr int kPresetMenuTagDefault = 0;
constexpr int kPresetMenuTagSave = 1;
constexpr int kPresetMenuTagSaveAs = 2;
constexpr int kPresetMenuTagDelete = 3;
constexpr int kPresetMenuTagRename = 4;
constexpr int kPresetMenuTagUserBase = 1000;
constexpr int kPresetMenuTagFactoryBase = 2000;
constexpr int kCtrlTagStandalonePresetNameEntryProxy = 91000;
constexpr std::streamoff kMaxStandaloneStateBytes = 16 * 1024 * 1024;
constexpr double kEffectiveMonoMaxPeakDiff = 3.0e-5;
constexpr double kEffectiveMonoMaxRelativeDiff = 1.0e-7;
constexpr double kEffectiveMonoSilenceMidEnergy = 1.0e-14;
constexpr double kEffectiveMonoEngageSeconds = 0.25;
constexpr double kEffectiveMonoReleaseSeconds = 0.03;
constexpr double kStereoSideBypassOneSidedEnergyRatio = 1.0e-6;
constexpr double kStereoSideBypassSilenceEnergy = 1.0e-14;
constexpr double kStereoSideBypassEngageSeconds = 0.08;
constexpr double kStereoSideBypassReleaseSeconds = 0.03;
constexpr int kStereoSideBypassResumeDeClickSamples = 64;
constexpr size_t kMinInternalPreparedFrames = 16384;
constexpr std::array<const char*, 3> kAmpSlotDefaultModelFileNames = {"Amp1.nam", "Amp2.nam", "Amp3.nam"};
constexpr double kDelayManualTempoDefaultBPM = 120.0;
constexpr double kDelayManualTempoMinBPM = 10.0;
constexpr double kDelayManualTempoMaxBPM = 350.0;
constexpr double kDelayHostTempoMinBPM = 1.0;
constexpr double kDelayHostTempoMaxBPM = 400.0;
constexpr int kDefaultFXDelayTimeMode = 1; // MS
#ifdef APP_API
constexpr int kDefaultDelayTempoSource = 1; // Manual in standalone.
#else
constexpr int kDefaultDelayTempoSource = 0; // Host auto in plugin formats.
#endif

struct EffectiveMonoInputAnalysis
{
  bool isEffectivelyMono = false;
  int monoSourceChannel = 0; // 0 = left, 1 = right
};

struct StereoSideSilenceAnalysis
{
  std::array<bool, 2> sideSilent = {false, false};
};

EffectiveMonoInputAnalysis AnalyzeEffectiveMonoInputBlock(const sample* inputLeft, const sample* inputRight,
                                                          const size_t numFrames)
{
  EffectiveMonoInputAnalysis result{};
  if (inputLeft == nullptr || inputRight == nullptr || numFrames == 0)
    return result;
  if (inputLeft == inputRight)
  {
    result.isEffectivelyMono = true;
    result.monoSourceChannel = 0;
    return result;
  }

  double diffEnergy = 0.0;
  double midEnergy = 0.0;
  double leftEnergy = 0.0;
  double rightEnergy = 0.0;
  double maxAbsDiff = 0.0;
  for (size_t s = 0; s < numFrames; ++s)
  {
    const double left = static_cast<double>(inputLeft[s]);
    const double right = static_cast<double>(inputRight[s]);
    const double diff = left - right;
    const double mid = 0.5 * (left + right);
    diffEnergy += diff * diff;
    midEnergy += mid * mid;
    leftEnergy += left * left;
    rightEnergy += right * right;
    maxAbsDiff = std::max(maxAbsDiff, std::abs(diff));
  }

  const double silenceEnergy = kEffectiveMonoSilenceMidEnergy * static_cast<double>(numFrames);
  const double louderEnergy = std::max(leftEnergy, rightEnergy);
  if (louderEnergy <= silenceEnergy)
  {
    result.isEffectivelyMono = true;
    result.monoSourceChannel = 0;
    return result;
  }

  if (maxAbsDiff > kEffectiveMonoMaxPeakDiff)
    return result;

  if (midEnergy <= kEffectiveMonoSilenceMidEnergy)
  {
    result.isEffectivelyMono = true;
    result.monoSourceChannel = 0;
    return result;
  }

  const double relativeDiff = diffEnergy / (midEnergy + 1.0e-30);
  if (relativeDiff <= kEffectiveMonoMaxRelativeDiff)
  {
    result.isEffectivelyMono = true;
    result.monoSourceChannel = (rightEnergy > leftEnergy) ? 1 : 0;
  }
  return result;
}

StereoSideSilenceAnalysis AnalyzeStereoSideSilenceBlock(const sample* inputLeft, const sample* inputRight,
                                                        const size_t numFrames)
{
  StereoSideSilenceAnalysis result{};
  if (inputLeft == nullptr || inputRight == nullptr || numFrames == 0)
    return result;

  double leftEnergy = 0.0;
  double rightEnergy = 0.0;
  for (size_t s = 0; s < numFrames; ++s)
  {
    const double left = static_cast<double>(inputLeft[s]);
    const double right = static_cast<double>(inputRight[s]);
    leftEnergy += left * left;
    rightEnergy += right * right;
  }

  const double silenceEnergy = kStereoSideBypassSilenceEnergy * static_cast<double>(numFrames);
  if (leftEnergy <= silenceEnergy && rightEnergy <= silenceEnergy)
  {
    result.sideSilent[0] = true;
    result.sideSilent[1] = true;
    return result;
  }

  result.sideSilent[0] = leftEnergy <= std::max(silenceEnergy, rightEnergy * kStereoSideBypassOneSidedEnergyRatio);
  result.sideSilent[1] = rightEnergy <= std::max(silenceEnergy, leftEnergy * kStereoSideBypassOneSidedEnergyRatio);
  return result;
}

class StandalonePresetNameEntryControl : public ITextControl
{
public:
  StandalonePresetNameEntryControl()
  : ITextControl(IRECT(), "")
  {
    mIgnoreMouse = true;
    SetTextEntryLength(128);
    Hide(true);
  }

  void SetCompletionHandler(std::function<void(const char*)> handler)
  {
    mCompletionHandler = std::move(handler);
  }

  void OnTextEntryCompletion(const char* str, int valIdx) override
  {
    (void) valIdx;
    if (mCompletionHandler)
      mCompletionHandler(str != nullptr ? str : "");
  }

private:
  std::function<void(const char*)> mCompletionHandler;
};

std::filesystem::path GetStandaloneStateFilePath()
{
#if defined(_WIN32)
  const char* localAppData = std::getenv("LOCALAPPDATA");
  if (localAppData == nullptr || localAppData[0] == '\0')
    localAppData = std::getenv("APPDATA");
  if (localAppData == nullptr || localAppData[0] == '\0')
    return {};
  return std::filesystem::path(localAppData) / BUNDLE_NAME / kStandaloneStateFileName;
#elif defined(OS_MAC)
  const char* home = std::getenv("HOME");
  if (home == nullptr || home[0] == '\0')
    return {};
  return std::filesystem::path(home) / "Library" / "Application Support" / BUNDLE_NAME / kStandaloneStateFileName;
#else
  return {};
#endif
}

std::filesystem::path GetStandalonePresetDirectoryPath()
{
  const auto statePath = GetStandaloneStateFilePath();
  if (statePath.empty())
    return {};
  return statePath.parent_path() / kStandalonePresetDirName;
}

std::filesystem::path GetStandaloneUserPresetDirectoryPath()
{
  const auto presetDir = GetStandalonePresetDirectoryPath();
  if (presetDir.empty())
    return {};
  return presetDir / kStandaloneUserPresetSubdirName;
}

std::filesystem::path GetStandaloneFactoryPresetDirectoryPath()
{
  const auto presetDir = GetStandalonePresetDirectoryPath();
  if (presetDir.empty())
    return {};
  return presetDir / kStandaloneFactoryPresetSubdirName;
}

std::filesystem::path EnsureStandalonePresetExtension(std::filesystem::path filePath)
{
  if (filePath.empty())
    return filePath;
  if (filePath.extension() != ("." + std::string(kStandalonePresetExtension)))
    filePath.replace_extension(kStandalonePresetExtension);
  return filePath;
}

std::string TrimWhitespace(std::string value)
{
  auto isNotSpace = [](const unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), isNotSpace));
  value.erase(std::find_if(value.rbegin(), value.rend(), isNotSpace).base(), value.end());
  return value;
}

std::string SanitizeStandalonePresetName(std::string rawName)
{
  rawName = TrimWhitespace(std::move(rawName));
  for (char& ch : rawName)
  {
    const unsigned char byte = static_cast<unsigned char>(ch);
    if (byte < 32 || ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '/' || ch == '\\' || ch == '|'
        || ch == '?' || ch == '*')
    {
      ch = '_';
    }
  }

  rawName = TrimWhitespace(std::move(rawName));
  if (rawName.empty())
    rawName = "Preset";
  return rawName;
}

IText MakePresetNameEntryText(const IText& sourceText)
{
  IText text = sourceText;
  text.mSize = 18.0f;
  text.mVAlign = EVAlign::Middle;
  return text;
}

bool LoadChunkFromFile(const std::filesystem::path& filePath, IByteChunk& chunk)
{
  if (filePath.empty())
    return false;

  std::ifstream input(filePath, std::ios::binary | std::ios::ate);
  if (!input.is_open())
    return false;

  const std::streamoff size = input.tellg();
  if (size <= 0 || size > kMaxStandaloneStateBytes)
    return false;

  chunk.Clear();
  chunk.Resize(static_cast<int>(size));
  input.seekg(0, std::ios::beg);
  input.read(reinterpret_cast<char*>(chunk.GetData()), size);
  return input.good();
}

bool SaveChunkToFile(const std::filesystem::path& filePath, const IByteChunk& chunk)
{
  if (filePath.empty())
    return false;

  std::error_code ec;
  std::filesystem::create_directories(filePath.parent_path(), ec);
  if (ec)
    return false;

  std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
  if (!output.is_open())
    return false;

  if (chunk.Size() > 0 && chunk.GetData() != nullptr)
    output.write(reinterpret_cast<const char*>(chunk.GetData()), chunk.Size());
  return output.good();
}

#ifdef APP_API
bool LoadStandaloneStateChunk(IByteChunk& chunk)
{
  return LoadChunkFromFile(GetStandaloneStateFilePath(), chunk);
}

void SaveStandaloneStateChunk(const IByteChunk& chunk)
{
  SaveChunkToFile(GetStandaloneStateFilePath(), chunk);
}
#endif

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
    .WithShowLabel(false)
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
  for (auto& pendingModel : mPendingLoadedSlotModel)
    pendingModel.store(nullptr, std::memory_order_relaxed);
  for (auto& pendingModelRight : mPendingLoadedSlotModelRight)
    pendingModelRight.store(nullptr, std::memory_order_relaxed);
  for (auto& pendingRequestId : mPendingLoadedSlotRequestId)
    pendingRequestId.store(0, std::memory_order_relaxed);
  for (auto& shouldRemoveSlotModel : mShouldRemoveModelSlot)
    shouldRemoveSlotModel.store(false, std::memory_order_relaxed);
  for (auto& slotState : mAmpSlotModelState)
    slotState.store(kAmpSlotModelStateEmpty, std::memory_order_relaxed);
  for (auto& slotHasLoudness : mAmpSlotHasLoudness)
    slotHasLoudness.store(false, std::memory_order_relaxed);
  for (auto& slotHasCalibration : mAmpSlotHasCalibration)
    slotHasCalibration.store(false, std::memory_order_relaxed);
  mStompHasLoudness.store(false, std::memory_order_relaxed);
  mStompHasCalibration.store(false, std::memory_order_relaxed);
  for (auto& uiEvent : mSlotLoadUIEvent)
    uiEvent.store(kSlotLoadUIEventNone, std::memory_order_relaxed);
  for (auto& requestId : mSlotLoadRequestId)
    requestId.store(0, std::memory_order_relaxed);
  mPendingAmpSlotSwitch.store(-1, std::memory_order_relaxed);
  mCurrentModelSlot = mAmpSelectorIndex;

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
  GetParam(kInputStereoMode)->InitBool("Input Stereo", false);
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
  GetParam(kFXDelayTimeMode)->InitEnum("FX Delay Time Mode", kDefaultFXDelayTimeMode, {"Sync", "MS"});
  GetParam(kFXDelayPingPong)->InitBool("FX Delay PingPong", false);
  GetParam(kFXDelayDucker)->InitDouble("FX Delay Ducker", 0.0, 0.0, 100.0, 0.1, "%");
  GetParam(kDelayTempoSource)->InitEnum("FX Delay Tempo Source", kDefaultDelayTempoSource, {"Auto", "Manual"});
  GetParam(kDelayManualTempoBPM)
    ->InitDouble("FX Delay Tempo", kDelayManualTempoDefaultBPM, kDelayManualTempoMinBPM, kDelayManualTempoMaxBPM, 1.0, "BPM");
  GetParam(kFXReverbActive)->InitBool("FX Reverb", false);
  GetParam(kFXReverbMix)->InitDouble("FX Reverb Mix", 20.0, 0.0, 100.0, 0.1, "%");
  GetParam(kFXReverbDecay)
    ->InitDouble("FX Reverb Decay",
                 1.8,
                 0.1,
                 10.0,
                 0.1,
                 "",
                 0,
                 "",
                 IParam::ShapeLinear(),
                 IParam::kUnitCustom,
                 [](double value, WDL_String& display) {
                   const double sizeNormalized = std::clamp((value - 0.1) / 9.9, 0.0, 1.0);
                   const int sizeDisplay = static_cast<int>(std::lround(1.0 + sizeNormalized * 99.0));
                   display.SetFormatted(16, "%d", sizeDisplay);
                 });
  GetParam(kFXReverbPreDelayMs)->InitDouble("FX Reverb PreDelay", 25.0, 0.0, 250.0, 1.0, "ms");
  GetParam(kFXReverbTone)->InitDouble("FX Reverb Tone", 50.0, 0.0, 100.0, 0.1, "%");
  GetParam(kFXDelayLowCutHz)->InitDouble("FX Delay LoCut", 20.0, 20.0, 2000.0, 1.0, "Hz");
  GetParam(kFXDelayHighCutHz)->InitDouble("FX Delay HiCut", 20000.0, 1000.0, 20000.0, 10.0, "Hz");
  GetParam(kFXReverbLowCutHz)->InitDouble("FX Reverb LoCut", 20.0, 20.0, 2000.0, 1.0, "Hz");
  GetParam(kFXReverbHighCutHz)->InitDouble("FX Reverb HiCut", 20000.0, 1000.0, 20000.0, 10.0, "Hz");
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
  mTopNavBypassed[static_cast<size_t>(TopNavSection::Tuner)] = !GetParam(kTunerActive)->Bool();
  _SetMasterGain();
  _CaptureAmpSlotState(mAmpSelectorIndex);
  for (int slotIndex = 0; slotIndex < static_cast<int>(mAmpSlotStates.size()); ++slotIndex)
  {
    if (slotIndex != mAmpSelectorIndex)
      mAmpSlotStates[slotIndex] = mAmpSlotStates[mAmpSelectorIndex];
    _ApplyAmpSlotStateToToneStack(slotIndex);
  }

  mNoiseGateTrigger.AddListener(&mNoiseGateGain);

#if NAM_RELEASE_MODE
  mAmpWorkflowMode = AmpWorkflowMode::Release;
  mAmpSlotModelEditLocked.fill(true);
#else
  mAmpWorkflowMode = AmpWorkflowMode::Rig;
  mAmpSlotModelEditLocked.fill(false);
#endif
  for (auto& fixedPath : mAmpSlotFixedModelPaths)
    fixedPath.Set("");
  for (auto& source : mAmpSlotModelSources)
  {
    source.kind = AmpSlotModelSourceKind::ExternalPath;
    source.value.Set("");
  }

  mMakeGraphicsFunc = [&]() {

#ifdef OS_IOS
    auto scaleFactor = GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT) * 0.85f;
#elif defined(APP_API)
    auto scaleFactor = 1.2f;
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
    const auto inputMonoSVG = pGraphics->LoadSVG(INPUT_MONO_SVG_FN);
    const auto inputStereoSVG = pGraphics->LoadSVG(INPUT_STEREO_SVG_FN);
    const auto ampActiveSVG = pGraphics->LoadSVG(AMP_ACTIVE_SVG_FN);
    const auto ampPickerActiveSVG = pGraphics->LoadSVG(AMPPICKER_ACTIVE_SVG_FN);
    const auto stompActiveSVG = pGraphics->LoadSVG(STOMP_ACTIVE_SVG_FN);
    const auto eqActiveSVG = pGraphics->LoadSVG(EQ_ACTIVE_SVG_FN);
    const auto cabActiveSVG = pGraphics->LoadSVG(CAB_ACTIVE_SVG_FN);
    const auto fxActiveSVG = pGraphics->LoadSVG(FX_ACTIVE_SVG_FN);
    const auto tunerActiveSVG = pGraphics->LoadSVG(TUNER_ACTIVE_SVG_FN);
    const auto outerKnobBackgroundSVG = pGraphics->LoadSVG(FLATKNOBBACKGROUND_SVG_FN);

    const auto amp2BackgroundBitmap = pGraphics->LoadBitmap(AMP2BACKGROUND_FN);
    const auto settingsBackgroundBitmap = pGraphics->LoadBitmap(SETTINGSBACKGROUND_FN);
    const auto fileBackgroundBitmap = pGraphics->LoadBitmap(FILEBACKGROUND_FN);
    const auto inputLevelBackgroundBitmap = pGraphics->LoadBitmap(INPUTLEVELBACKGROUND_FN);
    const auto ampKnobBackgroundBitmap = pGraphics->LoadBitmap(KNOBBACKGROUND_FN);
    const auto switchOffBitmap = pGraphics->LoadBitmap(SWITCH_OFF_FN);
    const auto switchOnBitmap = pGraphics->LoadBitmap(SWITCH_ON_FN);
    const auto switchHandleBitmap = pGraphics->LoadBitmap(SLIDESWITCHHANDLE_FN);
    const auto meterBackgroundBitmap = pGraphics->LoadBitmap(METERBACKGROUND_FN);
    const auto pedalKnobBitmap = pGraphics->LoadBitmap(PEDALKNOB_FN);
    const auto pedalKnobShadowBitmap = pGraphics->LoadBitmap(PEDALKNOBSHADOW_FN);
    const auto eqFaderKnobBitmap = pGraphics->LoadBitmap(EQFADERKNOB_FN);
    const auto stompButtonUpBitmap = pGraphics->LoadBitmap(STOMPBUTTONUP_FN);
    const auto stompButtonDownBitmap = pGraphics->LoadBitmap(STOMPBUTTONDOWN_FN);
    const auto greenLedOnBitmap = pGraphics->LoadBitmap(GREENLEDON_FN);
    const auto greenLedOffBitmap = pGraphics->LoadBitmap(GREENLEDOFF_FN);
    const auto redLedOnBitmap = pGraphics->LoadBitmap(REDLEDON_FN);
    const auto redLedOffBitmap = pGraphics->LoadBitmap(REDLEDOFF_FN);
    const auto smallOnOffOffBitmap = pGraphics->LoadBitmap(SMALLONOFF_OFF_FN);
    const auto smallOnOffOnBitmap = pGraphics->LoadBitmap(SMALLONOFF_ON_FN);

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
    pGraphics->AttachPopupMenuControl(
      IText(18.0f, COLOR_WHITE.WithOpacity(0.92f), "ArialNarrow-Bold", EAlign::Near, EVAlign::Middle));
    if (auto* pPopupMenuControl = pGraphics->GetPopupMenuControl())
    {
      pPopupMenuControl->SetPanelColor(IColor(245, 24, 24, 28));
      pPopupMenuControl->SetCellBackgroundColor(IColor(60, 255, 255, 255));
      pPopupMenuControl->SetItemColor(COLOR_WHITE.WithOpacity(0.92f));
      pPopupMenuControl->SetItemMouseoverColor(COLOR_WHITE);
      pPopupMenuControl->SetDisabledItemColor(COLOR_GRAY.WithOpacity(0.78f));
      pPopupMenuControl->SetSeparatorColor(IColor(70, 255, 255, 255));
    }
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
    constexpr float kFXEqSliderW = 14.0f;
    constexpr float kFXEqSliderH = 150.0f;
    constexpr float kFXEqSliderGap = 51.9f;
    const float fxEqSliderTopY = designToUIY(1038.0f);
    const float fxEqSliderStartX = designToUIX(781.0f);
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
    const float fxEqOutputSliderX = designToUIX(2358.0f); // Independent X anchor for EQ output gain slider.
    const auto fxEqOutputArea =
      IRECT(fxEqOutputSliderX - 0.5f * kFXEqSliderW, fxEqSliderTopY, fxEqOutputSliderX + 0.5f * kFXEqSliderW,
            fxEqSliderTopY + kFXEqSliderH);

    const float fxEqSwitchX = designToUIX(1555.0f);
    const float fxEqSwitchY = designToUIY(1650.0f);
    const auto fxEqSwitchArea =
      IRECT(fxEqSwitchX - 0.5f * stompButtonW, fxEqSwitchY - 0.5f * stompButtonH, fxEqSwitchX + 0.5f * stompButtonW,
            fxEqSwitchY + 0.5f * stompButtonH);
    const auto fxEqOnLedArea =
      IRECT(fxEqSwitchArea.MW() - 12.0f, fxEqSwitchArea.B - 304.0f, fxEqSwitchArea.MW() + 12.0f, fxEqSwitchArea.B - 280.0f);

    // FX section first pass against refreshed background art:
    // Delay row (top), Reverb row (bottom), and left-anchored amp-style on/off switches.
    const float fxDelayKnobY = designToUIY(800.0f);
    const auto fxDelayMixArea = makePedalKnobArea(designToUIX(1570.0f), fxDelayKnobY);
    const auto fxDelayFeedbackArea = makePedalKnobArea(designToUIX(1810.0f), fxDelayKnobY);
    const auto fxDelayTimeArea = makePedalKnobArea(designToUIX(2045.0f), fxDelayKnobY);
    const auto fxDelayLowCutArea = makePedalKnobArea(designToUIX(2290.0f), fxDelayKnobY);
    const auto fxDelayHighCutArea = makePedalKnobArea(designToUIX(2535.0f), fxDelayKnobY);
    const auto fxDelayDuckerArea = makePedalKnobArea(designToUIX(1566.0f), designToUIY(1055.0f));
    const float fxDelaySwitchX = designToUIX(430.0f);
    const float fxDelaySwitchY = designToUIY(850.0f);
    const auto fxDelaySwitchArea = IRECT(fxDelaySwitchX - 0.5f * modelSwitchWidth,
                                         fxDelaySwitchY - 0.5f * modelSwitchHeight,
                                         fxDelaySwitchX + 0.5f * modelSwitchWidth,
                                         fxDelaySwitchY + 0.5f * modelSwitchHeight);
    const float fxDelaySyncCenterX = designToUIX(2050.0f);
    const float fxDelaySyncCenterY = designToUIY(1100.0f);
    const auto fxDelaySyncModeArea = IRECT(fxDelaySyncCenterX - 20.0f, fxDelaySyncCenterY - 9.0f,
                                           fxDelaySyncCenterX + 20.0f, fxDelaySyncCenterY + 9.0f);
    const float fxDelayPingPongCenterX = designToUIX(1811.0f);
    const float fxDelayPingPongCenterY = fxDelaySyncCenterY;
    const auto fxDelayPingPongModeArea =
      IRECT(fxDelayPingPongCenterX - 20.0f, fxDelayPingPongCenterY - 9.0f, fxDelayPingPongCenterX + 20.0f,
            fxDelayPingPongCenterY + 9.0f);
    // Delay LCD display window (left side of the delay unit graphic).
    const auto fxDelayDigitalReadoutArea =
      IRECT(designToUIX(590.0f), designToUIY(685.0f), designToUIX(1340.0f), designToUIY(1060.0f));

    const float fxReverbKnobY = designToUIY(1448.0f);
    const auto fxReverbMixArea = makePedalKnobArea(designToUIX(740.0f), fxReverbKnobY);
    const auto fxReverbDecayArea = makePedalKnobArea(designToUIX(1075.0f), fxReverbKnobY);
    const auto fxReverbPreDelayArea = makePedalKnobArea(designToUIX(1400.0f), fxReverbKnobY);
    const auto fxReverbToneArea = makePedalKnobArea(designToUIX(1720.0f), fxReverbKnobY);
    const auto fxReverbLowCutArea = makePedalKnobArea(designToUIX(2050.0f), fxReverbKnobY);
    const auto fxReverbHighCutArea = makePedalKnobArea(designToUIX(2380.0f), fxReverbKnobY);
    const float fxReverbSwitchX = designToUIX(430.0f);
    const float fxReverbSwitchY = designToUIY(1520.0f);
    const auto fxReverbSwitchArea = IRECT(fxReverbSwitchX - 0.5f * modelSwitchWidth,
                                          fxReverbSwitchY - 0.5f * modelSwitchHeight,
                                          fxReverbSwitchX + 0.5f * modelSwitchWidth,
                                          fxReverbSwitchY + 0.5f * modelSwitchHeight);


    // Gate/EQ toggle row (independent group)
    const float toggleTop = frontKnobTop + 86.0f;
    const auto eqToggleArea = IRECT(midKnobArea.MW() - 17.0f, toggleTop, midKnobArea.MW() + 17.0f, toggleTop + 24.0f);

    // EQ-page HPF/LPF anchors follow the printed marker positions on the EQ background.
    const float eqFilterKnobCenterY = designToUIY(1585.0f);
    const auto hpfKnobArea = makePedalKnobArea(designToUIX(840.0f), eqFilterKnobCenterY);
    const auto lpfKnobArea = makePedalKnobArea(designToUIX(2268.0f), eqFilterKnobCenterY);
    constexpr float kInputModeSwitchCenterXOffset = -80.0f;
    constexpr float kInputModeSwitchCenterYOffset = 0.0f;
    constexpr float kInputModeSwitchWidth = 38.0f;
    constexpr float kInputModeSwitchHeight = 20.0f;
    const float inputModeSwitchCenterX = leftTransposeCenterX + kInputModeSwitchCenterXOffset;
    const float inputModeSwitchCenterY = topUtilityRowArea.MH() + kInputModeSwitchCenterYOffset;
    const auto inputModeSwitchArea = IRECT(inputModeSwitchCenterX - 0.5f * kInputModeSwitchWidth,
                                           inputModeSwitchCenterY - 0.5f * kInputModeSwitchHeight,
                                           inputModeSwitchCenterX + 0.5f * kInputModeSwitchWidth,
                                           inputModeSwitchCenterY + 0.5f * kInputModeSwitchHeight);
    constexpr float kSyncButtonGap = 16.0f;
    constexpr float kSyncButtonWidth = 56.0f;
    constexpr float kSyncButtonHeight = 22.0f;
    const float syncButtonCenterX = inputModeSwitchArea.R + kSyncButtonGap + 0.5f * kSyncButtonWidth;
    const auto delaySyncButtonArea = IRECT(syncButtonCenterX - 0.5f * kSyncButtonWidth,
                                           inputModeSwitchCenterY - 0.5f * kSyncButtonHeight,
                                           syncButtonCenterX + 0.5f * kSyncButtonWidth,
                                           inputModeSwitchCenterY + 0.5f * kSyncButtonHeight);
    constexpr float kTempoBPMFieldGap = 10.0f;
    constexpr float kTempoBPMFieldWidth = 52.0f;
    constexpr float kTempoBPMFieldHeight = 22.0f;
    const auto delayTempoBPMFieldArea =
      IRECT(delaySyncButtonArea.R + kTempoBPMFieldGap,
            inputModeSwitchCenterY - 0.5f * kTempoBPMFieldHeight,
            delaySyncButtonArea.R + kTempoBPMFieldGap + kTempoBPMFieldWidth,
            inputModeSwitchCenterY + 0.5f * kTempoBPMFieldHeight);
    constexpr float kTempoBPMLabelGap = 4.0f;
    constexpr float kTempoBPMLabelWidth = 36.0f;
    const auto delayTempoBPMLabelArea =
      IRECT(delayTempoBPMFieldArea.R + kTempoBPMLabelGap,
            delayTempoBPMFieldArea.T,
            delayTempoBPMFieldArea.R + kTempoBPMLabelGap + kTempoBPMLabelWidth,
            delayTempoBPMFieldArea.B);

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
    const float topNavIconGap = 19.0f;
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
    constexpr float kModelCapabilityTopPad = 2.0f;
    constexpr float kModelCapabilityRowHeight = 11.0f;
    constexpr float kModelCapabilityRowGap = 1.0f;
    auto makeCapabilityArea = [=](const IRECT& pickerArea, const int row) {
      const float top = pickerArea.B + kModelCapabilityTopPad + row * (kModelCapabilityRowHeight + kModelCapabilityRowGap);
      return IRECT(pickerArea.L + 8.0f, top, pickerArea.R - 8.0f, top + kModelCapabilityRowHeight);
    };
    const auto settingsAmp1HasLoudnessArea = makeCapabilityArea(settingsAmpModelArea1, 0);
    const auto settingsAmp1HasCalibrationArea = makeCapabilityArea(settingsAmpModelArea1, 1);
    const auto settingsAmp2HasLoudnessArea = makeCapabilityArea(settingsAmpModelArea2, 0);
    const auto settingsAmp2HasCalibrationArea = makeCapabilityArea(settingsAmpModelArea2, 1);
    const auto settingsAmp3HasLoudnessArea = makeCapabilityArea(settingsAmpModelArea3, 0);
    const auto settingsAmp3HasCalibrationArea = makeCapabilityArea(settingsAmpModelArea3, 1);
    const auto settingsStompHasLoudnessArea = makeCapabilityArea(settingsStompModelArea, 0);
    const auto settingsStompHasCalibrationArea = makeCapabilityArea(settingsStompModelArea, 1);
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
    const float topNavEqWidth = scaledWidthForHeightSVG(eqActiveSVG, topNavIconHeight);
    const float topNavFxWidth = scaledWidthForHeightSVG(fxActiveSVG, topNavIconHeight);
    const float topNavRowWidth =
      topNavStompWidth + topNavAmpWidth + topNavCabWidth + topNavEqWidth + topNavFxWidth + 4.0f * topNavIconGap;
    // Keep section icons on the same header strip as the settings cog, centered as a group.
    const float topNavLeft = topBarArea.MW() - 0.5f * topNavRowWidth;
    const float topNavTop = topBarIconRowTop;
    // Visual order: Stomp -> Amp -> Cab -> EQ -> FX
    const auto topNavStompArea = IRECT(topNavLeft, topNavTop, topNavLeft + topNavStompWidth, topNavTop + topNavIconHeight);
    const auto topNavAmpArea = IRECT(topNavStompArea.R + topNavIconGap, topNavTop,
                                     topNavStompArea.R + topNavIconGap + topNavAmpWidth, topNavTop + topNavIconHeight);
    const auto topNavCabArea = IRECT(topNavAmpArea.R + topNavIconGap, topNavTop,
                                     topNavAmpArea.R + topNavIconGap + topNavCabWidth, topNavTop + topNavIconHeight);
    const auto topNavEqArea = IRECT(topNavCabArea.R + topNavIconGap, topNavTop,
                                    topNavCabArea.R + topNavIconGap + topNavEqWidth, topNavTop + topNavIconHeight);
    const auto topNavFxArea = IRECT(topNavEqArea.R + topNavIconGap, topNavTop,
                                    topNavEqArea.R + topNavIconGap + topNavFxWidth, topNavTop + topNavIconHeight);
    // Tuner is a tool icon, placed to the left of settings rather than in the section strip.
    constexpr float kTunerToolGap = 20.0f;
    const float tunerToolRight = settingsButtonArea.L - kTunerToolGap;
    const float tunerToolLeft = std::max(contentArea.L, tunerToolRight - topNavTunerWidth);
    const float topToolRowTop = topUtilityIconCenterY - 0.5f * kTunerToolIconHeight;
    const auto topNavTunerArea =
      IRECT(
        tunerToolLeft, topToolRowTop, tunerToolLeft + topNavTunerWidth, topToolRowTop + kTunerToolIconHeight);
    // Preset strip in utility row (centered), with previous/next buttons and current preset text.
    constexpr float kPresetStripWidth = 310.0f;
    constexpr float kPresetStripHeight = 37.0f;
    constexpr float kPresetButtonSize = 25.0f;
    const float presetStripLeft = topUtilityRowArea.MW() - 0.5f * kPresetStripWidth;
    const float presetStripTop = topUtilityRowArea.MH() - 0.5f * kPresetStripHeight;
    const auto presetStripArea = IRECT(
      presetStripLeft, presetStripTop, presetStripLeft + kPresetStripWidth, presetStripTop + kPresetStripHeight);
    constexpr float kPresetArrowYOffset = 1.0f;
    const float presetArrowTop = presetStripArea.MH() - 0.5f * kPresetButtonSize + kPresetArrowYOffset;
    const auto presetPrevArea = IRECT(
      presetStripArea.L, presetArrowTop, presetStripArea.L + kPresetButtonSize, presetArrowTop + kPresetButtonSize);
    const auto presetNextArea = IRECT(
      presetStripArea.R - kPresetButtonSize, presetArrowTop, presetStripArea.R, presetArrowTop + kPresetButtonSize);
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
    // Footer amp selector strip uses dedicated picker icon art.
    const float footerAmpIconHeight = 43.0f;
    const float footerAmpIconWidth =
      (ampPickerActiveSVG.IsValid() && ampPickerActiveSVG.H() > 0.0f)
      ? (ampPickerActiveSVG.W() * (footerAmpIconHeight / ampPickerActiveSVG.H()))
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
        _RequestModelLoadForSlot(fileName, slotIndex, ctrlTag, true);
        _MarkStandalonePresetDirty();
        mAmpSlotStates[slotIndex].modelToggle = 1.0;
        mAmpSlotStates[slotIndex].modelToggleTouched = true;
        if (mAmpSelectorIndex == slotIndex)
        {
          GetParam(kModelToggle)->Set(1.0);
          SendParameterValueFromDelegate(kModelToggle, GetParam(kModelToggle)->GetNormalized(), true);
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
          _MarkStandalonePresetDirty();
        }
        SendParameterValueFromDelegate(kStompBoostActive, GetParam(kStompBoostActive)->GetNormalized(), true);
      }
    };

    // IR loader button
    auto loadIRLeftCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
      {
        const dsp::wav::LoadReturnCode retCode = _StageIRLeft(fileName);
        if (retCode != dsp::wav::LoadReturnCode::SUCCESS)
        {
          std::stringstream message;
          message << "Failed to load left IR file " << fileName.Get() << ":\n";
          message << dsp::wav::GetMsgForLoadReturnCode(retCode);

          _ShowMessageBox(GetUI(), message.str().c_str(), "Failed to load left IR!", kMB_OK);
        }
        else
        {
          _MarkStandalonePresetDirty();
        }
      }
    };

    auto loadIRRightCompletionHandler = [&](const WDL_String& fileName, const WDL_String& path) {
      if (fileName.GetLength())
      {
        const dsp::wav::LoadReturnCode retCode = _StageIRRight(fileName);
        if (retCode != dsp::wav::LoadReturnCode::SUCCESS)
        {
          std::stringstream message;
          message << "Failed to load right IR file " << fileName.Get() << ":\n";
          message << dsp::wav::GetMsgForLoadReturnCode(retCode);

          _ShowMessageBox(GetUI(), message.str().c_str(), "Failed to load right IR!", kMB_OK);
        }
        else
        {
          _MarkStandalonePresetDirty();
        }
      }
    };

    pGraphics->AttachControl(new NAMBackgroundBitmapControl(b, AMP2BACKGROUND_FN, amp2BackgroundBitmap), kCtrlTagMainBackground);
    // Keep FX background fully visible behind top/footer zones.
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
    pGraphics->AttachControl(new IPanelControl(presetStripArea, IColor(40, 255, 255, 255).WithOpacity(0.10f)));
    pGraphics->AttachControl(new NAMSquareButtonControl(presetPrevArea, [this](IControl*) {
      _SelectStandalonePresetRelative(-1);
    }, leftArrowSVG));
    pGraphics->AttachControl(new NAMSquareButtonControl(presetNextArea, [this](IControl*) {
      _SelectStandalonePresetRelative(1);
    }, rightArrowSVG));
    const IVStyle presetPickerStyle =
      utilityStyle.WithShowValue(false)
        .WithColor(EVColor::kBG, COLOR_TRANSPARENT)
        .WithColor(EVColor::kOFF, COLOR_TRANSPARENT)
        .WithColor(EVColor::kON, IColor(40, 255, 255, 255))
        .WithColor(EVColor::kPR, IColor(28, 255, 255, 255))
        .WithColor(EVColor::kFR, IColor(0, 255, 255, 255))
        .WithColor(EVColor::kHL, IColor(30, 255, 255, 255))
        .WithLabelText(IText(18.0f, COLOR_WHITE.WithOpacity(0.92f), "ArialNarrow-Bold", EAlign::Center, EVAlign::Middle));
    pGraphics->AttachControl(new IVButtonControl(presetLabelArea, SplashClickActionFunc, "Preset", presetPickerStyle), kCtrlTagPresetLabel)
      ->SetAnimationEndActionFunction([this](IControl* pCaller) {
        _ShowStandalonePresetMenu(pCaller->GetRECT());
      });
    pGraphics->AttachControl(new StandalonePresetNameEntryControl(), kCtrlTagStandalonePresetNameEntryProxy);
    _UpdatePresetLabel();
    pGraphics->AttachControl(new NAMSquareButtonControl(
                               tunerCloseArea,
                               [this](IControl*) {
                                 const auto tunerIdx = static_cast<size_t>(TopNavSection::Tuner);
                                 if (tunerIdx < mTopNavBypassed.size())
                                 {
                                   if (!mTopNavBypassed[tunerIdx])
                                     _ToggleTopNavSectionBypass(TopNavSection::Tuner);
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

    IControl* pEqTopIcon = new NAMTopIconControl(topNavEqArea, eqActiveSVG, eqActiveSVG, eqActiveSVG,
                                                 [this]() { _SetTopNavActiveSection(TopNavSection::Eq); },
                                                 [this]() { _ToggleTopNavSectionBypass(TopNavSection::Eq); });
    pGraphics->AttachControl(pEqTopIcon, kCtrlTagTopNavEq)->SetTooltip("EQ");

    IControl* pFxTopIcon = new NAMTopIconControl(topNavFxArea, fxActiveSVG, fxActiveSVG, fxActiveSVG,
                                                 [this]() { _SetTopNavActiveSection(TopNavSection::Fx); },
                                                 [this]() { _ToggleTopNavSectionBypass(TopNavSection::Fx); });
    pGraphics->AttachControl(pFxTopIcon, kCtrlTagTopNavFx)->SetTooltip("FX");
    pGraphics->AttachControl(
      new NAMTopIconControl(topNavTunerArea, tunerActiveSVG, tunerActiveSVG, tunerActiveSVG,
                            [this]() {
                              // Tuner behaves as a regular on/off toggle on normal click.
                              _ToggleTopNavSectionBypass(TopNavSection::Tuner);
                            },
                            [this]() {
                              // Keep Ctrl/Right-click behavior consistent with left-click toggle.
                              _ToggleTopNavSectionBypass(TopNavSection::Tuner);
                            },
                            false),
      kCtrlTagTopNavTuner)
      ->SetTooltip("Tuner");
    pGraphics->AttachControl(new NAMBitmapToggleControl(modelToggleArea, kModelToggle, switchOffBitmap, switchOnBitmap))
      ->SetTooltip("Model On/Off");
    pGraphics->AttachControl(new ISVGSwitchControl(inputModeSwitchArea, {inputMonoSVG, inputStereoSVG}, kInputStereoMode))
      ->SetTooltip("Input mode: Mono = input 1 only, Stereo = input 1+2");
    const IText syncOnText(18.0f, COLOR_WHITE.WithOpacity(0.96f), "Roboto-Regular", EAlign::Center, EVAlign::Middle);
    const IText syncOffText(18.0f, COLOR_GRAY.WithOpacity(0.9f), "Roboto-Regular", EAlign::Center, EVAlign::Middle);
    pGraphics->AttachControl(new NAMSyncTextToggleControl(delaySyncButtonArea, kDelayTempoSource, syncOnText, syncOffText))
      ->SetTooltip("Delay sync: ON uses host tempo when available, OFF uses manual BPM");
    const IVStyle tempoFieldStyle = utilityStyle.WithShowLabel(false)
                                     .WithColor(kFG, COLOR_BLACK.WithOpacity(0.78f))
                                     .WithColor(kHL, COLOR_WHITE.WithOpacity(0.86f))
                                     .WithColor(kFR, COLOR_BLACK.WithOpacity(0.5f))
                                     .WithValueText(IText(18.0f,
                                                          COLOR_WHITE.WithOpacity(0.96f),
                                                          "ArialNarrow-Bold",
                                                          EAlign::Center,
                                                          EVAlign::Middle));
    pGraphics->AttachControl(
      new NAMTempoNumberBoxControl(delayTempoBPMFieldArea, kDelayManualTempoBPM, nullptr, "BPM", tempoFieldStyle, false,
                                   kDelayManualTempoDefaultBPM, kDelayManualTempoMinBPM, kDelayManualTempoMaxBPM, "%0.0f", false))
      ->SetTooltip("Manual tempo (editable when SYNC is OFF)");
    pGraphics->AttachControl(
      new ITextControl(delayTempoBPMLabelArea,
                       "BPM",
                       IText(16.0f, COLOR_WHITE.WithOpacity(0.96f), "Roboto-Regular", EAlign::Near, EVAlign::Middle),
                       COLOR_TRANSPARENT));
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
    pGraphics->AttachControl(new NAMTopIconControl(footerAmpSlot1Area, ampPickerActiveSVG, ampPickerActiveSVG, ampPickerActiveSVG,
                                                   [this]() {
                                                     _SelectAmpSlot(0);
                                                   },
                                                   {}),
                             kCtrlTagAmpSlot1)
      ->SetTooltip("Amp Slot 1");
    pGraphics->AttachControl(new NAMTopIconControl(footerAmpSlot2Area, ampPickerActiveSVG, ampPickerActiveSVG, ampPickerActiveSVG,
                                                   [this]() {
                                                     _SelectAmpSlot(1);
                                                   },
                                                   {}),
                             kCtrlTagAmpSlot2)
      ->SetTooltip("Amp Slot 2");
    pGraphics->AttachControl(new NAMTopIconControl(footerAmpSlot3Area, ampPickerActiveSVG, ampPickerActiveSVG, ampPickerActiveSVG,
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
      "EQ_CONTROLS");
    pGraphics->AttachControl(new NAMBitmapLEDControl(fxEqOnLedArea, redLedOnBitmap, redLedOffBitmap),
                             kCtrlTagFXEQOnLED,
                             "EQ_CONTROLS");
    pGraphics->AttachControl(
      new NAMBitmapToggleControl(fxDelaySwitchArea, kFXDelayActive, switchOffBitmap, switchOnBitmap),
      -1,
      "FX_CONTROLS");
    // kFXDelayTimeMode enum is {Sync, MS}; Sync (0) should display switch ON.
    // NAMBitmapToggleControl draws "off" art at 0 and "on" art at 1, so mapping is intentionally inverted.
    pGraphics->AttachControl(
      new NAMBitmapToggleControl(fxDelaySyncModeArea, kFXDelayTimeMode, smallOnOffOnBitmap, smallOnOffOffBitmap),
      -1,
      "FX_CONTROLS")
      ->SetTooltip("Delay TIME mode: SYNC = note divisions, MS = milliseconds");
    pGraphics->AttachControl(
      new NAMBitmapToggleControl(fxDelayPingPongModeArea, kFXDelayPingPong, smallOnOffOffBitmap, smallOnOffOnBitmap),
      -1,
      "FX_CONTROLS")
      ->SetTooltip("Delay Ping-Pong: OFF = normal stereo, ON = cross-feedback ping-pong");
    pGraphics->AttachControl(new NAMFXDelayDigitalDisplayControl(fxDelayDigitalReadoutArea),
                             kCtrlTagFXDelayReadout,
                             "FX_CONTROLS")
      ->SetTooltip("Delay readout: TIME / MIX / FDBK");
    pGraphics->AttachControl(
      new NAMBitmapToggleControl(fxReverbSwitchArea, kFXReverbActive, switchOffBitmap, switchOnBitmap),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(new NAMSwitchControl(eqToggleArea, kEQActive, "EQ", style, switchHandleBitmap))->Hide(true);

    // The knobs
    constexpr float kSideLabelYOffset = 18.0f;
    constexpr float kSideValueYOffset = -24.0f;
    const IVStyle fxKnobNoLabelStyle = utilityStyle.WithShowLabel(false);
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
    pGraphics->AttachControl(new NAMEQFaderSliderControl(fxEqBand31Area, kFXEQBand31Hz, fxEqSliderStyle, eqFaderKnobBitmap),
                             -1,
                             "EQ_CONTROLS");
    pGraphics->AttachControl(new NAMEQFaderSliderControl(fxEqBand62Area, kFXEQBand62Hz, fxEqSliderStyle, eqFaderKnobBitmap),
                             -1,
                             "EQ_CONTROLS");
    pGraphics->AttachControl(
      new NAMEQFaderSliderControl(fxEqBand125Area, kFXEQBand125Hz, fxEqSliderStyle, eqFaderKnobBitmap),
      -1,
      "EQ_CONTROLS");
    pGraphics->AttachControl(
      new NAMEQFaderSliderControl(fxEqBand250Area, kFXEQBand250Hz, fxEqSliderStyle, eqFaderKnobBitmap),
      -1,
      "EQ_CONTROLS");
    pGraphics->AttachControl(
      new NAMEQFaderSliderControl(fxEqBand500Area, kFXEQBand500Hz, fxEqSliderStyle, eqFaderKnobBitmap),
      -1,
      "EQ_CONTROLS");
    pGraphics->AttachControl(
      new NAMEQFaderSliderControl(fxEqBand1kArea, kFXEQBand1kHz, fxEqSliderStyle, eqFaderKnobBitmap),
      -1,
      "EQ_CONTROLS");
    pGraphics->AttachControl(
      new NAMEQFaderSliderControl(fxEqBand2kArea, kFXEQBand2kHz, fxEqSliderStyle, eqFaderKnobBitmap),
      -1,
      "EQ_CONTROLS");
    pGraphics->AttachControl(
      new NAMEQFaderSliderControl(fxEqBand4kArea, kFXEQBand4kHz, fxEqSliderStyle, eqFaderKnobBitmap),
      -1,
      "EQ_CONTROLS");
    pGraphics->AttachControl(
      new NAMEQFaderSliderControl(fxEqBand8kArea, kFXEQBand8kHz, fxEqSliderStyle, eqFaderKnobBitmap),
      -1,
      "EQ_CONTROLS");
    pGraphics->AttachControl(
      new NAMEQFaderSliderControl(fxEqBand16kArea, kFXEQBand16kHz, fxEqSliderStyle, eqFaderKnobBitmap),
      -1,
      "EQ_CONTROLS");
    pGraphics->AttachControl(
      new NAMEQFaderSliderControl(fxEqOutputArea, kFXEQOutputGain, fxEqSliderStyle, eqFaderKnobBitmap),
      -1,
      "EQ_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxReverbMixArea, kFXReverbMix, "", fxKnobNoLabelStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxReverbDecayArea, kFXReverbDecay, "", fxKnobNoLabelStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(new NAMPedalKnobControl(fxReverbPreDelayArea, kFXReverbPreDelayMs, "", fxKnobNoLabelStyle,
                                                     pedalKnobBitmap, pedalKnobShadowBitmap, kPedalKnobScale, 8.0f, -5.0f),
                             -1,
                             "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxReverbToneArea, kFXReverbTone, "", fxKnobNoLabelStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxReverbLowCutArea, kFXReverbLowCutHz, "", fxKnobNoLabelStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxReverbHighCutArea, kFXReverbHighCutHz, "", fxKnobNoLabelStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxDelayMixArea, kFXDelayMix, "", fxKnobNoLabelStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f, false),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMDelayTimeKnobControl(fxDelayTimeArea, kFXDelayTimeMs, "", fxKnobNoLabelStyle, pedalKnobBitmap,
                                  pedalKnobShadowBitmap, kPedalKnobScale, 8.0f, -5.0f, false),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(new NAMPedalKnobControl(fxDelayFeedbackArea, kFXDelayFeedback, "", fxKnobNoLabelStyle, pedalKnobBitmap,
                                                     pedalKnobShadowBitmap, kPedalKnobScale, 8.0f, -5.0f, false),
                             -1,
                             "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxDelayLowCutArea, kFXDelayLowCutHz, "", fxKnobNoLabelStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxDelayHighCutArea, kFXDelayHighCutHz, "", fxKnobNoLabelStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
      -1,
      "FX_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(fxDelayDuckerArea, kFXDelayDucker, "", fxKnobNoLabelStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale * 0.55f, 8.0f, -5.0f, false),
      -1,
      "FX_CONTROLS")
      ->SetTooltip("Delay DUCKER amount (0% = off)");
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
    pGraphics->AttachControl(
      new NAMPedalKnobControl(hpfKnobArea, kUserHPFFrequency, "", utilityStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
                             -1,
                             "EQ_CONTROLS");
    pGraphics->AttachControl(
      new NAMPedalKnobControl(lpfKnobArea, kUserLPFFrequency, "", utilityStyle, pedalKnobBitmap, pedalKnobShadowBitmap,
                              kPedalKnobScale, 8.0f, -5.0f),
                             -1,
                             "EQ_CONTROLS");
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
    const IVStyle capabilityStyle = utilityStyle.WithLabelText(
      IText(12.0f, PluginColors::HELP_TEXT, "ArialNarrow-Bold", EAlign::Near, EVAlign::Middle));
    pSettingsBox->AddChildControl(
      new NAMReadOnlyCheckboxControl(settingsAmp1HasLoudnessArea, "Has Loudness", capabilityStyle), kCtrlTagAmp1HasLoudness);
    pSettingsBox->AddChildControl(
      new NAMReadOnlyCheckboxControl(settingsAmp1HasCalibrationArea, "Has Calibration", capabilityStyle),
      kCtrlTagAmp1HasCalibration);
    pSettingsBox->AddChildControl(
      new NAMReadOnlyCheckboxControl(settingsAmp2HasLoudnessArea, "Has Loudness", capabilityStyle), kCtrlTagAmp2HasLoudness);
    pSettingsBox->AddChildControl(
      new NAMReadOnlyCheckboxControl(settingsAmp2HasCalibrationArea, "Has Calibration", capabilityStyle),
      kCtrlTagAmp2HasCalibration);
    pSettingsBox->AddChildControl(
      new NAMReadOnlyCheckboxControl(settingsAmp3HasLoudnessArea, "Has Loudness", capabilityStyle), kCtrlTagAmp3HasLoudness);
    pSettingsBox->AddChildControl(
      new NAMReadOnlyCheckboxControl(settingsAmp3HasCalibrationArea, "Has Calibration", capabilityStyle),
      kCtrlTagAmp3HasCalibration);
    pSettingsBox->AddChildControl(
      new NAMReadOnlyCheckboxControl(settingsStompHasLoudnessArea, "Has Loudness", capabilityStyle),
      kCtrlTagStompHasLoudness);
    pSettingsBox->AddChildControl(
      new NAMReadOnlyCheckboxControl(settingsStompHasCalibrationArea, "Has Calibration", capabilityStyle),
      kCtrlTagStompHasCalibration);
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

  IByteChunk initialDefaultPresetChunk;
  if (SerializeState(initialDefaultPresetChunk))
  {
    mDefaultPresetStateChunk.Clear();
    mDefaultPresetStateChunk.PutChunk(&initialDefaultPresetChunk);
    mHasDefaultPresetState = true;
  }

  _StartModelLoadWorker();
}

NeuralAmpModeler::~NeuralAmpModeler()
{
  _StopModelLoadWorker();

  for (auto& pendingModel : mPendingLoadedSlotModel)
  {
    if (auto* ptr = pendingModel.exchange(nullptr, std::memory_order_relaxed))
      delete ptr;
  }
  for (auto& pendingModelRight : mPendingLoadedSlotModelRight)
  {
    if (auto* ptr = pendingModelRight.exchange(nullptr, std::memory_order_relaxed))
      delete ptr;
  }

#ifdef APP_API
  IByteChunk stateChunk;
  if (SerializeState(stateChunk))
    SaveStandaloneStateChunk(stateChunk);
#endif
  _DeallocateIOPointers();
}

void NeuralAmpModeler::ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames)
{
  const size_t numChannelsExternalIn = (size_t)NInChansConnected();
  const size_t numChannelsExternalOut = (size_t)NOutChansConnected();
  const size_t numFrames = (size_t)nFrames;
  const double sampleRate = GetSampleRate();
  const double hostTempoBPM = GetTempo();
  const bool hostTempoValid = std::isfinite(hostTempoBPM) && hostTempoBPM >= kDelayHostTempoMinBPM
                              && hostTempoBPM <= kDelayHostTempoMaxBPM;
  const double manualTempoBPM =
    std::round(std::clamp(GetParam(kDelayManualTempoBPM)->Value(), kDelayManualTempoMinBPM, kDelayManualTempoMaxBPM));
  const bool preferManualTempo = (GetParam(kDelayTempoSource)->Int() != 0);
  const bool useManualTempo = preferManualTempo || !hostTempoValid;
  mDelayUsingManualTempo.store(useManualTempo, std::memory_order_relaxed);
  mDelayHostTempoValid.store(hostTempoValid, std::memory_order_relaxed);
  mDelayTempoBPM.store(useManualTempo ? manualTempoBPM : hostTempoBPM, std::memory_order_relaxed);
  const double hostSamplePos = GetSamplePos();
  mDelaySamplePos.store(std::isfinite(hostSamplePos) ? hostSamplePos : -1.0, std::memory_order_relaxed);
  mDelayTransportIsRunning.store(GetTransportIsRunning(), std::memory_order_relaxed);
  const bool inputStereoMode = GetParam(kInputStereoMode)->Bool();
  const bool hasRightInputChannel = (inputs != nullptr) && (numChannelsExternalIn > 1) && (inputs[1] != nullptr);
  bool stereoCoreRequested = false;
  int effectiveMonoSourceChannel = 0;
#ifdef APP_API
#if NAM_APP_STEREO_CORE_TEST
  stereoCoreRequested = inputStereoMode && hasRightInputChannel;
#endif
#else
  stereoCoreRequested = inputStereoMode && hasRightInputChannel;
#endif

  if (stereoCoreRequested)
  {
    sample* inputLeft = (inputs != nullptr && numChannelsExternalIn > 0) ? inputs[0] : nullptr;
    if (inputLeft == nullptr && inputs != nullptr)
    {
      for (size_t c = 1; c < numChannelsExternalIn; ++c)
      {
        if (inputs[c] != nullptr)
        {
          inputLeft = inputs[c];
          break;
        }
      }
    }
    sample* inputRight = (inputs != nullptr && numChannelsExternalIn > 1) ? inputs[1] : nullptr;
    const auto effectiveMonoAnalysis = AnalyzeEffectiveMonoInputBlock(inputLeft, inputRight, numFrames);
    const bool blockEffectivelyMono = effectiveMonoAnalysis.isEffectivelyMono;
    effectiveMonoSourceChannel = effectiveMonoAnalysis.monoSourceChannel;
    const double safeSampleRate = std::max(1.0, sampleRate);
    const size_t monoEngageSamples =
      std::max<size_t>(1, static_cast<size_t>(std::ceil(kEffectiveMonoEngageSeconds * safeSampleRate)));
    const size_t stereoReleaseSamples =
      std::max<size_t>(1, static_cast<size_t>(std::ceil(kEffectiveMonoReleaseSeconds * safeSampleRate)));
    if (blockEffectivelyMono)
    {
      mEffectiveMonoCandidateSamples = std::min(monoEngageSamples, mEffectiveMonoCandidateSamples + numFrames);
      mEffectiveStereoCandidateSamples = 0;
      if (!mEffectiveMonoCollapseActive && mEffectiveMonoCandidateSamples >= monoEngageSamples)
      {
        mEffectiveMonoCollapseActive = true;
      }
    }
    else
    {
      mEffectiveMonoCandidateSamples = 0;
      if (mEffectiveMonoCollapseActive)
      {
        mEffectiveStereoCandidateSamples =
          std::min(stereoReleaseSamples, mEffectiveStereoCandidateSamples + numFrames);
        if (mEffectiveStereoCandidateSamples >= stereoReleaseSamples)
        {
          mEffectiveMonoCollapseActive = false;
          mEffectiveStereoCandidateSamples = 0;
        }
      }
      else
      {
        mEffectiveStereoCandidateSamples = 0;
      }
    }
  }
  else
  {
    mEffectiveMonoCollapseActive = false;
    mEffectiveMonoCandidateSamples = 0;
    mEffectiveStereoCandidateSamples = 0;
  }

  const size_t numChannelsMonoCore = (stereoCoreRequested && !mEffectiveMonoCollapseActive) ? 2 : 1;
  const size_t numChannelsInternal = kNumChannelsInternal;
  sample* remappedMonoInput[1] = {nullptr};
  sample** processInputs = inputs;
  size_t processNumChannelsExternalIn = numChannelsExternalIn;
  if (numChannelsMonoCore == 1 && stereoCoreRequested && effectiveMonoSourceChannel == 1 && inputs != nullptr
      && numChannelsExternalIn > 1 && inputs[1] != nullptr)
  {
    remappedMonoInput[0] = inputs[1];
    processInputs = remappedMonoInput;
    processNumChannelsExternalIn = 1;
  }

  // Disable floating point denormals
  std::fenv_t fe_state;
  std::feholdexcept(&fe_state);
  disable_denormals();

  if (!_PrepareBuffers(numChannelsInternal, numFrames, false))
  {
    // Fail-safe: never grow buffers in the audio callback. Pass through external input (or silence) for this block.
    if (outputs != nullptr)
    {
      for (size_t outChannel = 0; outChannel < numChannelsExternalOut; ++outChannel)
      {
        auto* out = outputs[outChannel];
        if (out == nullptr)
          continue;

        sample* source = nullptr;
        if (inputs != nullptr && numChannelsExternalIn > 0)
        {
          const size_t mappedInput = std::min(outChannel, numChannelsExternalIn - 1);
          source = inputs[mappedInput];
          if (source == nullptr)
          {
            for (size_t inChannel = 0; inChannel < numChannelsExternalIn; ++inChannel)
            {
              if (inputs[inChannel] != nullptr)
              {
                source = inputs[inChannel];
                break;
              }
            }
          }
        }

        if (source != nullptr)
          std::copy_n(source, numFrames, out);
        else
          std::fill(out, out + numFrames, 0.0f);
      }
    }

    std::feupdateenv(&fe_state);
    _UpdateMeters(inputs, outputs, numFrames, numChannelsExternalIn, numChannelsExternalOut);
    return;
  }
  // Input enters the amp core as mono (standalone) or dual-mono (plugin stereo input).
  _ProcessInput(processInputs, numFrames, processNumChannelsExternalIn, numChannelsMonoCore);
  if (numFrames > 0 && numChannelsMonoCore > 0 && mInputPointers != nullptr && mInputPointers[0] != nullptr)
    mInputSender.ProcessBlock(mInputPointers, (int)numFrames, kCtrlTagInputMeter, 1);
  _ApplyDSPStaging();
  const int activeSlot = std::clamp(mAmpSelectorIndex, 0, static_cast<int>(mToneStacks.size()) - 1);
  auto* activeToneStack = mToneStacks[activeSlot].get();
  const bool ampBypassed = mTopNavBypassed[static_cast<size_t>(TopNavSection::Amp)];
  const bool stompBypassed = mTopNavBypassed[static_cast<size_t>(TopNavSection::Stomp)];
  const bool cabBypassed = mTopNavBypassed[static_cast<size_t>(TopNavSection::Cab)];
  const bool noiseGateActive = GetParam(kNoiseGateActive)->Value() && !stompBypassed;
  const bool haveStereoStomp = (mStompModel != nullptr) && (mStompModelRight != nullptr);
  const bool boostEnabled = GetParam(kStompBoostActive)->Bool() && !stompBypassed
                            && ((numChannelsMonoCore == 1) ? (mStompModel != nullptr) : haveStereoStomp);
  const bool toneStackActive = GetParam(kEQActive)->Value() && !ampBypassed;
  const bool modelActive = GetParam(kModelToggle)->Bool() && !ampBypassed;
  const bool haveStereoModel = (mModel != nullptr) && (mModelRight != nullptr);
  const bool haveModelForCore = (numChannelsMonoCore == 1) ? (mModel != nullptr) : haveStereoModel;
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
      {
        if (outputs == nullptr || outputs[c] == nullptr)
          continue;
        std::fill(outputs[c], outputs[c] + numFrames, 0.0f);
      }
      std::feupdateenv(&fe_state);
      _UpdateMeters(nullptr, outputs, numFrames, 0, numChannelsExternalOut);
      return;
    }
    if (tunerMonitorMode == 1)
    {
      // Clean bypass while tuning, using post-input-gain mono signal.
      std::feupdateenv(&fe_state);
      _ProcessOutput(mInputPointers, outputs, numFrames, numChannelsMonoCore, numChannelsExternalOut);
      _UpdateMeters(nullptr, outputs, numFrames, 0, numChannelsExternalOut);
      return;
    }
    // tunerMonitorMode == 2 -> fall through to full processing path.
  }

  // Lightweight semitone transposer with internal click-free crossfade on 0<->nonzero transitions.
  // We call this every block so fade-out to bypass can complete smoothly.
  mTransposeShifter.ProcessBlock(mInputPointers[0], numFrames, transposeSemitones);
  if (numChannelsMonoCore > 1)
    mTransposeShifterRight.ProcessBlock(mInputPointers[1], numFrames, transposeSemitones);

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
    triggerOutput = mNoiseGateTrigger.Process(mInputPointers, numChannelsMonoCore, numFrames);
  }
  mNoiseGateIsAttenuating.store(noiseGateActive && mNoiseGateTrigger.IsAttenuating(10.0), std::memory_order_relaxed);

  sample** modelInputPointers =
    noiseGateActive ? mNoiseGateGain.Process(triggerOutput, numChannelsMonoCore, numFrames) : triggerOutput;
  std::array<bool, 2> bypassHeavySide = {false, false};
  if (numChannelsMonoCore == 2 && modelInputPointers != nullptr && modelInputPointers[0] != nullptr
      && modelInputPointers[1] != nullptr)
  {
    const auto sideSilence = AnalyzeStereoSideSilenceBlock(modelInputPointers[0], modelInputPointers[1], numFrames);
    const double safeSampleRate = std::max(1.0, sampleRate);
    const size_t bypassEngageSamples =
      std::max<size_t>(1, static_cast<size_t>(std::ceil(kStereoSideBypassEngageSeconds * safeSampleRate)));
    const size_t bypassReleaseSamples =
      std::max<size_t>(1, static_cast<size_t>(std::ceil(kStereoSideBypassReleaseSeconds * safeSampleRate)));
    for (size_t c = 0; c < 2; ++c)
    {
      if (sideSilence.sideSilent[c])
      {
        mStereoSideSilentCandidateSamples[c] =
          std::min(bypassEngageSamples, mStereoSideSilentCandidateSamples[c] + numFrames);
        mStereoSideActiveCandidateSamples[c] = 0;
        if (!mStereoSideBypassActive[c] && mStereoSideSilentCandidateSamples[c] >= bypassEngageSamples)
        {
          mStereoSideBypassActive[c] = true;
          mStereoSideResumeDeClickSamplesRemaining[c] = 0;
        }
      }
      else
      {
        mStereoSideSilentCandidateSamples[c] = 0;
        if (mStereoSideBypassActive[c])
        {
          mStereoSideActiveCandidateSamples[c] =
            std::min(bypassReleaseSamples, mStereoSideActiveCandidateSamples[c] + numFrames);
          if (mStereoSideActiveCandidateSamples[c] >= bypassReleaseSamples)
          {
            mStereoSideBypassActive[c] = false;
            mStereoSideActiveCandidateSamples[c] = 0;
            mStereoSideResumeDeClickSamplesRemaining[c] = kStereoSideBypassResumeDeClickSamples;
          }
        }
        else
        {
          mStereoSideActiveCandidateSamples[c] = 0;
        }
      }
      bypassHeavySide[c] = mStereoSideBypassActive[c];
    }
  }
  else
  {
    for (size_t c = 0; c < 2; ++c)
    {
      mStereoSideBypassActive[c] = false;
      mStereoSideSilentCandidateSamples[c] = 0;
      mStereoSideActiveCandidateSamples[c] = 0;
      mStereoSideResumeDeClickSamplesRemaining[c] = 0;
    }
  }

  if (boostEnabled)
  {
    sample** boostOutPointers = (modelInputPointers == mInputPointers) ? mOutputPointers : mInputPointers;
    if (numChannelsMonoCore == 1)
    {
      mStompModel->process(modelInputPointers, boostOutPointers, nFrames);
    }
    else
    {
      if (!bypassHeavySide[0])
      {
        sample* leftIn[1] = {modelInputPointers[0]};
        sample* leftOut[1] = {boostOutPointers[0]};
        mStompModel->process(leftIn, leftOut, nFrames);
      }
      else
      {
        std::copy_n(modelInputPointers[0], numFrames, boostOutPointers[0]);
      }

      if (!bypassHeavySide[1])
      {
        sample* rightIn[1] = {modelInputPointers[1]};
        sample* rightOut[1] = {boostOutPointers[1]};
        mStompModelRight->process(rightIn, rightOut, nFrames);
      }
      else
      {
        std::copy_n(modelInputPointers[1], numFrames, boostOutPointers[1]);
      }
    }
    modelInputPointers = boostOutPointers;
  }

  if (boostEnabled && boostLevelGain != 1.0)
  {
    for (size_t c = 0; c < numChannelsMonoCore; ++c)
      for (size_t s = 0; s < numFrames; ++s)
        modelInputPointers[c][s] *= boostLevelGain;
  }

  if (modelActive && haveModelForCore)
  {
    if (preModelGain != 1.0)
    {
      for (size_t c = 0; c < numChannelsMonoCore; ++c)
        for (size_t s = 0; s < numFrames; ++s)
          modelInputPointers[c][s] *= preModelGain;
    }
    sample** modelOutPointers = (modelInputPointers == mInputPointers) ? mOutputPointers : mInputPointers;
    if (numChannelsMonoCore == 1)
    {
      mModel->process(modelInputPointers, modelOutPointers, nFrames);
    }
    else
    {
      if (!bypassHeavySide[0])
      {
        sample* leftIn[1] = {modelInputPointers[0]};
        sample* leftOut[1] = {modelOutPointers[0]};
        mModel->process(leftIn, leftOut, nFrames);
      }
      else
      {
        std::copy_n(modelInputPointers[0], numFrames, modelOutPointers[0]);
      }

      if (!bypassHeavySide[1])
      {
        sample* rightIn[1] = {modelInputPointers[1]};
        sample* rightOut[1] = {modelOutPointers[1]};
        mModelRight->process(rightIn, rightOut, nFrames);
      }
      else
      {
        std::copy_n(modelInputPointers[1], numFrames, modelOutPointers[1]);
      }

      if (numFrames > 0)
      {
        for (size_t c = 0; c < 2; ++c)
        {
          if (bypassHeavySide[c])
          {
            mStereoSideResumePrevSample[c] = static_cast<double>(modelOutPointers[c][numFrames - 1]);
            mStereoSideResumeDeClickSamplesRemaining[c] = 0;
            continue;
          }

          int declickRemaining = mStereoSideResumeDeClickSamplesRemaining[c];
          if (declickRemaining > 0)
          {
            const int framesToSmooth = static_cast<int>(std::min(numFrames, static_cast<size_t>(declickRemaining)));
            double prev = mStereoSideResumePrevSample[c];
            int channelRemaining = declickRemaining;
            for (int s = 0; s < framesToSmooth; ++s)
            {
              const double t = 1.0 - static_cast<double>(channelRemaining - 1)
                                         / static_cast<double>(kStereoSideBypassResumeDeClickSamples);
              const double blended = (1.0 - t) * prev + t * static_cast<double>(modelOutPointers[c][s]);
              modelOutPointers[c][s] = static_cast<sample>(blended);
              prev = blended;
              --channelRemaining;
            }
            if (numFrames > static_cast<size_t>(framesToSmooth))
              prev = static_cast<double>(modelOutPointers[c][numFrames - 1]);
            mStereoSideResumePrevSample[c] = prev;
            declickRemaining -= framesToSmooth;
            mStereoSideResumeDeClickSamplesRemaining[c] = std::max(0, declickRemaining);
          }
          else
          {
            mStereoSideResumePrevSample[c] = static_cast<double>(modelOutPointers[c][numFrames - 1]);
          }
        }
      }
    }
    modelInputPointers = modelOutPointers;
  }
  else
  {
    _FallbackDSP(modelInputPointers, mOutputPointers, numChannelsMonoCore, numFrames);
    modelInputPointers = mOutputPointers;
  }
  sample** toneStackOutPointers = (toneStackActive && activeToneStack != nullptr)
                                    ? activeToneStack->Process(modelInputPointers, numChannelsMonoCore, nFrames)
                                    : modelInputPointers;
  if (!ampBypassed && mMasterGain != 1.0)
  {
    for (size_t c = 0; c < numChannelsMonoCore; ++c)
      for (size_t s = 0; s < numFrames; ++s)
        toneStackOutPointers[c][s] *= mMasterGain;
  }

  auto copyMonoToStereo = [this, numFrames](sample** monoPointers) {
    if (monoPointers == nullptr || monoPointers[0] == nullptr)
      return;
    for (size_t s = 0; s < numFrames; ++s)
    {
      const sample monoSample = monoPointers[0][s];
      mOutputArray[0][s] = monoSample;
      mOutputArray[1][s] = monoSample;
    }
  };

  sample** irPointers = mOutputPointers;
  if (numChannelsMonoCore == 1)
    copyMonoToStereo(toneStackOutPointers);
  else
    irPointers = toneStackOutPointers;
  if (GetParam(kIRToggle)->Value() && !cabBypassed)
  {
    const bool haveLeftIR = (mIR != nullptr);
    const bool haveRightIR = (mIRRight != nullptr);
    auto processSingleChannelIR = [numFrames](dsp::ImpulseResponse* ir, sample* inChannel, sample* outChannel) {
      if (ir == nullptr || inChannel == nullptr || outChannel == nullptr)
      {
        if (outChannel != nullptr)
          std::fill(outChannel, outChannel + numFrames, 0.0f);
        return;
      }

      sample* inPtrs[1] = {inChannel};
      sample** irOutPointers = ir->Process(inPtrs, 1, numFrames);
      if (irOutPointers == nullptr || irOutPointers[0] == nullptr)
      {
        std::fill(outChannel, outChannel + numFrames, 0.0f);
        return;
      }

      for (size_t s = 0; s < numFrames; ++s)
        outChannel[s] = irOutPointers[0][s];
    };

    if (haveLeftIR && haveRightIR)
    {
      const double blend = GetParam(kCabIRBlend)->Value() * 0.01;
      const double leftGain = 1.0 - blend;
      const double rightGain = blend;

      if (numChannelsMonoCore == 1)
      {
        sample** irLeftPointers = mIR->Process(toneStackOutPointers, 1, numFrames);
        sample** irRightPointers = mIRRight->Process(toneStackOutPointers, 1, numFrames);
        for (size_t s = 0; s < numFrames; ++s)
        {
          const sample irLeft = irLeftPointers[0][s];
          const sample irRight = irRightPointers[0][s];
          mOutputArray[0][s] = static_cast<sample>(leftGain * irLeft + rightGain * irRight);
          mOutputArray[1][s] = static_cast<sample>(leftGain * irRight + rightGain * irLeft);
        }
      }
      else if (mIRChannel2 != nullptr && mIRRightChannel2 != nullptr)
      {
        processSingleChannelIR(mIR.get(), toneStackOutPointers[0], mInputArray[0].data());
        processSingleChannelIR(mIRRight.get(), toneStackOutPointers[0], mOutputArray[0].data());
        processSingleChannelIR(mIRChannel2.get(), toneStackOutPointers[1], mInputArray[1].data());
        processSingleChannelIR(mIRRightChannel2.get(), toneStackOutPointers[1], mOutputArray[1].data());

        for (size_t s = 0; s < numFrames; ++s)
        {
          const sample irLeftL = mInputArray[0][s];
          const sample irRightL = mOutputArray[0][s];
          const sample irLeftR = mInputArray[1][s];
          const sample irRightR = mOutputArray[1][s];
          mOutputArray[0][s] = static_cast<sample>(leftGain * irLeftL + rightGain * irRightL);
          mOutputArray[1][s] = static_cast<sample>(leftGain * irRightR + rightGain * irLeftR);
        }
      }
      else
      {
        // If stereo IR state isn't ready yet, keep signal path alive.
        for (size_t s = 0; s < numFrames; ++s)
        {
          mOutputArray[0][s] = toneStackOutPointers[0][s];
          mOutputArray[1][s] = toneStackOutPointers[1][s];
        }
      }
      irPointers = mOutputPointers;
    }
    else if (haveLeftIR)
    {
      if (numChannelsMonoCore == 1)
      {
        sample** leftIRPointers = mIR->Process(toneStackOutPointers, 1, numFrames);
        copyMonoToStereo(leftIRPointers);
        irPointers = mOutputPointers;
      }
      else if (mIRChannel2 != nullptr)
      {
        processSingleChannelIR(mIR.get(), toneStackOutPointers[0], mOutputArray[0].data());
        processSingleChannelIR(mIRChannel2.get(), toneStackOutPointers[1], mOutputArray[1].data());
        irPointers = mOutputPointers;
      }
      else
      {
        irPointers = toneStackOutPointers;
      }
    }
    else if (haveRightIR)
    {
      if (numChannelsMonoCore == 1)
      {
        sample** rightIRPointers = mIRRight->Process(toneStackOutPointers, 1, numFrames);
        copyMonoToStereo(rightIRPointers);
        irPointers = mOutputPointers;
      }
      else if (mIRRightChannel2 != nullptr)
      {
        processSingleChannelIR(mIRRight.get(), toneStackOutPointers[0], mOutputArray[0].data());
        processSingleChannelIR(mIRRightChannel2.get(), toneStackOutPointers[1], mOutputArray[1].data());
        irPointers = mOutputPointers;
      }
      else
      {
        irPointers = toneStackOutPointers;
      }
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

  sample** fxReverbPointers = userLowPassPointers2;
  const bool eqBypassed = mTopNavBypassed[static_cast<size_t>(TopNavSection::Eq)];
  const bool fxBypassed = mTopNavBypassed[static_cast<size_t>(TopNavSection::Fx)];

  if (GetParam(kFXEQActive)->Bool() && !eqBypassed && sampleRate > 0.0)
    _ProcessPostCabEQStage(fxReverbPointers, numChannelsInternal, numFrames, sampleRate);

  const bool fxDelayActive = GetParam(kFXDelayActive)->Bool() && !fxBypassed;
  if (mFXDelayBufferSamples > 2 && sampleRate > 0.0)
    _ProcessFXDelayStage(fxReverbPointers, numChannelsInternal, numChannelsMonoCore, numFrames, sampleRate, fxDelayActive);

  const bool fxReverbActive = GetParam(kFXReverbActive)->Bool() && !fxBypassed;
  if (fxReverbActive && !mFXReverbWasActive)
    _ResetFXReverbState();
  mFXReverbWasActive = fxReverbActive;

  if (fxReverbActive && sampleRate > 0.0 && mFXReverbPreDelayBufferSamples > 2)
    _ProcessFXReverbStage(fxReverbPointers, numChannelsInternal, numChannelsMonoCore, numFrames, sampleRate);

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
  _UpdateMeters(nullptr, outputs, numFrames, 0, numChannelsExternalOut);
}

void NeuralAmpModeler::OnReset()
{
  const auto sampleRate = GetSampleRate();
  const int maxBlockSize = GetBlockSize();
  constexpr double kFXDelayMaxSeconds = 2.0;
  constexpr double kFXReverbMaxPreDelaySeconds = 0.30;
  constexpr double kFXReverbCombMaxSizeScale = 2.4;
  constexpr std::array<int, 8> kFXReverbCombBaseDelay = {1423, 1597, 1789, 1999, 2239, 2473, 2741, 3049};
  constexpr std::array<int, 8> kFXReverbCombStereoOffset = {0, 37, 53, 71, 89, 107, 131, 149};
  constexpr std::array<int, 2> kFXReverbPreDiffAllpassBaseDelay = {113, 337};
  constexpr std::array<int, 2> kFXReverbAllpassBaseDelay = {307, 503};
  constexpr std::array<double, 8> kFXReverbRoomEarlyTapMs = {1.2, 2.4, 3.8, 5.5, 7.6, 10.4, 13.7, 17.2};
  constexpr std::array<int, 10> kFXEQParamIdx = {
    kFXEQBand31Hz, kFXEQBand62Hz, kFXEQBand125Hz, kFXEQBand250Hz, kFXEQBand500Hz,
    kFXEQBand1kHz, kFXEQBand2kHz, kFXEQBand4kHz, kFXEQBand8kHz, kFXEQBand16kHz
  };

#ifdef APP_API
  if (!mStandaloneStateLoadAttempted)
  {
    mStandaloneStateLoadAttempted = true;
    IByteChunk startupStateChunk;
    if (LoadStandaloneStateChunk(startupStateChunk))
      UnserializeState(startupStateChunk, 0);
  }
#endif

#if defined(APP_API) && (NAM_STARTUP_TMPLOAD_DEFAULTS > 0)
  if (!mStartupDefaultLoadAttempted)
  {
    mStartupDefaultLoadAttempted = true;

    const bool anyAmpSlotPath = std::any_of(
      mAmpNAMPaths.begin(), mAmpNAMPaths.end(), [](const WDL_String& path) { return path.GetLength() > 0; });
    const bool hasExistingPaths =
      anyAmpSlotPath || (mNAMPath.GetLength() > 0) || (mStompNAMPath.GetLength() > 0) || (mIRPath.GetLength() > 0) ||
      (mIRPathRight.GetLength() > 0);

    if (!hasExistingPaths)
    {
      auto existsNoThrow = [](const std::filesystem::path& path) {
        std::error_code ec;
        const bool exists = std::filesystem::exists(path, ec);
        return exists && !ec;
      };
      auto makeAbsoluteNoThrow = [](const std::filesystem::path& path) {
        std::error_code ec;
        const std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
        return ec ? path : absolutePath;
      };
      auto setWdlPath = [](WDL_String& target, const std::filesystem::path& path) { target.Set(path.string().c_str()); };
      auto hasAllDefaultFiles = [&](const std::filesystem::path& baseDir) {
        return existsNoThrow(baseDir / "Amp1.nam") && existsNoThrow(baseDir / "Amp2.nam") &&
               existsNoThrow(baseDir / "Amp3.nam") && existsNoThrow(baseDir / "Boost1.nam") &&
               existsNoThrow(baseDir / "Cab1.wav");
      };

      std::vector<std::filesystem::path> candidateDirs = {
        "NeuralAmpModeler/resources/tmpLoad", "resources/tmpLoad", "tmpLoad"
      };
      {
        WDL_String hostPath;
        HostPath(hostPath, GetBundleID());
        if (hostPath.GetLength() > 0)
        {
          const std::filesystem::path hostDir(hostPath.Get());
          candidateDirs.push_back(hostDir / "tmpLoad");
          candidateDirs.push_back(hostDir / "resources" / "tmpLoad");

          std::filesystem::path cursor = hostDir;
          for (int depth = 0; depth < 10; ++depth)
          {
            candidateDirs.push_back(cursor / "NeuralAmpModeler" / "resources" / "tmpLoad");
            candidateDirs.push_back(cursor / "resources" / "tmpLoad");
            if (!cursor.has_parent_path())
              break;
            cursor = cursor.parent_path();
          }
        }
      }
      std::filesystem::path defaultsDir;
      for (const auto& candidateDir : candidateDirs)
      {
        if (hasAllDefaultFiles(candidateDir))
        {
          defaultsDir = makeAbsoluteNoThrow(candidateDir);
          break;
        }
      }

      if (!defaultsDir.empty())
      {
        for (int slotIndex = 0; slotIndex < static_cast<int>(mAmpNAMPaths.size()); ++slotIndex)
        {
          WDL_String slotModelPath;
          setWdlPath(slotModelPath, defaultsDir / kAmpSlotDefaultModelFileNames[slotIndex]);
          _SetAmpSlotFixedModelPath(slotIndex, slotModelPath);
          _SetAmpSlotModelPath(slotIndex, slotModelPath);
        }
        setWdlPath(mStompNAMPath, defaultsDir / "Boost1.nam");
        setWdlPath(mIRPath, defaultsDir / "Cab1.wav");

        for (int slotIndex = 0; slotIndex < static_cast<int>(mAmpNAMPaths.size()); ++slotIndex)
        {
          if (mAmpNAMPaths[slotIndex].GetLength())
          {
            _RequestModelLoadForSlot(mAmpNAMPaths[slotIndex], slotIndex, _GetAmpModelCtrlTagForSlot(slotIndex));
            mAmpSlotStates[slotIndex].modelToggle = 1.0;
            mAmpSlotStates[slotIndex].modelToggleTouched = true;
          }
        }
        _ApplyAmpSlotState(mAmpSelectorIndex);
        if (mStompModel == nullptr && mStagedStompModel == nullptr)
          _StageStompModel(mStompNAMPath);
        if (mIR == nullptr && mStagedIR == nullptr)
          _StageIRLeft(mIRPath);
      }
    }
  }
#endif

  _ApplyInputStereoAutoDefaultIfNeeded();

  if (!mDefaultPresetCapturedFromStartup && !mStateRestoredFromChunk)
  {
    mTopNavBypassed[static_cast<size_t>(TopNavSection::Tuner)] = !GetParam(kTunerActive)->Bool();
    for (int slotIndex = 0; slotIndex < static_cast<int>(mAmpNAMPaths.size()); ++slotIndex)
    {
      if (!mAmpNAMPaths[slotIndex].GetLength())
        continue;
      mAmpSlotStates[slotIndex].modelToggle = 1.0;
      mAmpSlotStates[slotIndex].modelToggleTouched = true;
    }
    _ApplyAmpSlotState(mAmpSelectorIndex);

    IByteChunk startupDefaultPresetChunk;
    if (SerializeState(startupDefaultPresetChunk))
    {
      mDefaultPresetStateChunk.Clear();
      mDefaultPresetStateChunk.PutChunk(&startupDefaultPresetChunk);
      mHasDefaultPresetState = true;
      mDefaultPresetCapturedFromStartup = true;
      mDefaultPresetActive = true;
    }
  }

  // Tail is because the HPF DC blocker has a decay.
  // 10 cycles should be enough to pass the VST3 tests checking tail behavior.
  // I'm ignoring the model & IR, but it's not the end of the world.
  const int tailCycles = 10;
  SetTailSize(tailCycles * (int)(sampleRate / kDCBlockerFrequency));
  const double meterSenderSampleRate = (sampleRate > 0.0) ? sampleRate : DEFAULT_SAMPLE_RATE;
  mInputSender.Reset(meterSenderSampleRate);
  mOutputSender.Reset(meterSenderSampleRate);
  // If there is a model or IR loaded, they need to be checked for resampling.
  _ResetModelAndIR(sampleRate, GetBlockSize());
  mTransposeShifter.Reset(sampleRate, maxBlockSize);
  mTransposeShifterRight.Reset(sampleRate, maxBlockSize);
  for (int slotIndex = 0; slotIndex < static_cast<int>(mToneStacks.size()); ++slotIndex)
  {
    if (mToneStacks[slotIndex] != nullptr)
      mToneStacks[slotIndex]->Reset(sampleRate, maxBlockSize);
    _ApplyAmpSlotStateToToneStack(slotIndex);
  }
  // Pre-size internal buffers outside ProcessBlock() to avoid callback-time growth.
  const size_t preparedFrames = std::max<size_t>(static_cast<size_t>(std::max(1, maxBlockSize)), kMinInternalPreparedFrames);
  _PrepareBuffers(kNumChannelsInternal, preparedFrames, true);
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
  mFXDelaySmoothedDucker = std::clamp(GetParam(kFXDelayDucker)->Value() * 0.01, 0.0, 1.0);
  mFXDelayDuckerEnvelope = 0.0;
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
    mFXReverbEarlyTapSamples[1][i] = mFXReverbEarlyTapSamples[0][i];
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
      const size_t baseDelaySamples =
        std::max<size_t>(1, static_cast<size_t>(std::llround(kFXReverbCombBaseDelay[i] * reverbDelayScale)));
      const size_t stereoOffsetSamples =
        (c == 0) ? 0 : static_cast<size_t>(std::max(1LL, static_cast<long long>(std::llround(kFXReverbCombStereoOffset[i] * reverbDelayScale))));
      const size_t delaySamples = baseDelaySamples + stereoOffsetSamples;
      mFXReverbCombDelaySamples[c][i] = delaySamples;
      const size_t maxDelaySamples = std::max<size_t>(
        delaySamples + 2, static_cast<size_t>(std::ceil(static_cast<double>(delaySamples) * kFXReverbCombMaxSizeScale)));
      mFXReverbCombBuffer[c][i].assign(maxDelaySamples + 8, 0.0f);
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
  constexpr double kInitRoomEarlyLevel = 1.10;
  mFXReverbSmoothedEarlyLevel = kInitRoomEarlyLevel;
  mFXReverbSmoothedEarlyToneHz = 1700.0 + mFXReverbSmoothedTone * 3600.0;
  const double maxReverbCutHz = std::max(40.0, 0.45 * sampleRate);
  mFXReverbSmoothedLowCutHz = std::clamp(GetParam(kFXReverbLowCutHz)->Value(), 20.0, maxReverbCutHz);
  mFXReverbSmoothedHighCutHz =
    std::clamp(std::max(GetParam(kFXReverbHighCutHz)->Value(), mFXReverbSmoothedLowCutHz + 20.0), 20.0, maxReverbCutHz);
  mFXReverbLowCutLPState.fill(0.0);
  mFXReverbHighCutLPState.fill(0.0);
  mFXReverbStereoDecorrelatorState.fill(0.0);
  mFXReverbWasActive = false;
  mEffectiveMonoCollapseActive = false;
  mEffectiveMonoCandidateSamples = 0;
  mEffectiveStereoCandidateSamples = 0;
  mStereoSideBypassActive.fill(false);
  mStereoSideSilentCandidateSamples.fill(0);
  mStereoSideActiveCandidateSamples.fill(0);
  mStereoSideResumeDeClickSamplesRemaining.fill(0);
  mStereoSideResumePrevSample.fill(0.0);
  _UpdateLatency();
}

void NeuralAmpModeler::OnIdle()
{
  _ApplyInputStereoAutoDefaultIfNeeded();
  _RefreshModelCapabilityIndicators();

  if (auto* pGraphics = GetUI())
  {
    if (auto* pTempoControl = pGraphics->GetControlWithParamIdx(kDelayManualTempoBPM))
    {
      const bool syncOn = (GetParam(kDelayTempoSource)->Int() == 0);
      pTempoControl->SetDisabled(syncOn);

      double displayedTempoBPM = GetParam(kDelayManualTempoBPM)->Value();
      if (syncOn && mDelayHostTempoValid.load(std::memory_order_relaxed))
      {
        displayedTempoBPM =
          std::clamp(mDelayTempoBPM.load(std::memory_order_relaxed), kDelayManualTempoMinBPM, kDelayManualTempoMaxBPM);
      }

      const double displayedTempoNormalized = GetParam(kDelayManualTempoBPM)->ToNormalized(displayedTempoBPM);
      pTempoControl->SetValueFromDelegate(displayedTempoNormalized, 0);
    }
  }

  if (mStandalonePresetNameEntryReopenPending)
  {
    bool reopened = false;
    if (auto* pGraphics = GetUI())
    {
      auto* pPresetLabelControl = pGraphics->GetControlWithTag(kCtrlTagPresetLabel);
      auto* pProxyControl = pGraphics->GetControlWithTag(kCtrlTagStandalonePresetNameEntryProxy);
      if (pPresetLabelControl != nullptr && pProxyControl != nullptr)
      {
        const IText entryText = MakePresetNameEntryText(pPresetLabelControl->GetText());
        pGraphics->CreateTextEntry(
          *pProxyControl, entryText, pPresetLabelControl->GetRECT(),
          mStandalonePresetNameEntryPendingText.Get());
        reopened = true;
      }
    }
    if (reopened)
    {
      mStandalonePresetNameEntryReopenPending = false;
      mStandalonePresetNameEntryPendingText.Set("");
    }
  }

  if (mDefaultPresetPostLoadSyncPending && mDefaultPresetActive)
  {
    bool keepSyncPending = false;
    const int activeSlot = std::clamp(mAmpSelectorIndex, 0, static_cast<int>(mAmpNAMPaths.size()) - 1);
    const auto& activeSlotPath = mAmpNAMPaths[activeSlot];
    if (activeSlotPath.GetLength() > 0)
    {
      if (!GetParam(kModelToggle)->Bool())
      {
        GetParam(kModelToggle)->Set(1.0);
        SendParameterValueFromDelegate(kModelToggle, GetParam(kModelToggle)->GetNormalized(), true);
        mAmpSlotStates[activeSlot].modelToggle = 1.0;
        mAmpSlotStates[activeSlot].modelToggleTouched = true;
        keepSyncPending = true;
      }

      const int activeSlotState = mAmpSlotModelState[activeSlot].load(std::memory_order_acquire);
      if (mModel == nullptr)
      {
        if (activeSlotState == kAmpSlotModelStateEmpty)
        {
          _RequestModelLoadForSlot(activeSlotPath, activeSlot, _GetAmpModelCtrlTagForSlot(activeSlot));
          mPendingAmpSlotSwitch.store(activeSlot, std::memory_order_release);
          keepSyncPending = true;
        }
        else if (activeSlotState == kAmpSlotModelStateLoading)
        {
          keepSyncPending = true;
        }
        else if (activeSlotState == kAmpSlotModelStateReady)
        {
          mPendingAmpSlotSwitch.store(activeSlot, std::memory_order_release);
          keepSyncPending = true;
        }
      }
    }

    if (mStompNAMPath.GetLength() > 0)
    {
      mShouldRemoveStompModel = false;
      if (mStompModel == nullptr && mStagedStompModel == nullptr)
        _StageStompModel(mStompNAMPath);
      if (mStompModel == nullptr && mStagedStompModel != nullptr)
        keepSyncPending = true;
    }

    if (mIRPath.GetLength() > 0)
    {
      mShouldRemoveIRLeft = false;
      if (mIR == nullptr && mStagedIR == nullptr)
        _StageIRLeft(mIRPath);
      if (mIR == nullptr && mStagedIR != nullptr)
        keepSyncPending = true;
    }
    else
    {
      for (const auto& ampPath : mAmpNAMPaths)
      {
        if (ampPath.GetLength() <= 0)
          continue;
        std::error_code ec;
        const std::filesystem::path ampFilePath = std::filesystem::absolute(std::filesystem::path(ampPath.Get()), ec);
        const std::filesystem::path basePath = ec ? std::filesystem::path(ampPath.Get()) : ampFilePath;
        const std::filesystem::path cabPath = basePath.parent_path() / "Cab1.wav";
        ec.clear();
        if (!std::filesystem::exists(cabPath, ec) || ec)
          continue;

        mIRPath.Set(cabPath.string().c_str());
        mIRPathRight.Set("");
        mShouldRemoveIRLeft = false;
        _StageIRLeft(mIRPath);
        keepSyncPending = true;
        break;
      }
    }

    if (mIRPathRight.GetLength() > 0)
    {
      mShouldRemoveIRRight = false;
      if (mIRRight == nullptr && mStagedIRRight == nullptr)
        _StageIRRight(mIRPathRight);
      if (mIRRight == nullptr && mStagedIRRight != nullptr)
        keepSyncPending = true;
    }

    mDefaultPresetPostLoadSyncPending = keepSyncPending;
  }

  if (GetUI() != nullptr)
  {
    auto syncIRPicker = [this](const int ctrlTag, const int loadedMsgTag, const int clearMsgTag, const WDL_String& currentPath,
                               WDL_String& lastSentPath) {
      if (std::strcmp(currentPath.Get(), lastSentPath.Get()) == 0)
        return;

      if (currentPath.GetLength() > 0)
        SendControlMsgFromDelegate(ctrlTag, loadedMsgTag, currentPath.GetLength(), currentPath.Get());
      else
        SendControlMsgFromDelegate(ctrlTag, clearMsgTag, 0, nullptr);

      lastSentPath.Set(currentPath.Get());
    };

    syncIRPicker(kCtrlTagIRFileBrowserLeft, kMsgTagLoadedIRLeft, kMsgTagClearIRLeft, mIRPath, mLastSentIRPath);
    syncIRPicker(kCtrlTagIRFileBrowserRight, kMsgTagLoadedIRRight, kMsgTagClearIRRight, mIRPathRight, mLastSentIRPathRight);
  }

  mInputSender.TransmitData(*this);
  mOutputSender.TransmitData(*this);

  bool refreshModelCapabilityIndicators = false;
  for (int slotIndex = 0; slotIndex < static_cast<int>(mAmpNAMPaths.size()); ++slotIndex)
  {
    const int event = mSlotLoadUIEvent[slotIndex].exchange(kSlotLoadUIEventNone, std::memory_order_relaxed);
    if (event == kSlotLoadUIEventLoaded)
    {
      refreshModelCapabilityIndicators = true;
      const int ctrlTag = _GetAmpModelCtrlTagForSlot(slotIndex);
      const auto& slotPath = mAmpNAMPaths[slotIndex];
      if (slotPath.GetLength())
        SendControlMsgFromDelegate(ctrlTag, kMsgTagLoadedModel, slotPath.GetLength(), slotPath.Get());
    }
    else if (event == kSlotLoadUIEventFailed)
    {
      refreshModelCapabilityIndicators = true;
      _ClearAmpSlotCapabilityState(slotIndex);
      mAmpSlotModelState[slotIndex].store(kAmpSlotModelStateFailed, std::memory_order_relaxed);
      const int ctrlTag = _GetAmpModelCtrlTagForSlot(slotIndex);
      SendControlMsgFromDelegate(ctrlTag, kMsgTagLoadFailed);
      mAmpSlotStates[slotIndex].modelToggle = 0.0;
      mAmpSlotStates[slotIndex].modelToggleTouched = true;
      if (slotIndex == mAmpSelectorIndex && GetParam(kModelToggle)->Bool())
      {
        GetParam(kModelToggle)->Set(0.0);
        SendParameterValueFromDelegate(kModelToggle, GetParam(kModelToggle)->GetNormalized(), true);
      }
    }
  }
  if (refreshModelCapabilityIndicators)
    _RefreshModelCapabilityIndicators();

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
      static_cast<NAMSettingsPageControl*>(pGraphics->GetControlWithTag(kCtrlTagSettingsBox))->ClearModelInfo();
      _RefreshOutputModeControlSupport();
      const int activeSlot = std::clamp(mAmpSelectorIndex, 0, static_cast<int>(mAmpNAMPaths.size()) - 1);
      const int activeSlotState = mAmpSlotModelState[activeSlot].load(std::memory_order_relaxed);
      const bool shouldForceToggleOff =
        (activeSlotState != kAmpSlotModelStateLoading)
        && (mAmpNAMPaths[activeSlot].GetLength() == 0 || activeSlotState == kAmpSlotModelStateFailed);
      if (shouldForceToggleOff && GetParam(kModelToggle)->Bool())
      {
        GetParam(kModelToggle)->Set(0.0);
        SendParameterValueFromDelegate(kModelToggle, GetParam(kModelToggle)->GetNormalized(), true);
      }
      mModelCleared = false;
    }
  }
}

void NeuralAmpModeler::_ApplyInputStereoAutoDefaultIfNeeded()
{
#ifdef APP_API
  return;
#else
  if (mInputStereoAutoDefaultApplied || mStateRestoredFromChunk)
    return;

  if (NInChansConnected() <= 1)
    return;

  if (auto* inputStereoParam = GetParam(kInputStereoMode); inputStereoParam != nullptr)
  {
    if (!inputStereoParam->Bool())
    {
      inputStereoParam->Set(1.0);
      SendParameterValueFromDelegate(kInputStereoMode, inputStereoParam->GetNormalized(), true);
    }
    mInputStereoAutoDefaultApplied = true;
  }
#endif
}

bool NeuralAmpModeler::SerializeState(IByteChunk& chunk) const
{
  constexpr int32_t kStateSchemaVersion = 3;

  // If this isn't here when unserializing, then we know we're dealing with something before v0.8.0.
  WDL_String header("###NeuralAmpModeler###"); // Don't change this!
  chunk.PutStr(header.Get());
  // Plugin version, so we can load legacy serialized states in the future!
  WDL_String version(PLUG_VERSION_STR);
  chunk.PutStr(version.Get());

  chunk.Put(&kStateSchemaVersion);

  const int32_t activeSlot = static_cast<int32_t>(std::clamp(mAmpSelectorIndex, 0, static_cast<int>(mAmpNAMPaths.size()) - 1));
  chunk.Put(&activeSlot);

  for (const auto& slotPath : mAmpNAMPaths)
    chunk.PutStr(slotPath.Get());

  chunk.PutStr(mStompNAMPath.Get());
  chunk.PutStr(mIRPath.Get());
  chunk.PutStr(mIRPathRight.Get());

  // Presets should not force which UI section is visible.
  const int32_t topNavActiveSection = static_cast<int32_t>(TopNavSection::Amp);
  chunk.Put(&topNavActiveSection);

  for (const bool bypassed : mTopNavBypassed)
  {
    const int32_t bypassedInt = bypassed ? 1 : 0;
    chunk.Put(&bypassedInt);
  }

  for (const auto& slotState : mAmpSlotStates)
  {
    const int32_t modelToggleTouched = slotState.modelToggleTouched ? 1 : 0;
    chunk.Put(&slotState.modelToggle);
    chunk.Put(&modelToggleTouched);
    chunk.Put(&slotState.toneStackActive);
    chunk.Put(&slotState.preModelGain);
    chunk.Put(&slotState.bass);
    chunk.Put(&slotState.mid);
    chunk.Put(&slotState.treble);
    chunk.Put(&slotState.presence);
    chunk.Put(&slotState.depth);
    chunk.Put(&slotState.master);
  }

  return SerializeParams(chunk);
}

int NeuralAmpModeler::UnserializeState(const IByteChunk& chunk, int startPos)
{
  constexpr int32_t kStateSchemaVersion = 3;
  constexpr int32_t kLegacyStateSchemaVersion = 2;
  auto markStateRestored = [this]() {
    mStateRestoredFromChunk = true;
    mInputStereoAutoDefaultApplied = true;
    if (!mLoadingDefaultPreset)
      mDefaultPresetActive = false;
  };
  auto syncAllParamControls = [this]() {
    if (GetUI() == nullptr)
      return;
    for (int paramIdx = 0; paramIdx < kNumParams; ++paramIdx)
    {
      if (auto* pParam = GetParam(paramIdx))
        SendParameterValueFromDelegate(paramIdx, pParam->GetNormalized(), true);
    }
  };

  // Look for the expected header. If it's there, then we'll know what to do.
  WDL_String header;
  int pos = startPos;
  pos = chunk.GetStr(header, pos);

  const char* kExpectedHeader = "###NeuralAmpModeler###";
  if (strcmp(header.Get(), kExpectedHeader) != 0)
  {
    const int restoredPos = _UnserializeStateWithUnknownVersion(chunk, startPos);
    if (restoredPos > startPos)
    {
      markStateRestored();
      syncAllParamControls();
    }
    return restoredPos;
  }

  WDL_String version;
  const int versionPos = chunk.GetStr(version, pos);
  if (versionPos < 0)
    return startPos;

  // Current chunk schema (v3): explicit slot paths/states + full parameter payload.
  int32_t schemaVersion = 0;
  const int schemaPos = chunk.Get(&schemaVersion, versionPos);
  if (schemaPos >= 0 && (schemaVersion == kStateSchemaVersion || schemaVersion == kLegacyStateSchemaVersion))
  {
    int statePos = schemaPos;

    int32_t activeSlot = static_cast<int32_t>(mAmpSelectorIndex);
    statePos = chunk.Get(&activeSlot, statePos);
    if (statePos < 0)
      return startPos;

    std::array<WDL_String, 3> ampPaths;
    for (auto& slotPath : ampPaths)
    {
      statePos = chunk.GetStr(slotPath, statePos);
      if (statePos < 0)
        return startPos;
    }

    WDL_String stompPath;
    WDL_String irLeftPath;
    WDL_String irRightPath;
    statePos = chunk.GetStr(stompPath, statePos);
    if (statePos < 0)
      return startPos;
    statePos = chunk.GetStr(irLeftPath, statePos);
    if (statePos < 0)
      return startPos;
    statePos = chunk.GetStr(irRightPath, statePos);
    if (statePos < 0)
      return startPos;

    int32_t topNavActiveSection = static_cast<int32_t>(TopNavSection::Amp);
    statePos = chunk.Get(&topNavActiveSection, statePos);
    if (statePos < 0)
      return startPos;

    std::array<bool, static_cast<size_t>(TopNavSection::Count)> bypassed = {};
    const size_t storedBypassCount =
      (schemaVersion == kLegacyStateSchemaVersion) ? (static_cast<size_t>(TopNavSection::Tuner) + 1) : bypassed.size();
    for (size_t i = 0; i < storedBypassCount; ++i)
    {
      int32_t bypassInt = 0;
      statePos = chunk.Get(&bypassInt, statePos);
      if (statePos < 0)
        return startPos;
      bypassed[i] = (bypassInt != 0);
    }
    if (schemaVersion == kLegacyStateSchemaVersion)
      bypassed[static_cast<size_t>(TopNavSection::Eq)] = bypassed[static_cast<size_t>(TopNavSection::Fx)];

    std::array<AmpSlotState, 3> slotStates = {};
    for (auto& slotState : slotStates)
    {
      int32_t modelToggleTouched = 0;
      statePos = chunk.Get(&slotState.modelToggle, statePos);
      if (statePos < 0)
        return startPos;
      statePos = chunk.Get(&modelToggleTouched, statePos);
      if (statePos < 0)
        return startPos;
      statePos = chunk.Get(&slotState.toneStackActive, statePos);
      if (statePos < 0)
        return startPos;
      statePos = chunk.Get(&slotState.preModelGain, statePos);
      if (statePos < 0)
        return startPos;
      statePos = chunk.Get(&slotState.bass, statePos);
      if (statePos < 0)
        return startPos;
      statePos = chunk.Get(&slotState.mid, statePos);
      if (statePos < 0)
        return startPos;
      statePos = chunk.Get(&slotState.treble, statePos);
      if (statePos < 0)
        return startPos;
      statePos = chunk.Get(&slotState.presence, statePos);
      if (statePos < 0)
        return startPos;
      statePos = chunk.Get(&slotState.depth, statePos);
      if (statePos < 0)
        return startPos;
      statePos = chunk.Get(&slotState.master, statePos);
      if (statePos < 0)
        return startPos;
      slotState.modelToggleTouched = (modelToggleTouched != 0);
    }

    const int paramsPos = UnserializeParams(chunk, statePos);
    if (paramsPos < 0)
    {
      const int restoredPos = _UnserializeStateWithKnownVersion(chunk, pos);
      if (restoredPos > startPos)
      {
        markStateRestored();
        syncAllParamControls();
      }
      return restoredPos;
    }

    mAmpSelectorIndex = std::clamp(static_cast<int>(activeSlot), 0, static_cast<int>(mAmpNAMPaths.size()) - 1);
    mAmpNAMPaths = ampPaths;
    mNAMPath = mAmpNAMPaths[mAmpSelectorIndex];
    mStompNAMPath = stompPath;
    mIRPath = irLeftPath;
    mIRPathRight = irRightPath;
    mAmpSlotStates = slotStates;

    (void) topNavActiveSection;
    mTopNavBypassed = bypassed;

    for (int slotIndex = 0; slotIndex < static_cast<int>(mToneStacks.size()); ++slotIndex)
      _ApplyAmpSlotStateToToneStack(slotIndex);

    for (int slotIndex = 0; slotIndex < static_cast<int>(mAmpNAMPaths.size()); ++slotIndex)
    {
      const auto& slotPath = mAmpNAMPaths[slotIndex];
      const WDL_String effectiveSlotPath = _ResolveAmpSlotModelPathForMode(slotIndex, slotPath);
      if (effectiveSlotPath.GetLength())
      {
        _RequestModelLoadForSlot(slotPath, slotIndex, _GetAmpModelCtrlTagForSlot(slotIndex));
      }
      else
      {
        mAmpSlotModelState[slotIndex].store(kAmpSlotModelStateEmpty, std::memory_order_relaxed);
        mSlotLoadRequestId[slotIndex].fetch_add(1, std::memory_order_relaxed);
        mShouldRemoveModelSlot[slotIndex].store(true, std::memory_order_relaxed);
      }
    }
    mPendingAmpSlotSwitch.store(mAmpSelectorIndex, std::memory_order_release);

    if (mStompNAMPath.GetLength())
      _StageStompModel(mStompNAMPath);
    else
      mShouldRemoveStompModel = true;

    if (mIRPath.GetLength())
      _StageIRLeft(mIRPath);
    else
      mShouldRemoveIRLeft = true;

    if (mIRPathRight.GetLength())
      _StageIRRight(mIRPathRight);
    else
      mShouldRemoveIRRight = true;

    _ApplyAmpSlotState(mAmpSelectorIndex);
    _SyncTunerParamToTopNav();
    _RefreshTopNavControls();
    markStateRestored();
    syncAllParamControls();

    return paramsPos;
  }

  // Legacy headered chunk from this fork: version + single NAM path + IR L/R + full parameter payload.
  WDL_String legacyNAMPath;
  WDL_String legacyIRPath;
  WDL_String legacyIRRightPath;
  int legacyPos = versionPos;
  legacyPos = chunk.GetStr(legacyNAMPath, legacyPos);
  if (legacyPos >= 0)
    legacyPos = chunk.GetStr(legacyIRPath, legacyPos);
  if (legacyPos >= 0)
    legacyPos = chunk.GetStr(legacyIRRightPath, legacyPos);

  if (legacyPos >= 0)
  {
    const int paramsPos = UnserializeParams(chunk, legacyPos);
    if (paramsPos < 0)
    {
      const int restoredPos = _UnserializeStateWithKnownVersion(chunk, pos);
      if (restoredPos > startPos)
      {
        markStateRestored();
        syncAllParamControls();
      }
      return restoredPos;
    }
    mNAMPath = legacyNAMPath;
    mIRPath = legacyIRPath;
    mIRPathRight = legacyIRRightPath;
    _SetAmpSlotModelPath(mAmpSelectorIndex, mNAMPath);

    const WDL_String effectiveLegacyPath = _ResolveAmpSlotModelPathForMode(mAmpSelectorIndex, mNAMPath);
    if (effectiveLegacyPath.GetLength())
      _RequestModelLoadForSlot(mNAMPath, mAmpSelectorIndex, _GetAmpModelCtrlTagForSlot(mAmpSelectorIndex));
    else
    {
      mAmpSlotModelState[mAmpSelectorIndex].store(kAmpSlotModelStateEmpty, std::memory_order_relaxed);
      mSlotLoadRequestId[mAmpSelectorIndex].fetch_add(1, std::memory_order_relaxed);
      mShouldRemoveModelSlot[mAmpSelectorIndex].store(true, std::memory_order_relaxed);
    }
    mPendingAmpSlotSwitch.store(mAmpSelectorIndex, std::memory_order_release);

    if (mIRPath.GetLength())
      _StageIRLeft(mIRPath);
    else
      mShouldRemoveIRLeft = true;

    if (mIRPathRight.GetLength())
      _StageIRRight(mIRPathRight);
    else
      mShouldRemoveIRRight = true;

    _ApplyAmpSlotState(mAmpSelectorIndex);
    _SyncTunerParamToTopNav();
    _RefreshTopNavControls();
    markStateRestored();
    syncAllParamControls();
    return paramsPos;
  }

  const int restoredPos = _UnserializeStateWithKnownVersion(chunk, pos);
  if (restoredPos > startPos)
  {
    markStateRestored();
    syncAllParamControls();
  }
  return restoredPos;
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
  const int activeSlotModelState = mAmpSlotModelState[mAmpSelectorIndex].load(std::memory_order_relaxed);
  if (mAmpNAMPaths[mAmpSelectorIndex].GetLength() && activeSlotModelState == kAmpSlotModelStateFailed)
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
    mLastSentIRPath.Set(mIRPath.Get());
    if (mIR == nullptr && mStagedIR == nullptr)
      SendControlMsgFromDelegate(kCtrlTagIRFileBrowserLeft, kMsgTagLoadFailed);
  }
  else
  {
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowserLeft, kMsgTagClearIRLeft, 0, nullptr);
    mLastSentIRPath.Set("");
  }
  if (mIRPathRight.GetLength())
  {
    SendControlMsgFromDelegate(
      kCtrlTagIRFileBrowserRight, kMsgTagLoadedIRRight, mIRPathRight.GetLength(), mIRPathRight.Get());
    mLastSentIRPathRight.Set(mIRPathRight.Get());
    if (mIRRight == nullptr && mStagedIRRight == nullptr)
      SendControlMsgFromDelegate(kCtrlTagIRFileBrowserRight, kMsgTagLoadFailed);
  }
  else
  {
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowserRight, kMsgTagClearIRRight, 0, nullptr);
    mLastSentIRPathRight.Set("");
  }

  if (mModel != nullptr)
  {
    _UpdateControlsFromModel();
  }
  _RefreshOutputModeControlSupport();
  _RefreshModelCapabilityIndicators();

  // If no model is available, force model toggle to OFF.
  if (mModel == nullptr && activeSlotModelState != kAmpSlotModelStateLoading && GetParam(kModelToggle)->Bool())
  {
    GetParam(kModelToggle)->Set(0.0);
    SendParameterValueFromDelegate(kModelToggle, GetParam(kModelToggle)->GetNormalized(), true);
  }

  if (GetParam(kTunerActive)->Bool())
  {
    mTopNavBypassed[static_cast<size_t>(TopNavSection::Tuner)] = false;
  }

  _RefreshTopNavControls();
  _RefreshStandalonePresetList();
  _UpdatePresetLabel();

  if (auto* pGraphics = GetUI())
  {
    for (int slotIndex = 0; slotIndex < static_cast<int>(mAmpNAMPaths.size()); ++slotIndex)
    {
      if (auto* pSlotModelPicker = pGraphics->GetControlWithTag(_GetAmpModelCtrlTagForSlot(slotIndex)))
        pSlotModelPicker->SetDisabled(!_CanEditAmpSlotModel(slotIndex));
    }
  }
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
    case kToneBass:
    case kToneMid:
    case kToneTreble:
    case kTonePresence:
    case kToneDepth: _ApplyCurrentAmpParamsToActiveToneStack(); break;
    default: break;
  }

  if (_IsAmpSlotManagedParam(paramIdx))
  {
    _CaptureAmpSlotState(mAmpSelectorIndex);
    if (paramIdx == kModelToggle)
      mAmpSlotStates[mAmpSelectorIndex].modelToggleTouched = true;
  }
}

void NeuralAmpModeler::OnParamChangeUI(int paramIdx, EParamSource source)
{
  if (source == kUI)
    _MarkStandalonePresetDirty();

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
        if (source == kUI && active && (mStompModel == nullptr) && (mStagedStompModel == nullptr))
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
        break;
      }
      case kFXDelayTimeMode:
      {
        if (auto* pDelayTimeControl = pGraphics->GetControlWithParamIdx(kFXDelayTimeMs))
          pDelayTimeControl->SetDirty(false);
        if (auto* pDelayReadout = pGraphics->GetControlWithTag(kCtrlTagFXDelayReadout))
          pDelayReadout->SetDirty(false);
        break;
      }
      case kFXDelayTimeMs:
      case kFXDelayMix:
      case kFXDelayFeedback:
      {
        if (auto* pDelayReadout = pGraphics->GetControlWithTag(kCtrlTagFXDelayReadout))
          pDelayReadout->SetDirty(false);
        break;
      }
      case kDelayTempoSource:
      {
        const bool syncOn = (GetParam(kDelayTempoSource)->Int() == 0);
        if (auto* pTempoControl = pGraphics->GetControlWithParamIdx(kDelayManualTempoBPM))
          pTempoControl->SetDisabled(syncOn);
        break;
      }
      case kFXReverbActive:
      {
        if (auto* pFXReverbOnLED = pGraphics->GetControlWithTag(kCtrlTagFXReverbOnLED))
          pFXReverbOnLED->SetValueFromDelegate(active ? 1.0 : 0.0, 0);
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
      case kInputStereoMode:
        if (active)
        {
          // Promote mono-loaded assets into stereo-core instances if needed.
          const int activeSlot = std::clamp(mAmpSelectorIndex, 0, static_cast<int>(mAmpNAMPaths.size()) - 1);
          const WDL_String& activeSlotPath = mAmpNAMPaths[activeSlot];
          if (mModelRight == nullptr && activeSlotPath.GetLength())
            _RequestModelLoadForSlot(activeSlotPath, activeSlot, _GetAmpModelCtrlTagForSlot(activeSlot));
          if (mStompModelRight == nullptr && mStompNAMPath.GetLength())
            _StageStompModel(mStompNAMPath);
          if (mIRChannel2 == nullptr && mIRPath.GetLength())
            _StageIRLeft(mIRPath);
          if (mIRRightChannel2 == nullptr && mIRPathRight.GetLength())
            _StageIRRight(mIRPathRight);
        }
        break;
      case kModelToggle:
        if (mApplyingAmpSlotState)
          break;
        if (source == kUI && active && (mModel == nullptr)
            && (mAmpSlotModelState[mAmpSelectorIndex].load(std::memory_order_relaxed) != kAmpSlotModelStateLoading))
        {
          const int activeSlot = std::clamp(mAmpSelectorIndex, 0, static_cast<int>(mAmpNAMPaths.size()) - 1);
          const WDL_String& activeSlotPath = mAmpNAMPaths[activeSlot];
          if (activeSlotPath.GetLength())
          {
            const int slotCtrlTag = _GetAmpModelCtrlTagForSlot(activeSlot);
            _RequestModelLoadForSlot(activeSlotPath, activeSlot, slotCtrlTag);
            mPendingAmpSlotSwitch.store(activeSlot, std::memory_order_release);
            break;
          }

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
                _RequestModelLoadForSlot(chosenFileName, mAmpSelectorIndex, slotCtrlTag, true);
                _MarkStandalonePresetDirty();
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
      const int resolvedSlot = (slotIndex < 0 || slotIndex >= static_cast<int>(mAmpNAMPaths.size()))
                                 ? std::clamp(mAmpSelectorIndex, 0, static_cast<int>(mAmpNAMPaths.size()) - 1)
                                 : slotIndex;

      if (!_CanEditAmpSlotModel(resolvedSlot))
        return true;

      WDL_String emptyPath;
      emptyPath.Set("");
      _SetAmpSlotModelPath(resolvedSlot, emptyPath);
      mAmpSlotStates[resolvedSlot].modelToggle = 0.0;
      mAmpSlotStates[resolvedSlot].modelToggleTouched = true;
      mAmpSlotModelState[resolvedSlot].store(kAmpSlotModelStateEmpty, std::memory_order_relaxed);
      mSlotLoadUIEvent[resolvedSlot].store(kSlotLoadUIEventNone, std::memory_order_relaxed);
      mSlotLoadRequestId[resolvedSlot].fetch_add(1, std::memory_order_relaxed);
      mShouldRemoveModelSlot[resolvedSlot].store(true, std::memory_order_relaxed);
      _ClearAmpSlotCapabilityState(resolvedSlot);
      _RefreshModelCapabilityIndicators();
      _MarkStandalonePresetDirty();

      if (resolvedSlot == mAmpSelectorIndex)
      {
        mPendingAmpSlotSwitch.store(resolvedSlot, std::memory_order_release);
      }
      return true;
    }
    case kMsgTagClearStompModel:
      mShouldRemoveStompModel = true;
      _ClearStompCapabilityState();
      _RefreshModelCapabilityIndicators();
      _MarkStandalonePresetDirty();
      return true;
    case kMsgTagClearIRLeft:
      mShouldRemoveIRLeft = true;
      _MarkStandalonePresetDirty();
      return true;
    case kMsgTagClearIRRight:
      mShouldRemoveIRRight = true;
      _MarkStandalonePresetDirty();
      return true;
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

void NeuralAmpModeler::_UpdatePresetLabel()
{
  auto* pGraphics = GetUI();
  if (pGraphics == nullptr)
    return;

  auto* pText = dynamic_cast<ITextControl*>(pGraphics->GetControlWithTag(kCtrlTagPresetLabel));
  auto* pLabelButton = dynamic_cast<IVButtonControl*>(pGraphics->GetControlWithTag(kCtrlTagPresetLabel));
  if (pText == nullptr && pLabelButton == nullptr)
    return;

  WDL_String label;
  if (mStandalonePresetFilePath.GetLength() > 0)
  {
    std::filesystem::path presetPath(mStandalonePresetFilePath.Get());
    const std::string presetName = presetPath.stem().string();
    label.SetFormatted(256, "%s", presetName.c_str());
  }
  else if (mDefaultPresetActive)
  {
    label.Set("Default");
  }
  else if (!mStandalonePresetPaths.empty())
  {
    label.Set("Unsaved");
  }
  else
  {
    label.Set("No Presets");
  }
  if (mStandalonePresetDirty && std::strcmp(label.Get(), "No Presets") != 0)
    label.Append("*");

  if (pText != nullptr)
  {
    pText->SetStr(label.Get());
    pText->SetDirty(false);
  }
  if (pLabelButton != nullptr)
  {
    pLabelButton->SetLabelStr(label.Get());
    pLabelButton->SetDirty(false);
  }
}

void NeuralAmpModeler::_RefreshStandalonePresetList()
{
  mStandaloneUserPresetPaths.clear();
  mStandaloneFactoryPresetPaths.clear();
  mStandalonePresetPaths.clear();
  mStandalonePresetIndex = -1;

  const auto userPresetDir = GetStandaloneUserPresetDirectoryPath();
  const auto factoryPresetDir = GetStandaloneFactoryPresetDirectoryPath();
  if (userPresetDir.empty() || factoryPresetDir.empty())
    return;

  std::error_code ec;
  if (!std::filesystem::exists(userPresetDir, ec))
    std::filesystem::create_directories(userPresetDir, ec);
  if (ec)
    return;

  auto scanPresetDirectory = [&ec](const std::filesystem::path& directory, std::vector<WDL_String>& outPaths) {
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
    {
      if (ec)
        break;
      if (!entry.is_regular_file())
        continue;
      if (entry.path().extension() != ("." + std::string(kStandalonePresetExtension)))
        continue;

      WDL_String path;
      path.Set(entry.path().string().c_str());
      outPaths.push_back(path);
    }

    std::sort(
      outPaths.begin(),
      outPaths.end(),
      [](const WDL_String& a, const WDL_String& b) { return std::strcmp(a.Get(), b.Get()) < 0; });
  };

  scanPresetDirectory(userPresetDir, mStandaloneUserPresetPaths);

  std::vector<std::filesystem::path> factorySearchDirs;
  factorySearchDirs.push_back(factoryPresetDir);
  factorySearchDirs.push_back(std::filesystem::path("NeuralAmpModeler") / "resources" / "Presets" / "Factory");
  factorySearchDirs.push_back(std::filesystem::path("NeuralAmpModeler") / "resources" / "presets" / "factory");
  factorySearchDirs.push_back(std::filesystem::path("resources") / "Presets" / "Factory");
  factorySearchDirs.push_back(std::filesystem::path("resources") / "presets" / "factory");
  factorySearchDirs.push_back(std::filesystem::path("Presets") / "Factory");
  factorySearchDirs.push_back(std::filesystem::path("presets") / "factory");
  WDL_String hostPath;
  HostPath(hostPath, GetBundleID());
  if (hostPath.GetLength() > 0)
  {
    const std::filesystem::path hostDir(hostPath.Get());
    factorySearchDirs.push_back(hostDir / "Presets" / "Factory");
    factorySearchDirs.push_back(hostDir / "resources" / "Presets" / "Factory");
    factorySearchDirs.push_back(hostDir / "resources" / "presets" / "factory");
  }

  std::vector<std::filesystem::path> normalizedFactoryDirs;
  for (auto factoryDir : factorySearchDirs)
  {
    std::error_code dirEc;
    factoryDir = std::filesystem::absolute(factoryDir, dirEc);
    if (dirEc || !std::filesystem::exists(factoryDir, dirEc) || dirEc)
      continue;
    bool duplicate = false;
    for (const auto& existingDir : normalizedFactoryDirs)
    {
      if (existingDir == factoryDir)
      {
        duplicate = true;
        break;
      }
    }
    if (!duplicate)
      normalizedFactoryDirs.push_back(factoryDir);
  }

  for (const auto& factoryDir : normalizedFactoryDirs)
    scanPresetDirectory(factoryDir, mStandaloneFactoryPresetPaths);

  std::sort(
    mStandaloneFactoryPresetPaths.begin(),
    mStandaloneFactoryPresetPaths.end(),
    [](const WDL_String& a, const WDL_String& b) { return std::strcmp(a.Get(), b.Get()) < 0; });
  mStandaloneFactoryPresetPaths.erase(
    std::unique(
      mStandaloneFactoryPresetPaths.begin(),
      mStandaloneFactoryPresetPaths.end(),
      [](const WDL_String& a, const WDL_String& b) { return std::strcmp(a.Get(), b.Get()) == 0; }),
    mStandaloneFactoryPresetPaths.end());

  if (ec)
    return;

  for (const auto& path : mStandaloneUserPresetPaths)
    mStandalonePresetPaths.push_back(path);
  for (const auto& path : mStandaloneFactoryPresetPaths)
    mStandalonePresetPaths.push_back(path);

  if (mStandalonePresetFilePath.GetLength() > 0)
  {
    for (int i = 0; i < static_cast<int>(mStandalonePresetPaths.size()); ++i)
    {
      if (std::strcmp(mStandalonePresetPaths[i].Get(), mStandalonePresetFilePath.Get()) == 0)
      {
        mStandalonePresetIndex = i;
        break;
      }
    }
  }
}

bool NeuralAmpModeler::_LoadStandalonePresetFromFile(const WDL_String& filePath)
{
  std::filesystem::path path = EnsureStandalonePresetExtension(std::filesystem::path(filePath.Get()));
  std::error_code ec;
  path = std::filesystem::absolute(path, ec);
  if (ec)
    path = EnsureStandalonePresetExtension(std::filesystem::path(filePath.Get()));

  IByteChunk chunk;
  if (!LoadChunkFromFile(path, chunk))
    return false;

  const int unserializePos = UnserializeState(chunk, 0);
  if (unserializePos < 0)
    return false;

  mStandalonePresetFilePath.Set(path.string().c_str());
  mDefaultPresetActive = false;
  mDefaultPresetPostLoadSyncPending = false;
  _SetStandalonePresetDirty(false);
  _RefreshStandalonePresetList();
  _UpdatePresetLabel();
  return true;
}

bool NeuralAmpModeler::_LoadDefaultPreset()
{
  if (!mHasDefaultPresetState || mDefaultPresetStateChunk.Size() <= 0)
    return false;

  mLoadingDefaultPreset = true;
  const int unserializePos = UnserializeState(mDefaultPresetStateChunk, 0);
  mLoadingDefaultPreset = false;
  if (unserializePos < 0)
    return false;

#if defined(APP_API) && (NAM_STARTUP_TMPLOAD_DEFAULTS > 0)
  auto existsNoThrow = [](const std::filesystem::path& path) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    return exists && !ec;
  };
  auto makeAbsoluteNoThrow = [](const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
    return ec ? path : absolutePath;
  };
  auto setWdlPath = [](WDL_String& target, const std::filesystem::path& path) { target.Set(path.string().c_str()); };
  auto hasAllDefaultFiles = [&](const std::filesystem::path& baseDir) {
    return existsNoThrow(baseDir / "Amp1.nam") && existsNoThrow(baseDir / "Amp2.nam")
           && existsNoThrow(baseDir / "Amp3.nam") && existsNoThrow(baseDir / "Boost1.nam")
           && existsNoThrow(baseDir / "Cab1.wav");
  };

  std::vector<std::filesystem::path> candidateDirs = {"NeuralAmpModeler/resources/tmpLoad", "resources/tmpLoad", "tmpLoad"};
  WDL_String hostPath;
  HostPath(hostPath, GetBundleID());
  if (hostPath.GetLength() > 0)
  {
    const std::filesystem::path hostDir(hostPath.Get());
    candidateDirs.push_back(hostDir / "tmpLoad");
    candidateDirs.push_back(hostDir / "resources" / "tmpLoad");

    std::filesystem::path cursor = hostDir;
    for (int depth = 0; depth < 10; ++depth)
    {
      candidateDirs.push_back(cursor / "NeuralAmpModeler" / "resources" / "tmpLoad");
      candidateDirs.push_back(cursor / "resources" / "tmpLoad");
      if (!cursor.has_parent_path())
        break;
      cursor = cursor.parent_path();
    }
  }

  std::filesystem::path defaultsDir;
  for (const auto& candidateDir : candidateDirs)
  {
    if (hasAllDefaultFiles(candidateDir))
    {
      defaultsDir = makeAbsoluteNoThrow(candidateDir);
      break;
    }
  }

  if (!defaultsDir.empty())
  {
    for (int slotIndex = 0; slotIndex < static_cast<int>(mAmpNAMPaths.size()); ++slotIndex)
    {
      WDL_String slotModelPath;
      setWdlPath(slotModelPath, defaultsDir / kAmpSlotDefaultModelFileNames[slotIndex]);
      _SetAmpSlotFixedModelPath(slotIndex, slotModelPath);
      _SetAmpSlotModelPath(slotIndex, slotModelPath);
    }
    setWdlPath(mStompNAMPath, defaultsDir / "Boost1.nam");
    setWdlPath(mIRPath, defaultsDir / "Cab1.wav");
    mIRPathRight.Set("");
  }

  auto resolveDefaultCabIfMissing = [&]() {
    const std::filesystem::path currentIRPath = makeAbsoluteNoThrow(std::filesystem::path(mIRPath.Get()));
    if (mIRPath.GetLength() > 0 && existsNoThrow(currentIRPath))
      return;

    for (const auto& candidateDir : candidateDirs)
    {
      const std::filesystem::path cabPath = candidateDir / "Cab1.wav";
      if (!existsNoThrow(cabPath))
        continue;
      setWdlPath(mIRPath, makeAbsoluteNoThrow(cabPath));
      mIRPathRight.Set("");
      return;
    }

    for (const auto& ampPath : mAmpNAMPaths)
    {
      if (ampPath.GetLength() <= 0)
        continue;
      const std::filesystem::path ampFilePath = makeAbsoluteNoThrow(std::filesystem::path(ampPath.Get()));
      if (!existsNoThrow(ampFilePath))
        continue;

      const std::filesystem::path cabPath = ampFilePath.parent_path() / "Cab1.wav";
      if (!existsNoThrow(cabPath))
        continue;

      setWdlPath(mIRPath, makeAbsoluteNoThrow(cabPath));
      mIRPathRight.Set("");
      return;
    }
  };
  resolveDefaultCabIfMissing();
#endif

  const int activeSlot = std::clamp(mAmpSelectorIndex, 0, static_cast<int>(mAmpNAMPaths.size()) - 1);
  for (int slotIndex = 0; slotIndex < static_cast<int>(mAmpNAMPaths.size()); ++slotIndex)
  {
    if (!mAmpNAMPaths[slotIndex].GetLength())
      continue;
    auto& slotState = mAmpSlotStates[slotIndex];
    slotState.modelToggle = 1.0;
    slotState.modelToggleTouched = true;
    _RequestModelLoadForSlot(mAmpNAMPaths[slotIndex], slotIndex, _GetAmpModelCtrlTagForSlot(slotIndex));
  }
  _ApplyAmpSlotState(activeSlot);
  if (!GetParam(kIRToggle)->Bool())
  {
    GetParam(kIRToggle)->Set(1.0);
    SendParameterValueFromDelegate(kIRToggle, GetParam(kIRToggle)->GetNormalized(), true);
    OnParamChange(kIRToggle);
  }
  if (mStompNAMPath.GetLength())
  {
    mShouldRemoveStompModel = false;
    _StageStompModel(mStompNAMPath);
  }
  else
    mShouldRemoveStompModel = true;
  if (mIRPath.GetLength())
  {
    mShouldRemoveIRLeft = false;
    _StageIRLeft(mIRPath);
  }
  else
    mShouldRemoveIRLeft = true;
  if (mIRPathRight.GetLength())
  {
    mShouldRemoveIRRight = false;
    _StageIRRight(mIRPathRight);
  }
  else
    mShouldRemoveIRRight = true;
  mPendingAmpSlotSwitch.store(activeSlot, std::memory_order_release);

  const auto tunerIdx = static_cast<size_t>(TopNavSection::Tuner);
  mTopNavBypassed[tunerIdx] = true;
  mTopNavBypassed[static_cast<size_t>(TopNavSection::Cab)] = false;
  if (GetParam(kTunerActive)->Bool())
  {
    GetParam(kTunerActive)->Set(0.0);
    SendParameterValueFromDelegate(kTunerActive, GetParam(kTunerActive)->GetNormalized(), true);
    OnParamChange(kTunerActive);
  }
  _RefreshTopNavControls();

  mStandalonePresetFilePath.Set("");
  mStandalonePresetIndex = -1;
  mDefaultPresetActive = true;
  mDefaultPresetPostLoadSyncPending = true;
  _SetStandalonePresetDirty(false);
  _RefreshStandalonePresetList();
  _UpdatePresetLabel();
  return true;
}

bool NeuralAmpModeler::_SaveStandalonePresetToFile(const WDL_String& filePath)
{
  std::filesystem::path path = EnsureStandalonePresetExtension(std::filesystem::path(filePath.Get()));
  std::error_code ec;
  path = std::filesystem::absolute(path, ec);
  if (ec)
    path = EnsureStandalonePresetExtension(std::filesystem::path(filePath.Get()));

  IByteChunk chunk;
  if (!SerializeState(chunk))
    return false;

  if (!SaveChunkToFile(path, chunk))
    return false;

  mStandalonePresetFilePath.Set(path.string().c_str());
  mDefaultPresetActive = false;
  mDefaultPresetPostLoadSyncPending = false;
  _SetStandalonePresetDirty(false);
  _RefreshStandalonePresetList();
  _UpdatePresetLabel();
  return true;
}

void NeuralAmpModeler::_PromptStandalonePresetSaveAs()
{
  auto* pGraphics = GetUI();
  if (pGraphics == nullptr)
    return;

  auto* pPresetLabelControl = pGraphics->GetControlWithTag(kCtrlTagPresetLabel);
  auto* pProxyControl =
    dynamic_cast<StandalonePresetNameEntryControl*>(pGraphics->GetControlWithTag(kCtrlTagStandalonePresetNameEntryProxy));
  if (pPresetLabelControl == nullptr || pProxyControl == nullptr)
  {
    WDL_String fileName;
    if (mStandalonePresetFilePath.GetLength() > 0)
      fileName.Set(std::filesystem::path(mStandalonePresetFilePath.Get()).filename().string().c_str());
    else
      fileName.Set(kStandalonePresetDefaultFileName);
    WDL_String path;
    const auto userPresetDir = GetStandaloneUserPresetDirectoryPath();
    if (!userPresetDir.empty())
      path.Set(userPresetDir.string().c_str());
    pGraphics->PromptForFile(fileName, path, EFileAction::Save, kStandalonePresetExtension, [this](const WDL_String& file, const WDL_String&) {
      if (file.GetLength() == 0)
        return;
      if (!_SaveStandalonePresetToFile(file))
        _ShowMessageBox(GetUI(), "Failed to save preset.", "Preset Save Error", kMB_OK);
    });
    return;
  }

  std::string suggestedName = "Preset";
  if (mStandalonePresetFilePath.GetLength() > 0)
    suggestedName = std::filesystem::path(mStandalonePresetFilePath.Get()).stem().string();

  pProxyControl->SetCompletionHandler([this](const char* enteredText) {
    const std::filesystem::path userPresetDir = GetStandaloneUserPresetDirectoryPath();
    if (userPresetDir.empty())
    {
      _ShowMessageBox(GetUI(), "Preset directory is unavailable.", "Preset Save Error", kMB_OK);
      return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(userPresetDir, ec))
      std::filesystem::create_directories(userPresetDir, ec);
    if (ec)
    {
      _ShowMessageBox(GetUI(), "Failed to create preset directory.", "Preset Save Error", kMB_OK);
      return;
    }

    const std::string sanitizedName = SanitizeStandalonePresetName(enteredText != nullptr ? enteredText : "");
    std::filesystem::path targetPath = EnsureStandalonePresetExtension(userPresetDir / sanitizedName);
    std::error_code existsEc;
    const bool targetExists = std::filesystem::exists(targetPath, existsEc);
    if (existsEc)
    {
      _ShowMessageBox(GetUI(), "Failed to access preset path.", "Preset Save Error", kMB_OK);
      return;
    }

    if (targetExists)
    {
      std::string confirmMessage = "Preset \"";
      confirmMessage += targetPath.stem().string();
      confirmMessage += "\" already exists. Overwrite?";
      const EMsgBoxResult overwriteResult = _ShowMessageBox(GetUI(), confirmMessage.c_str(), "Overwrite Preset", kMB_YESNO);
      if (overwriteResult != kYES)
      {
        mStandalonePresetNameEntryPendingText.Set(sanitizedName.c_str());
        mStandalonePresetNameEntryReopenPending = true;
        return;
      }
    }

    WDL_String targetFile;
    targetFile.Set(targetPath.string().c_str());
    if (!_SaveStandalonePresetToFile(targetFile))
      _ShowMessageBox(GetUI(), "Failed to save preset.", "Preset Save Error", kMB_OK);
  });

  const IText entryText = MakePresetNameEntryText(pPresetLabelControl->GetText());
  pGraphics->CreateTextEntry(*pProxyControl, entryText, pPresetLabelControl->GetRECT(), suggestedName.c_str());
}

void NeuralAmpModeler::_PromptStandalonePresetRename()
{
  auto* pGraphics = GetUI();
  if (pGraphics == nullptr || mStandalonePresetFilePath.GetLength() <= 0)
    return;
  if (_IsStandaloneFactoryPresetPath(mStandalonePresetFilePath))
    return;

  auto* pPresetLabelControl = pGraphics->GetControlWithTag(kCtrlTagPresetLabel);
  auto* pProxyControl =
    dynamic_cast<StandalonePresetNameEntryControl*>(pGraphics->GetControlWithTag(kCtrlTagStandalonePresetNameEntryProxy));
  if (pPresetLabelControl == nullptr || pProxyControl == nullptr)
    return;

  const std::filesystem::path sourcePath(mStandalonePresetFilePath.Get());
  const std::string suggestedName = sourcePath.stem().string();
  pProxyControl->SetCompletionHandler([this, sourcePath](const char* enteredText) {
    const std::string sanitizedName = SanitizeStandalonePresetName(enteredText != nullptr ? enteredText : "");
    const std::filesystem::path targetPath = EnsureStandalonePresetExtension(sourcePath.parent_path() / sanitizedName);
    std::error_code equivalentEc;
    if (std::filesystem::equivalent(sourcePath, targetPath, equivalentEc))
      return;

    std::error_code ec;
    const bool targetExists = std::filesystem::exists(targetPath, ec);
    if (ec)
    {
      _ShowMessageBox(GetUI(), "Failed to access preset path.", "Preset Rename Error", kMB_OK);
      return;
    }

    if (targetExists)
    {
      std::string confirmMessage = "Preset \"";
      confirmMessage += targetPath.stem().string();
      confirmMessage += "\" already exists. Overwrite?";
      const EMsgBoxResult overwriteResult = _ShowMessageBox(GetUI(), confirmMessage.c_str(), "Overwrite Preset", kMB_YESNO);
      if (overwriteResult != kYES)
      {
        mStandalonePresetNameEntryPendingText.Set(sanitizedName.c_str());
        mStandalonePresetNameEntryReopenPending = true;
        return;
      }

      std::filesystem::remove(targetPath, ec);
      if (ec)
      {
        _ShowMessageBox(GetUI(), "Failed to overwrite preset.", "Preset Rename Error", kMB_OK);
        return;
      }
    }

    std::filesystem::rename(sourcePath, targetPath, ec);
    if (ec)
    {
      _ShowMessageBox(GetUI(), "Failed to rename preset.", "Preset Rename Error", kMB_OK);
      return;
    }

    mStandalonePresetFilePath.Set(targetPath.string().c_str());
    mDefaultPresetActive = false;
    _RefreshStandalonePresetList();
    _UpdatePresetLabel();
  });

  const IText entryText = MakePresetNameEntryText(pPresetLabelControl->GetText());
  pGraphics->CreateTextEntry(*pProxyControl, entryText, pPresetLabelControl->GetRECT(), suggestedName.c_str());
}

void NeuralAmpModeler::_PromptStandalonePresetDelete()
{
  auto* pGraphics = GetUI();
  if (pGraphics == nullptr || mStandalonePresetFilePath.GetLength() <= 0)
    return;
  if (_IsStandaloneFactoryPresetPath(mStandalonePresetFilePath))
    return;

  _RefreshStandalonePresetList();
  int removedIndex = -1;
  for (int i = 0; i < static_cast<int>(mStandalonePresetPaths.size()); ++i)
  {
    if (std::strcmp(mStandalonePresetPaths[i].Get(), mStandalonePresetFilePath.Get()) == 0)
    {
      removedIndex = i;
      break;
    }
  }

  const std::filesystem::path removedPath(mStandalonePresetFilePath.Get());
  const std::string presetName = removedPath.stem().string();
  std::string confirmMessage = "Delete preset \"";
  confirmMessage += presetName;
  confirmMessage += "\"?";
  const EMsgBoxResult result = _ShowMessageBox(pGraphics, confirmMessage.c_str(), "Delete Preset", kMB_YESNO);
  if (result != kYES)
    return;

  std::error_code ec;
  const bool removed = std::filesystem::remove(removedPath, ec);
  if (ec || !removed)
  {
    _ShowMessageBox(GetUI(), "Failed to delete preset.", "Preset Delete Error", kMB_OK);
    return;
  }

  _RefreshStandalonePresetList();
  if (!mStandalonePresetPaths.empty())
  {
    if (removedIndex < 0 || removedIndex >= static_cast<int>(mStandalonePresetPaths.size()))
      removedIndex = static_cast<int>(mStandalonePresetPaths.size()) - 1;
    _LoadStandalonePresetFromFile(mStandalonePresetPaths[removedIndex]);
    return;
  }

  if (!_LoadDefaultPreset())
  {
    mStandalonePresetFilePath.Set("");
    mDefaultPresetActive = false;
    _SetStandalonePresetDirty(false);
    _UpdatePresetLabel();
  }
}

void NeuralAmpModeler::_SelectStandalonePresetRelative(const int delta)
{
  _RefreshStandalonePresetList();
  if (mStandalonePresetPaths.empty())
    return;

  int nextIndex = mStandalonePresetIndex;
  if (nextIndex < 0 || nextIndex >= static_cast<int>(mStandalonePresetPaths.size()))
    nextIndex = (delta < 0) ? static_cast<int>(mStandalonePresetPaths.size()) - 1 : 0;
  else
    nextIndex = (nextIndex + delta + static_cast<int>(mStandalonePresetPaths.size())) % static_cast<int>(mStandalonePresetPaths.size());

  if (nextIndex < 0 || nextIndex >= static_cast<int>(mStandalonePresetPaths.size()))
    return;

  _LoadStandalonePresetFromFile(mStandalonePresetPaths[nextIndex]);
}

void NeuralAmpModeler::_ShowStandalonePresetMenu(const IRECT& anchorArea)
{
  auto* pGraphics = GetUI();
  if (pGraphics == nullptr)
    return;

  _RefreshStandalonePresetList();

  mStandalonePresetMenu.Clear();
  mStandalonePresetMenu.SetFunction([this](IPopupMenu* pMenu) {
    if (pMenu == nullptr)
      return;
    auto* pItem = pMenu->GetChosenItem();
    if (pItem == nullptr)
      return;

    const int tag = pItem->GetTag();
    if (tag == kPresetMenuTagDefault)
    {
      if (!_LoadDefaultPreset())
        _ShowMessageBox(GetUI(), "Default preset is unavailable.", "Preset Load Error", kMB_OK);
      return;
    }
    if (tag == kPresetMenuTagSave)
    {
      if (mStandalonePresetFilePath.GetLength() > 0 && !_IsStandaloneFactoryPresetPath(mStandalonePresetFilePath))
      {
        _SaveStandalonePresetToFile(mStandalonePresetFilePath);
      }
      else
      {
        _PromptStandalonePresetSaveAs();
      }
      return;
    }
    if (tag == kPresetMenuTagSaveAs)
    {
      _PromptStandalonePresetSaveAs();
      return;
    }
    if (tag == kPresetMenuTagRename)
    {
      _PromptStandalonePresetRename();
      return;
    }
    if (tag == kPresetMenuTagDelete)
    {
      _PromptStandalonePresetDelete();
      return;
    }
    if (tag >= kPresetMenuTagUserBase
        && tag < kPresetMenuTagUserBase + static_cast<int>(mStandaloneUserPresetPaths.size()))
    {
      _LoadStandalonePresetFromFile(mStandaloneUserPresetPaths[tag - kPresetMenuTagUserBase]);
      return;
    }
    if (tag >= kPresetMenuTagFactoryBase
        && tag < kPresetMenuTagFactoryBase + static_cast<int>(mStandaloneFactoryPresetPaths.size()))
    {
      _LoadStandalonePresetFromFile(mStandaloneFactoryPresetPaths[tag - kPresetMenuTagFactoryBase]);
      return;
    }
  });

  int defaultFlags = mDefaultPresetActive ? IPopupMenu::Item::kChecked : IPopupMenu::Item::kNoFlags;
  if (!mHasDefaultPresetState || mDefaultPresetStateChunk.Size() <= 0)
    defaultFlags |= IPopupMenu::Item::kDisabled;
  mStandalonePresetMenu.AddItem(new IPopupMenu::Item("Default", defaultFlags, kPresetMenuTagDefault));
  mStandalonePresetMenu.AddSeparator();
  mStandalonePresetMenu.AddItem(new IPopupMenu::Item("Save", IPopupMenu::Item::kNoFlags, kPresetMenuTagSave));
  mStandalonePresetMenu.AddItem(new IPopupMenu::Item("Save As...", IPopupMenu::Item::kNoFlags, kPresetMenuTagSaveAs));
  const bool canRenamePreset =
    (mStandalonePresetFilePath.GetLength() > 0) && !_IsStandaloneFactoryPresetPath(mStandalonePresetFilePath);
  const int renameFlags = canRenamePreset ? IPopupMenu::Item::kNoFlags : IPopupMenu::Item::kDisabled;
  mStandalonePresetMenu.AddItem(new IPopupMenu::Item("Rename Preset", renameFlags, kPresetMenuTagRename));
  const bool canDeletePreset =
    (mStandalonePresetFilePath.GetLength() > 0) && !_IsStandaloneFactoryPresetPath(mStandalonePresetFilePath);
  const int deleteFlags = canDeletePreset ? IPopupMenu::Item::kNoFlags : IPopupMenu::Item::kDisabled;
  mStandalonePresetMenu.AddItem(new IPopupMenu::Item("Delete Preset", deleteFlags, kPresetMenuTagDelete));
  mStandalonePresetMenu.AddSeparator();

  auto* pUserSubmenu = new IPopupMenu();
  if (mStandaloneUserPresetPaths.empty())
  {
    pUserSubmenu->AddItem("No User Presets", -1, IPopupMenu::Item::kDisabled);
  }
  else
  {
    for (int i = 0; i < static_cast<int>(mStandaloneUserPresetPaths.size()); ++i)
    {
      const std::filesystem::path path(mStandaloneUserPresetPaths[i].Get());
      const std::string name = path.stem().string();
      const bool isCurrent = std::strcmp(mStandaloneUserPresetPaths[i].Get(), mStandalonePresetFilePath.Get()) == 0;
      const int flags = isCurrent ? IPopupMenu::Item::kChecked : IPopupMenu::Item::kNoFlags;
      pUserSubmenu->AddItem(new IPopupMenu::Item(name.c_str(), flags, kPresetMenuTagUserBase + i));
    }
  }

  auto* pFactorySubmenu = new IPopupMenu();
  if (mStandaloneFactoryPresetPaths.empty())
  {
    pFactorySubmenu->AddItem("No Factory Presets", -1, IPopupMenu::Item::kDisabled);
  }
  else
  {
    for (int i = 0; i < static_cast<int>(mStandaloneFactoryPresetPaths.size()); ++i)
    {
      const std::filesystem::path path(mStandaloneFactoryPresetPaths[i].Get());
      const std::string name = path.stem().string();
      const bool isCurrent = std::strcmp(mStandaloneFactoryPresetPaths[i].Get(), mStandalonePresetFilePath.Get()) == 0;
      const int flags = isCurrent ? IPopupMenu::Item::kChecked : IPopupMenu::Item::kNoFlags;
      pFactorySubmenu->AddItem(new IPopupMenu::Item(name.c_str(), flags, kPresetMenuTagFactoryBase + i));
    }
  }

  mStandalonePresetMenu.AddItem("Factory Presets", pFactorySubmenu);
  mStandalonePresetMenu.AddItem("User Presets", pUserSubmenu);
  auto* pPresetControl = pGraphics->GetControlWithTag(kCtrlTagPresetLabel);
  if (pPresetControl == nullptr)
    return;
  pGraphics->CreatePopupMenu(*pPresetControl, mStandalonePresetMenu, anchorArea);
}

bool NeuralAmpModeler::_IsStandaloneFactoryPresetPath(const WDL_String& filePath) const
{
  for (const auto& factoryPath : mStandaloneFactoryPresetPaths)
  {
    if (std::strcmp(factoryPath.Get(), filePath.Get()) == 0)
      return true;
  }
  return false;
}

void NeuralAmpModeler::_SetStandalonePresetDirty(const bool isDirty)
{
  if (mStandalonePresetDirty == isDirty)
    return;

  mStandalonePresetDirty = isDirty;
  _UpdatePresetLabel();
}

void NeuralAmpModeler::_MarkStandalonePresetDirty()
{
  _SetStandalonePresetDirty(true);
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

  const bool previous = mTopNavBypassed[idx];
  mTopNavBypassed[idx] = !mTopNavBypassed[idx];
  if (mTopNavBypassed[idx] != previous)
    _MarkStandalonePresetDirty();

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

void NeuralAmpModeler::_StartModelLoadWorker()
{
  if (mModelLoadWorker.joinable())
    return;
  mModelLoadWorkerExit = false;
  mModelLoadWorker = std::thread([this]() { _ModelLoadWorkerLoop(); });
}

void NeuralAmpModeler::_StopModelLoadWorker()
{
  {
    std::lock_guard<std::mutex> lock(mModelLoadMutex);
    mModelLoadWorkerExit = true;
    mModelLoadJobs.clear();
  }
  mModelLoadCV.notify_all();
  if (mModelLoadWorker.joinable())
    mModelLoadWorker.join();
}

void NeuralAmpModeler::_RequestModelLoadForSlot(const WDL_String& modelPath, int slotIndex, int slotCtrlTag,
                                                const bool userInitiated)
{
  slotIndex = std::clamp(slotIndex, 0, static_cast<int>(mAmpNAMPaths.size()) - 1);
  if (userInitiated && !_CanEditAmpSlotModel(slotIndex))
    return;

  const bool slotLocked = !_CanEditAmpSlotModel(slotIndex);
  if (mAmpWorkflowMode == AmpWorkflowMode::Release && slotLocked && !userInitiated
      && mAmpSlotFixedModelPaths[slotIndex].GetLength() == 0 && modelPath.GetLength() > 0)
  {
    _SetAmpSlotFixedModelPath(slotIndex, modelPath);
  }

  const WDL_String effectiveModelPath = _ResolveAmpSlotModelPathForMode(slotIndex, modelPath);
  if (effectiveModelPath.GetLength() == 0)
    return;

  _SetAmpSlotModelPath(slotIndex, effectiveModelPath);

  mAmpSlotModelState[slotIndex].store(kAmpSlotModelStateLoading, std::memory_order_release);
  mSlotLoadUIEvent[slotIndex].store(kSlotLoadUIEventNone, std::memory_order_relaxed);
  const uint64_t requestId = mSlotLoadRequestId[slotIndex].fetch_add(1, std::memory_order_relaxed) + 1;
  _ClearAmpSlotCapabilityState(slotIndex);

  {
    std::lock_guard<std::mutex> lock(mModelLoadMutex);
    for (auto it = mModelLoadJobs.begin(); it != mModelLoadJobs.end();)
    {
      if (it->slotIndex == slotIndex)
        it = mModelLoadJobs.erase(it);
      else
        ++it;
    }
    ModelLoadJob job;
    job.slotIndex = slotIndex;
    job.requestId = requestId;
    job.modelPath = effectiveModelPath;
    const double sampleRate = GetSampleRate();
    job.sampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
    job.blockSize = std::max(1, GetBlockSize());
    mModelLoadJobs.push_back(std::move(job));
  }
  mModelLoadCV.notify_one();

  SendControlMsgFromDelegate(slotCtrlTag, kMsgTagLoadedModel, effectiveModelPath.GetLength(), effectiveModelPath.Get());
}

bool NeuralAmpModeler::_CanEditAmpSlotModel(int slotIndex) const
{
  slotIndex = std::clamp(slotIndex, 0, static_cast<int>(mAmpSlotModelEditLocked.size()) - 1);
  if (mAmpWorkflowMode == AmpWorkflowMode::Rig)
    return true;
  return !mAmpSlotModelEditLocked[static_cast<size_t>(slotIndex)];
}

WDL_String NeuralAmpModeler::_ResolveAmpSlotModelPathForMode(int slotIndex, const WDL_String& requestedPath) const
{
  AmpSlotModelSource requestedSource;
  requestedSource.kind = AmpSlotModelSourceKind::ExternalPath;
  requestedSource.value = requestedPath;
  return _ResolveAmpSlotModelSourceToPathForMode(slotIndex, requestedSource);
}

WDL_String NeuralAmpModeler::_ResolveAmpSlotModelSourceToPathForMode(int slotIndex,
                                                                      const AmpSlotModelSource& requestedSource) const
{
  slotIndex = std::clamp(slotIndex, 0, static_cast<int>(mAmpSlotFixedModelPaths.size()) - 1);
  AmpSlotModelSource effectiveSource = requestedSource;
  if (mAmpWorkflowMode == AmpWorkflowMode::Release && !_CanEditAmpSlotModel(slotIndex))
  {
    const WDL_String& fixedPath = mAmpSlotFixedModelPaths[slotIndex];
    if (fixedPath.GetLength() > 0)
    {
      effectiveSource.kind = AmpSlotModelSourceKind::ExternalPath;
      effectiveSource.value = fixedPath;
    }
  }

  if (effectiveSource.kind == AmpSlotModelSourceKind::EmbeddedModelId)
  {
    // Milestone-B scaffold: embedded model ids are not materialized to loadable paths yet.
    WDL_String emptyPath;
    emptyPath.Set("");
    return emptyPath;
  }

  WDL_String path;
  path.Set(effectiveSource.value.Get());
  return path;
}

void NeuralAmpModeler::_SetAmpSlotModelSource(int slotIndex, const AmpSlotModelSource& source)
{
  slotIndex = std::clamp(slotIndex, 0, static_cast<int>(mAmpSlotModelSources.size()) - 1);
  mAmpSlotModelSources[slotIndex].kind = source.kind;
  mAmpSlotModelSources[slotIndex].value = source.value;
}

void NeuralAmpModeler::_SetAmpSlotFixedModelPath(int slotIndex, const WDL_String& modelPath)
{
  slotIndex = std::clamp(slotIndex, 0, static_cast<int>(mAmpSlotFixedModelPaths.size()) - 1);
  mAmpSlotFixedModelPaths[slotIndex] = modelPath;
}

void NeuralAmpModeler::_SetAmpSlotModelPath(int slotIndex, const WDL_String& modelPath)
{
  slotIndex = std::clamp(slotIndex, 0, static_cast<int>(mAmpNAMPaths.size()) - 1);
  AmpSlotModelSource source;
  source.kind = AmpSlotModelSourceKind::ExternalPath;
  source.value = modelPath;
  _SetAmpSlotModelSource(slotIndex, source);
  mAmpNAMPaths[slotIndex] = modelPath;
  if (slotIndex == mAmpSelectorIndex)
    mNAMPath = modelPath;
}

void NeuralAmpModeler::_ModelLoadWorkerLoop()
{
  while (true)
  {
    ModelLoadJob job;
    {
      std::unique_lock<std::mutex> lock(mModelLoadMutex);
      mModelLoadCV.wait(lock, [this]() { return mModelLoadWorkerExit || !mModelLoadJobs.empty(); });
      if (mModelLoadWorkerExit && mModelLoadJobs.empty())
        return;
      job = std::move(mModelLoadJobs.front());
      mModelLoadJobs.pop_front();
    }

    const int slotIndex = std::clamp(job.slotIndex, 0, static_cast<int>(mAmpNAMPaths.size()) - 1);
    if (job.requestId != mSlotLoadRequestId[slotIndex].load(std::memory_order_relaxed))
      continue;

    bool success = false;
    bool hasLoudness = false;
    bool hasCalibration = false;
    std::unique_ptr<ResamplingNAM> loadedModel;
    std::unique_ptr<ResamplingNAM> loadedModelRight;
    try
    {
      auto dspPath = std::filesystem::u8path(job.modelPath.Get());
      const double sampleRate = (job.sampleRate > 0.0) ? job.sampleRate : 48000.0;
      const int blockSize = std::max(1, job.blockSize);
      auto loadResampledModel = [&dspPath, sampleRate, blockSize]() {
        std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);
        std::unique_ptr<ResamplingNAM> temp = std::make_unique<ResamplingNAM>(std::move(model), sampleRate);
        temp->Reset(sampleRate, blockSize);
        return temp;
      };

      loadedModel = loadResampledModel();
      loadedModelRight = loadResampledModel();
      success = (loadedModel != nullptr) && (loadedModelRight != nullptr);
      if (success)
      {
        hasLoudness = loadedModel->HasLoudness();
        hasCalibration = loadedModel->HasOutputLevel();
      }
    }
    catch (...)
    {
      success = false;
    }

    if (job.requestId != mSlotLoadRequestId[slotIndex].load(std::memory_order_relaxed))
      continue;

    if (success)
    {
      _SetAmpSlotCapabilityState(slotIndex, hasLoudness, hasCalibration);
      mPendingLoadedSlotRequestId[slotIndex].store(job.requestId, std::memory_order_release);
      if (auto* oldPtr =
            mPendingLoadedSlotModelRight[slotIndex].exchange(loadedModelRight.release(), std::memory_order_acq_rel))
        delete oldPtr;
      if (auto* oldPtr = mPendingLoadedSlotModel[slotIndex].exchange(loadedModel.release(), std::memory_order_acq_rel))
        delete oldPtr;
      mSlotLoadUIEvent[slotIndex].store(kSlotLoadUIEventLoaded, std::memory_order_relaxed);
    }
    else
    {
      _ClearAmpSlotCapabilityState(slotIndex);
      mSlotLoadUIEvent[slotIndex].store(kSlotLoadUIEventFailed, std::memory_order_relaxed);
    }
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

  _CaptureAmpSlotState(mAmpSelectorIndex);
  mAmpSelectorIndex = slotIndex;
  _MarkStandalonePresetDirty();
  mPendingAmpSlotSwitch.store(slotIndex, std::memory_order_release);

  const int slotCtrlTag = _GetAmpModelCtrlTagForSlot(slotIndex);
  const WDL_String& slotPath = mAmpNAMPaths[slotIndex];
  if (slotPath.GetLength())
  {
    mNAMPath = slotPath;
    const int slotModelState = mAmpSlotModelState[slotIndex].load(std::memory_order_relaxed);
    if (slotModelState != kAmpSlotModelStateLoading && slotModelState != kAmpSlotModelStateReady)
      _RequestModelLoadForSlot(slotPath, slotIndex, slotCtrlTag);
  }
  else
  {
    mNAMPath.Set("");
    mShouldRemoveModelSlot[slotIndex].store(true, std::memory_order_relaxed);
    mAmpSlotModelState[slotIndex].store(kAmpSlotModelStateEmpty, std::memory_order_relaxed);
    mSlotLoadRequestId[slotIndex].fetch_add(1, std::memory_order_relaxed);
    _ClearAmpSlotCapabilityState(slotIndex);
    mAmpSlotStates[slotIndex].modelToggle = 0.0;
    mAmpSlotStates[slotIndex].modelToggleTouched = true;
  }

  _ApplyAmpSlotState(slotIndex);
  if (!slotPath.GetLength() && GetParam(kModelToggle)->Bool())
  {
    GetParam(kModelToggle)->Set(0.0);
    SendParameterValueFromDelegate(kModelToggle, GetParam(kModelToggle)->GetNormalized(), true);
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
    const bool showEqSection = (mTopNavActiveSection == TopNavSection::Eq);
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
    updateIcon(kCtrlTagTopNavEq, TopNavSection::Eq);
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
    else if (mTopNavActiveSection == TopNavSection::Eq)
      backgroundResource = EQBACKGROUND_FN;
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
      pFXEQOnLED->Hide(!showEqSection);
    if (auto* pFXDelayOnLED = pGraphics->GetControlWithTag(kCtrlTagFXDelayOnLED))
      pFXDelayOnLED->Hide(!showFxSection);
    if (auto* pFXReverbOnLED = pGraphics->GetControlWithTag(kCtrlTagFXReverbOnLED))
      pFXReverbOnLED->Hide(!showFxSection);
    pGraphics->ForControlInGroup("FX_CONTROLS", [showFxSection](IControl* pControl) {
      pControl->Hide(!showFxSection);
    });
    pGraphics->ForControlInGroup("EQ_CONTROLS", [showEqSection](IControl* pControl) {
      pControl->Hide(!showEqSection);
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

bool NeuralAmpModeler::_IsAmpSlotManagedParam(const int paramIdx) const
{
  switch (paramIdx)
  {
    case kModelToggle:
    case kEQActive:
    case kPreModelGain:
    case kToneBass:
    case kToneMid:
    case kToneTreble:
    case kTonePresence:
    case kToneDepth:
    case kMasterVolume: return true;
    default: return false;
  }
}

void NeuralAmpModeler::_CaptureAmpSlotState(int slotIndex)
{
  slotIndex = std::clamp(slotIndex, 0, static_cast<int>(mAmpSlotStates.size()) - 1);
  auto& state = mAmpSlotStates[slotIndex];
  state.modelToggle = GetParam(kModelToggle)->Bool() ? 1.0 : 0.0;
  state.toneStackActive = GetParam(kEQActive)->Bool() ? 1.0 : 0.0;
  state.preModelGain = GetParam(kPreModelGain)->Value();
  state.bass = GetParam(kToneBass)->Value();
  state.mid = GetParam(kToneMid)->Value();
  state.treble = GetParam(kToneTreble)->Value();
  state.presence = GetParam(kTonePresence)->Value();
  state.depth = GetParam(kToneDepth)->Value();
  state.master = GetParam(kMasterVolume)->Value();
}

void NeuralAmpModeler::_ApplyAmpSlotStateToToneStack(int slotIndex)
{
  slotIndex = std::clamp(slotIndex, 0, static_cast<int>(mToneStacks.size()) - 1);
  auto* toneStack = mToneStacks[slotIndex].get();
  if (toneStack == nullptr)
    return;

  const auto& state = mAmpSlotStates[slotIndex];
  toneStack->SetParam("bass", state.bass);
  toneStack->SetParam("middle", state.mid);
  toneStack->SetParam("treble", state.treble);
  toneStack->SetParam("presence", state.presence);
  toneStack->SetParam("depth", state.depth);
}

void NeuralAmpModeler::_ApplyCurrentAmpParamsToActiveToneStack()
{
  const int activeSlot = std::clamp(mAmpSelectorIndex, 0, static_cast<int>(mToneStacks.size()) - 1);
  auto* toneStack = mToneStacks[activeSlot].get();
  if (toneStack == nullptr)
    return;

  toneStack->SetParam("bass", GetParam(kToneBass)->Value());
  toneStack->SetParam("middle", GetParam(kToneMid)->Value());
  toneStack->SetParam("treble", GetParam(kToneTreble)->Value());
  toneStack->SetParam("presence", GetParam(kTonePresence)->Value());
  toneStack->SetParam("depth", GetParam(kToneDepth)->Value());
}

void NeuralAmpModeler::_ApplyAmpSlotState(int slotIndex)
{
  slotIndex = std::clamp(slotIndex, 0, static_cast<int>(mAmpSlotStates.size()) - 1);
  auto& state = mAmpSlotStates[slotIndex];
  const bool hasSlotModelPath = mAmpNAMPaths[slotIndex].GetLength() > 0;
  const bool useModelToggleFallback = hasSlotModelPath && !state.modelToggleTouched;
  const double modelToggleValue = useModelToggleFallback ? 1.0 : state.modelToggle;
  auto applyParam = [this](const int paramIdx, const double value) {
    auto* param = GetParam(paramIdx);
    param->Set(value);
    SendParameterValueFromDelegate(paramIdx, param->GetNormalized(), true);
  };

  mApplyingAmpSlotState = true;
  applyParam(kModelToggle, modelToggleValue);
  applyParam(kEQActive, state.toneStackActive);
  applyParam(kPreModelGain, state.preModelGain);
  applyParam(kToneBass, state.bass);
  applyParam(kToneMid, state.mid);
  applyParam(kToneTreble, state.treble);
  applyParam(kTonePresence, state.presence);
  applyParam(kToneDepth, state.depth);
  applyParam(kMasterVolume, state.master);
  mApplyingAmpSlotState = false;

  if (useModelToggleFallback)
  {
    state.modelToggle = modelToggleValue;
    state.modelToggleTouched = true;
  }

  _SetMasterGain();
  _ApplyCurrentAmpParamsToActiveToneStack();
  _CaptureAmpSlotState(slotIndex);
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
  const bool inputStereoMode = GetParam(kInputStereoMode)->Bool();
  bool triggerOutputDeClick = false;
  auto updateActiveModelGainsAndLatency = [this]() {
    _UpdateLatency();
    _SetInputGain();
    _SetOutputGain();
  };

  // Slot-targeted model removals (requested from non-audio threads).
  for (int slotIndex = 0; slotIndex < static_cast<int>(mAmpNAMPaths.size()); ++slotIndex)
  {
    if (!mShouldRemoveModelSlot[slotIndex].exchange(false, std::memory_order_acq_rel))
      continue;

    _ClearAmpSlotCapabilityState(slotIndex);
    mAmpSlotModelCache[slotIndex] = nullptr;
    mAmpSlotModelCacheRight[slotIndex] = nullptr;
    if (mAmpSlotModelState[slotIndex].load(std::memory_order_acquire) != kAmpSlotModelStateLoading)
      mAmpSlotModelState[slotIndex].store(kAmpSlotModelStateEmpty, std::memory_order_relaxed);

    if (slotIndex == mCurrentModelSlot)
    {
      mModel = nullptr;
      mModelRight = nullptr;
      if (slotIndex == mAmpSelectorIndex
          && mAmpSlotModelState[slotIndex].load(std::memory_order_acquire) == kAmpSlotModelStateEmpty)
        mNAMPath.Set("");
      mModelCleared = true;
      updateActiveModelGainsAndLatency();
    }
  }

  // Worker -> audio lock-free handoff for newly loaded slot models.
  for (int slotIndex = 0; slotIndex < static_cast<int>(mAmpNAMPaths.size()); ++slotIndex)
  {
    ResamplingNAM* loadedLeft = mPendingLoadedSlotModel[slotIndex].load(std::memory_order_acquire);
    if (loadedLeft == nullptr)
      continue;
    loadedLeft = mPendingLoadedSlotModel[slotIndex].exchange(nullptr, std::memory_order_acq_rel);
    if (loadedLeft == nullptr)
      continue;
    ResamplingNAM* loadedRight =
      mPendingLoadedSlotModelRight[slotIndex].exchange(nullptr, std::memory_order_acq_rel);
    if (loadedRight == nullptr)
    {
      // Worker publishes right then left; if we catch an in-flight handoff, retry next block.
      ResamplingNAM* expectedNull = nullptr;
      if (!mPendingLoadedSlotModel[slotIndex].compare_exchange_strong(
            expectedNull, loadedLeft, std::memory_order_acq_rel))
      {
        delete loadedLeft;
      }
      continue;
    }

    std::unique_ptr<ResamplingNAM> loadedModel(loadedLeft);
    std::unique_ptr<ResamplingNAM> loadedModelRight(loadedRight);
    const uint64_t pendingRequestId = mPendingLoadedSlotRequestId[slotIndex].load(std::memory_order_acquire);
    const uint64_t activeRequestId = mSlotLoadRequestId[slotIndex].load(std::memory_order_relaxed);
    if (pendingRequestId != activeRequestId)
      continue;

    if (slotIndex == mCurrentModelSlot)
    {
      mModel = std::move(loadedModel);
      mModelRight = std::move(loadedModelRight);
      if (mAmpNAMPaths[slotIndex].GetLength())
        mNAMPath = mAmpNAMPaths[slotIndex];
      mNewModelLoadedInDSP = true;
      mAmpSlotModelState[slotIndex].store(kAmpSlotModelStateReady, std::memory_order_relaxed);
      updateActiveModelGainsAndLatency();
    }
    else
    {
      mAmpSlotModelCache[slotIndex] = std::move(loadedModel);
      mAmpSlotModelCacheRight[slotIndex] = std::move(loadedModelRight);
      mAmpSlotModelState[slotIndex].store(kAmpSlotModelStateReady, std::memory_order_relaxed);
    }
  }

  // Slot switching is resolved on audio thread by swapping active model ownership.
  const int requestedSlot = mPendingAmpSlotSwitch.exchange(-1, std::memory_order_acquire);
  if (requestedSlot >= 0)
  {
    const int targetSlot = std::clamp(requestedSlot, 0, static_cast<int>(mAmpNAMPaths.size()) - 1);
    if (targetSlot != mCurrentModelSlot)
    {
      const bool targetHasPath = (mAmpNAMPaths[targetSlot].GetLength() > 0);
      const int targetState = mAmpSlotModelState[targetSlot].load(std::memory_order_acquire);
      const bool haveReadyTargetModel = (targetState == kAmpSlotModelStateReady) && (mAmpSlotModelCache[targetSlot] != nullptr)
                                        && (!inputStereoMode || mAmpSlotModelCacheRight[targetSlot] != nullptr);
      const bool waitForTargetLoad = targetHasPath && !haveReadyTargetModel
                                     && (targetState == kAmpSlotModelStateLoading);
      if (waitForTargetLoad)
      {
        // Keep current slot live until the target slot model has actually arrived.
        mPendingAmpSlotSwitch.store(targetSlot, std::memory_order_release);
      }
      else
      {
        const int previousSlot = mCurrentModelSlot;
        if (previousSlot >= 0 && previousSlot < static_cast<int>(mAmpNAMPaths.size()))
        {
          mAmpSlotModelCache[previousSlot] = std::move(mModel);
          mAmpSlotModelCacheRight[previousSlot] = std::move(mModelRight);
          const int prevState =
            (mAmpSlotModelCache[previousSlot] != nullptr) ? kAmpSlotModelStateReady : kAmpSlotModelStateEmpty;
          mAmpSlotModelState[previousSlot].store(prevState, std::memory_order_relaxed);
        }

        mCurrentModelSlot = targetSlot;
        if (haveReadyTargetModel)
        {
          mModel = std::move(mAmpSlotModelCache[targetSlot]);
          mModelRight = std::move(mAmpSlotModelCacheRight[targetSlot]);
          mAmpSlotModelState[targetSlot].store(kAmpSlotModelStateReady, std::memory_order_relaxed);
          mNewModelLoadedInDSP = true;
        }
        else
        {
          _ClearAmpSlotCapabilityState(targetSlot);
          mModel = nullptr;
          mModelRight = nullptr;
          if (mAmpSlotModelState[targetSlot].load(std::memory_order_acquire) != kAmpSlotModelStateLoading)
            mAmpSlotModelState[targetSlot].store(kAmpSlotModelStateEmpty, std::memory_order_relaxed);
          mModelCleared = true;
        }

        triggerOutputDeClick = true;
        updateActiveModelGainsAndLatency();
      }
    }
  }

  // Remove marked modules
  if (mShouldRemoveModel)
  {
    _ClearAmpSlotCapabilityState(mCurrentModelSlot);
    mModel = nullptr;
    mModelRight = nullptr;
    mNAMPath.Set("");
    mAmpSlotModelState[mCurrentModelSlot].store(kAmpSlotModelStateEmpty, std::memory_order_relaxed);
    mShouldRemoveModel = false;
    mModelCleared = true;
    updateActiveModelGainsAndLatency();
  }
  if (mShouldRemoveStompModel)
  {
    _ClearStompCapabilityState();
    mStompModel = nullptr;
    mStompModelRight = nullptr;
    mStompNAMPath.Set("");
    mShouldRemoveStompModel = false;
    _UpdateLatency();
  }
  if (mShouldRemoveIRLeft)
  {
    mStagedIR = nullptr;
    mStagedIRChannel2 = nullptr;
    mStagedIRPath.Set("");
    mIR = nullptr;
    mIRChannel2 = nullptr;
    mIRPath.Set("");
    mShouldRemoveIRLeft = false;
    triggerOutputDeClick = true;
  }
  if (mShouldRemoveIRRight)
  {
    mStagedIRRight = nullptr;
    mStagedIRRightChannel2 = nullptr;
    mStagedIRPathRight.Set("");
    mIRRight = nullptr;
    mIRRightChannel2 = nullptr;
    mIRPathRight.Set("");
    mShouldRemoveIRRight = false;
    triggerOutputDeClick = true;
  }
  // Move things from staged to live
  if (mStagedModel != nullptr && (!inputStereoMode || mStagedModelRight != nullptr))
  {
    mModel = std::move(mStagedModel);
    mStagedModel = nullptr;
    mModelRight = std::move(mStagedModelRight);
    mStagedModelRight = nullptr;
    mCurrentModelSlot = std::clamp(mAmpSelectorIndex, 0, static_cast<int>(mAmpNAMPaths.size()) - 1);
    mAmpSlotModelState[mCurrentModelSlot].store(kAmpSlotModelStateReady, std::memory_order_relaxed);
    _SetAmpSlotCapabilityState(mCurrentModelSlot, mModel->HasLoudness(), mModel->HasOutputLevel());
    mNewModelLoadedInDSP = true;
    updateActiveModelGainsAndLatency();
  }
  if (mStagedStompModel != nullptr && (!inputStereoMode || mStagedStompModelRight != nullptr))
  {
    mStompModel = std::move(mStagedStompModel);
    mStagedStompModel = nullptr;
    mStompModelRight = std::move(mStagedStompModelRight);
    mStagedStompModelRight = nullptr;
    _SetStompCapabilityState(mStompModel->HasLoudness(), mStompModel->HasOutputLevel());
    _UpdateLatency();
  }
  if (mStagedIR != nullptr && (!inputStereoMode || mStagedIRChannel2 != nullptr))
  {
    mIR = std::move(mStagedIR);
    mStagedIR = nullptr;
    mIRChannel2 = std::move(mStagedIRChannel2);
    mStagedIRChannel2 = nullptr;
    mIRPath = mStagedIRPath;
    mStagedIRPath.Set("");
    triggerOutputDeClick = true;
  }
  if (mStagedIRRight != nullptr && (!inputStereoMode || mStagedIRRightChannel2 != nullptr))
  {
    mIRRight = std::move(mStagedIRRight);
    mStagedIRRight = nullptr;
    mIRRightChannel2 = std::move(mStagedIRRightChannel2);
    mStagedIRRightChannel2 = nullptr;
    mIRPathRight = mStagedIRPathRight;
    mStagedIRPathRight.Set("");
    triggerOutputDeClick = true;
  }

  if (triggerOutputDeClick)
    mAmpSwitchDeClickSamplesRemaining.store(kAmpSlotSwitchDeClickSamples, std::memory_order_relaxed);
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
  if (mStagedModelRight != nullptr)
  {
    mStagedModelRight->Reset(sampleRate, maxBlockSize);
  }
  else if (mModelRight != nullptr)
  {
    mModelRight->Reset(sampleRate, maxBlockSize);
  }
  if (mStagedStompModel != nullptr)
  {
    mStagedStompModel->Reset(sampleRate, maxBlockSize);
  }
  else if (mStompModel != nullptr)
  {
    mStompModel->Reset(sampleRate, maxBlockSize);
  }
  if (mStagedStompModelRight != nullptr)
  {
    mStagedStompModelRight->Reset(sampleRate, maxBlockSize);
  }
  else if (mStompModelRight != nullptr)
  {
    mStompModelRight->Reset(sampleRate, maxBlockSize);
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
      mStagedIRPath = mIRPath;
    }
  }
  if (mStagedIRChannel2 != nullptr)
  {
    const double irSampleRate = mStagedIRChannel2->GetSampleRate();
    if (irSampleRate != sampleRate)
    {
      const auto irData = mStagedIRChannel2->GetData();
      mStagedIRChannel2 = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
    }
  }
  else if (mIRChannel2 != nullptr)
  {
    const double irSampleRate = mIRChannel2->GetSampleRate();
    if (irSampleRate != sampleRate)
    {
      const auto irData = mIRChannel2->GetData();
      mStagedIRChannel2 = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
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
      mStagedIRPathRight = mIRPathRight;
    }
  }
  if (mStagedIRRightChannel2 != nullptr)
  {
    const double irSampleRate = mStagedIRRightChannel2->GetSampleRate();
    if (irSampleRate != sampleRate)
    {
      const auto irData = mStagedIRRightChannel2->GetData();
      mStagedIRRightChannel2 = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
    }
  }
  else if (mIRRightChannel2 != nullptr)
  {
    const double irSampleRate = mIRRightChannel2->GetSampleRate();
    if (irSampleRate != sampleRate)
    {
      const auto irData = mIRRightChannel2->GetData();
      mStagedIRRightChannel2 = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
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
    auto loadResampledModel = [this, &dspPath]() {
      std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);
      std::unique_ptr<ResamplingNAM> temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
      temp->Reset(GetSampleRate(), GetBlockSize());
      return temp;
    };

    auto stagedModel = loadResampledModel();
    auto stagedModelRight = loadResampledModel();
    _SetAmpSlotCapabilityState(
      slotIndex, (stagedModel != nullptr) && stagedModel->HasLoudness(),
      (stagedModel != nullptr) && stagedModel->HasOutputLevel());
    _RefreshModelCapabilityIndicators();
    // Publish stereo companion first; publish primary last to avoid half-swapped stereo state.
    mStagedModelRight = std::move(stagedModelRight);
    mStagedModel = std::move(stagedModel);
    _SetAmpSlotModelPath(slotIndex, modelPath);
    SendControlMsgFromDelegate(slotCtrlTag, kMsgTagLoadedModel, modelPath.GetLength(), modelPath.Get());
  }
  catch (std::runtime_error& e)
  {
    SendControlMsgFromDelegate(slotCtrlTag, kMsgTagLoadFailed);

    if (mStagedModel != nullptr)
    {
      mStagedModel = nullptr;
    }
    if (mStagedModelRight != nullptr)
    {
      mStagedModelRight = nullptr;
    }
    if (slotIndex == mCurrentModelSlot && mModel != nullptr)
      _SetAmpSlotCapabilityState(slotIndex, mModel->HasLoudness(), mModel->HasOutputLevel());
    else if (mAmpSlotModelCache[slotIndex] != nullptr)
      _SetAmpSlotCapabilityState(
        slotIndex, mAmpSlotModelCache[slotIndex]->HasLoudness(), mAmpSlotModelCache[slotIndex]->HasOutputLevel());
    else
      _ClearAmpSlotCapabilityState(slotIndex);
    _RefreshModelCapabilityIndicators();
    _SetAmpSlotModelPath(slotIndex, previousSlotPath);
    if (slotIndex == mAmpSelectorIndex)
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
    auto loadResampledStompModel = [this, &dspPath]() {
      std::unique_ptr<nam::DSP> model = nam::get_dsp(dspPath);
      std::unique_ptr<ResamplingNAM> temp = std::make_unique<ResamplingNAM>(std::move(model), GetSampleRate());
      temp->Reset(GetSampleRate(), GetBlockSize());
      return temp;
    };

    auto stagedStompModel = loadResampledStompModel();
    auto stagedStompModelRight = loadResampledStompModel();
    const bool hasLoudness = (stagedStompModel != nullptr) && stagedStompModel->HasLoudness();
    const bool hasCalibration = (stagedStompModel != nullptr) && stagedStompModel->HasOutputLevel();
    // Publish stereo companion first; publish primary last to avoid half-swapped stereo state.
    mStagedStompModelRight = std::move(stagedStompModelRight);
    mStagedStompModel = std::move(stagedStompModel);
    _SetStompCapabilityState(hasLoudness, hasCalibration);
    _RefreshModelCapabilityIndicators();
    mStompNAMPath = modelPath;
    SendControlMsgFromDelegate(
      kCtrlTagStompModelFileBrowser, kMsgTagLoadedStompModel, mStompNAMPath.GetLength(), mStompNAMPath.Get());
  }
  catch (std::runtime_error& e)
  {
    SendControlMsgFromDelegate(kCtrlTagStompModelFileBrowser, kMsgTagLoadFailed);

    if (mStagedStompModel != nullptr)
      mStagedStompModel = nullptr;
    if (mStagedStompModelRight != nullptr)
      mStagedStompModelRight = nullptr;

    if (mStompModel != nullptr)
      _SetStompCapabilityState(mStompModel->HasLoudness(), mStompModel->HasOutputLevel());
    else
      _ClearStompCapabilityState();
    _RefreshModelCapabilityIndicators();
    mStompNAMPath = previousNAMPath;
    std::cerr << "Failed to read stomp DSP module" << std::endl;
    std::cerr << e.what() << std::endl;
    return e.what();
  }
  return "";
}

dsp::wav::LoadReturnCode NeuralAmpModeler::_StageIRLeft(const WDL_String& irPath)
{
  const double sampleRate = GetSampleRate();
  dsp::wav::LoadReturnCode wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
  try
  {
    auto irPathU8 = std::filesystem::u8path(irPath.Get());
    auto stagedIR = std::make_unique<dsp::ImpulseResponse>(irPathU8.string().c_str(), sampleRate);
    wavState = stagedIR->GetWavState();
    if (wavState == dsp::wav::LoadReturnCode::SUCCESS)
    {
      const auto irData = stagedIR->GetData();
      auto stagedIRChannel2 = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
      // Publish stereo companion first; publish primary last to avoid half-swapped stereo state.
      mStagedIRChannel2 = std::move(stagedIRChannel2);
      mStagedIR = std::move(stagedIR);
      mStagedIRPath = irPath;
    }
  }
  catch (std::runtime_error& e)
  {
    wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
    std::cerr << "Caught unhandled exception while attempting to load IR:" << std::endl;
    std::cerr << e.what() << std::endl;
  }

  if (wavState == dsp::wav::LoadReturnCode::SUCCESS)
  {
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowserLeft, kMsgTagLoadedIRLeft, irPath.GetLength(), irPath.Get());
  }
  else
  {
    if (mStagedIR != nullptr)
    {
      mStagedIR = nullptr;
    }
    if (mStagedIRChannel2 != nullptr)
    {
      mStagedIRChannel2 = nullptr;
    }
    mStagedIRPath.Set("");
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowserLeft, kMsgTagLoadFailed);
  }

  return wavState;
}

dsp::wav::LoadReturnCode NeuralAmpModeler::_StageIRRight(const WDL_String& irPath)
{
  const double sampleRate = GetSampleRate();
  dsp::wav::LoadReturnCode wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
  try
  {
    auto irPathU8 = std::filesystem::u8path(irPath.Get());
    auto stagedIRRight = std::make_unique<dsp::ImpulseResponse>(irPathU8.string().c_str(), sampleRate);
    wavState = stagedIRRight->GetWavState();
    if (wavState == dsp::wav::LoadReturnCode::SUCCESS)
    {
      const auto irData = stagedIRRight->GetData();
      auto stagedIRRightChannel2 = std::make_unique<dsp::ImpulseResponse>(irData, sampleRate);
      // Publish stereo companion first; publish primary last to avoid half-swapped stereo state.
      mStagedIRRightChannel2 = std::move(stagedIRRightChannel2);
      mStagedIRRight = std::move(stagedIRRight);
      mStagedIRPathRight = irPath;
    }
  }
  catch (std::runtime_error& e)
  {
    wavState = dsp::wav::LoadReturnCode::ERROR_OTHER;
    std::cerr << "Caught unhandled exception while attempting to load right IR:" << std::endl;
    std::cerr << e.what() << std::endl;
  }

  if (wavState == dsp::wav::LoadReturnCode::SUCCESS)
  {
    SendControlMsgFromDelegate(
      kCtrlTagIRFileBrowserRight, kMsgTagLoadedIRRight, irPath.GetLength(), irPath.Get());
  }
  else
  {
    if (mStagedIRRight != nullptr)
      mStagedIRRight = nullptr;
    if (mStagedIRRightChannel2 != nullptr)
      mStagedIRRightChannel2 = nullptr;
    mStagedIRPathRight.Set("");
    SendControlMsgFromDelegate(kCtrlTagIRFileBrowserRight, kMsgTagLoadFailed);
  }

  return wavState;
}

size_t NeuralAmpModeler::_GetBufferNumChannels() const
{
  // Assumes input and output internal buses use the same channel count.
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
  for (auto& toneStack : mToneStacks)
    toneStack = std::make_unique<dsp::tone_stack::BasicNamToneStack>();
}
bool NeuralAmpModeler::_PrepareBuffers(const size_t numChannels, const size_t numFrames, const bool allowGrowth)
{
  const bool updateChannels = numChannels != _GetBufferNumChannels();
  const bool growFrames = updateChannels || (_GetBufferNumFrames() < numFrames);

  if (!allowGrowth && (updateChannels || growFrames))
    return false;

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
  return true;
}

void NeuralAmpModeler::_PrepareIOPointers(const size_t numChannels)
{
  _DeallocateIOPointers();
  _AllocateIOPointers(numChannels);
}

void NeuralAmpModeler::_ProcessInput(iplug::sample** inputs, const size_t nFrames, const size_t nChansIn,
                                     const size_t nChansOut)
{
  // Mono or dual-mono core ingest for the amp chain.
  if (nChansOut < 1)
    return;
  if (inputs == nullptr)
    return;
  if (nChansIn == 0)
    return;

  if (nChansOut == 1)
  {
    // Mono mode: use input 1 only (ignore input 2).
    sample* monoInput = inputs[0];
    if (monoInput == nullptr)
    {
      for (size_t c = 1; c < nChansIn; ++c)
      {
        if (inputs[c] != nullptr)
        {
          monoInput = inputs[c];
          break;
        }
      }
    }
    if (monoInput == nullptr)
      return;

    const double gain = mInputGain;
    for (size_t s = 0; s < nFrames; ++s)
      mInputArray[0][s] = gain * monoInput[s];
    return;
  }

  // Stereo mode: L follows input 1 mapping, R follows input 2 mapping if available.
  const double gain = mInputGain;
  sample* inputLeft = (nChansIn > 0) ? inputs[0] : nullptr;
  sample* inputRight = (nChansIn > 1) ? inputs[1] : nullptr;
  if (inputLeft == nullptr)
  {
    for (size_t c = 1; c < nChansIn; ++c)
    {
      if (inputs[c] != nullptr)
      {
        inputLeft = inputs[c];
        break;
      }
    }
  }
  if (inputLeft == nullptr)
    return;

  for (size_t s = 0; s < nFrames; ++s)
  {
    mInputArray[0][s] = gain * inputLeft[s];
    mInputArray[1][s] = (inputRight != nullptr) ? static_cast<sample>(gain * inputRight[s]) : 0.0f;
  }
}

void NeuralAmpModeler::_ProcessOutput(iplug::sample** inputs, iplug::sample** outputs, const size_t nFrames,
                                      const size_t nChansIn, const size_t nChansOut)
{
  if (outputs == nullptr)
    return;
  const double gain = mOutputGain;

  auto writeSample = [gain](sample inputSample) {
    double outputSample = gain * static_cast<double>(inputSample);
    if (!std::isfinite(outputSample))
    {
      outputSample = 0.0;
    }
#ifdef APP_API // Ensure valid output to interface
    return static_cast<sample>(std::clamp(outputSample, -1.0, 1.0));
#else // In a DAW, other things may come next and should be able to handle large
      // values.
    return static_cast<sample>(outputSample);
#endif
  };

  // Assume _PrepareBuffers() was already called
  if (nChansIn == 0 || inputs == nullptr || inputs[0] == nullptr)
  {
    for (size_t cout = 0; cout < nChansOut; ++cout)
    {
      if (outputs[cout] == nullptr)
        continue;
      for (size_t s = 0; s < nFrames; ++s)
        outputs[cout][s] = 0.0;
    }
    return;
  }

  if (nChansIn == 1)
  {
    // Broadcast internal mono stream to all output channels.
    for (size_t cout = 0; cout < nChansOut; ++cout)
    {
      if (outputs[cout] == nullptr)
        continue;
      for (size_t s = 0; s < nFrames; ++s)
        outputs[cout][s] = writeSample(inputs[0][s]);
    }
    return;
  }

  if (nChansOut == 1)
  {
    if (outputs[0] == nullptr)
      return;

    // Downmix internal stereo bus to mono output.
    for (size_t s = 0; s < nFrames; ++s)
    {
      double mix = 0.0;
      size_t activeChannels = 0;
      for (size_t cin = 0; cin < nChansIn; ++cin)
      {
        if (inputs[cin] == nullptr)
          continue;
        mix += inputs[cin][s];
        ++activeChannels;
      }
      const sample monoSample = (activeChannels > 0) ? static_cast<sample>(mix / static_cast<double>(activeChannels)) : 0.0f;
      outputs[0][s] = writeSample(monoSample);
    }
    return;
  }

  // Map each output channel to matching internal channel (wrap for extra outputs).
  for (size_t cout = 0; cout < nChansOut; ++cout)
  {
    if (outputs[cout] == nullptr)
      continue;
    const size_t cin = cout % nChansIn;
    if (inputs[cin] == nullptr)
    {
      for (size_t s = 0; s < nFrames; ++s)
        outputs[cout][s] = 0.0;
      continue;
    }
    for (size_t s = 0; s < nFrames; ++s)
      outputs[cout][s] = writeSample(inputs[cin][s]);
  }
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
  }
  _RefreshOutputModeControlSupport();
}

void NeuralAmpModeler::_RefreshOutputModeControlSupport()
{
  if (auto* pGraphics = GetUI())
  {
    auto* outputModeControl = dynamic_cast<OutputModeControl*>(pGraphics->GetControlWithTag(kCtrlTagOutputMode));
    if (outputModeControl == nullptr)
      return;

    const bool normalizedSupported = (mModel != nullptr) && mModel->HasLoudness();
    const bool calibratedSupported = (mModel != nullptr) && mModel->HasOutputLevel();
    outputModeControl->SetNormalizedDisable(!normalizedSupported);
    outputModeControl->SetCalibratedDisable(!calibratedSupported);
  }
}

void NeuralAmpModeler::_SetAmpSlotCapabilityState(const int slotIndex, const bool hasLoudness, const bool hasCalibration)
{
  const int clampedSlot = std::clamp(slotIndex, 0, static_cast<int>(mAmpSlotHasLoudness.size()) - 1);
  mAmpSlotHasLoudness[clampedSlot].store(hasLoudness, std::memory_order_relaxed);
  mAmpSlotHasCalibration[clampedSlot].store(hasCalibration, std::memory_order_relaxed);
}

void NeuralAmpModeler::_ClearAmpSlotCapabilityState(const int slotIndex)
{
  _SetAmpSlotCapabilityState(slotIndex, false, false);
}

void NeuralAmpModeler::_SetStompCapabilityState(const bool hasLoudness, const bool hasCalibration)
{
  mStompHasLoudness.store(hasLoudness, std::memory_order_relaxed);
  mStompHasCalibration.store(hasCalibration, std::memory_order_relaxed);
}

void NeuralAmpModeler::_ClearStompCapabilityState()
{
  _SetStompCapabilityState(false, false);
}

void NeuralAmpModeler::_RefreshModelCapabilityIndicators()
{
  if (auto* pGraphics = GetUI())
  {
    auto updateCheck = [pGraphics](const int ctrlTag, const bool checked) {
      auto* checkControl = dynamic_cast<NAMReadOnlyCheckboxControl*>(pGraphics->GetControlWithTag(ctrlTag));
      if (checkControl != nullptr)
        checkControl->SetChecked(checked);
    };

    updateCheck(kCtrlTagAmp1HasLoudness, mAmpSlotHasLoudness[0].load(std::memory_order_relaxed));
    updateCheck(kCtrlTagAmp1HasCalibration, mAmpSlotHasCalibration[0].load(std::memory_order_relaxed));
    updateCheck(kCtrlTagAmp2HasLoudness, mAmpSlotHasLoudness[1].load(std::memory_order_relaxed));
    updateCheck(kCtrlTagAmp2HasCalibration, mAmpSlotHasCalibration[1].load(std::memory_order_relaxed));
    updateCheck(kCtrlTagAmp3HasLoudness, mAmpSlotHasLoudness[2].load(std::memory_order_relaxed));
    updateCheck(kCtrlTagAmp3HasCalibration, mAmpSlotHasCalibration[2].load(std::memory_order_relaxed));
    updateCheck(kCtrlTagStompHasLoudness, mStompHasLoudness.load(std::memory_order_relaxed));
    updateCheck(kCtrlTagStompHasCalibration, mStompHasCalibration.load(std::memory_order_relaxed));
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
  if (nFrames == 0)
    return;

  // Right now, we didn't specify MAXNC when we initialized these, so it's 1.
  const int nChansHack = 1;
  if (nChansIn > 0 && inputPointer != nullptr && inputPointer[0] != nullptr)
    mInputSender.ProcessBlock(inputPointer, (int)nFrames, kCtrlTagInputMeter, nChansHack);
  if (nChansOut > 0 && outputPointer != nullptr && outputPointer[0] != nullptr)
    mOutputSender.ProcessBlock(outputPointer, (int)nFrames, kCtrlTagOutputMeter, nChansHack);
}

// HACK
#include "Unserialization.cpp"
