#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include "../AudioDSPTools/dsp/ImpulseResponse.h"
#include "../AudioDSPTools/dsp/NoiseGate.h"
#include "../AudioDSPTools/dsp/dsp.h"
#include "../AudioDSPTools/dsp/wav.h"
#include "../AudioDSPTools/dsp/ResamplingContainer/ResamplingContainer.h"
#include "../NeuralAmpModelerCore/NAM/dsp.h"

#include "Colors.h"
#include "TunerAnalyzer.h"
#include "ToneStack.h"
#include "TransposeShifter.h"

#include "IPlug_include_in_plug_hdr.h"
#include "ISender.h"


const int kNumPresets = 1;
// Mono core path with stereo-capable post-cab processing/output bus.
constexpr size_t kNumChannelsInternal = 2;

class NAMSender : public iplug::IPeakAvgSender<>
{
public:
  NAMSender()
  : iplug::IPeakAvgSender<>(-90.0, true, 5.0f, 1.0f, 300.0f, 500.0f)
  {
  }
};

enum EParams
{
  // Keep parameter ordering stable for serialization compatibility.
  kInputLevel = 0,
  kNoiseGateThreshold,
  kToneBass,
  kToneMid,
  kToneTreble,
  kOutputLevel,
  // The rest can be appended.
  kNoiseGateActive,
  kEQActive,
  kIRToggle,
  // Input calibration
  kCalibrateInput,
  kInputCalibrationLevel,
  kOutputMode,
  // Post-cab filters
  kUserHPFFrequency,
  kUserLPFFrequency,
  // Cab IR blend (append-only to preserve old serialization order)
  kCabIRBlend,
  // Model on/off (bypass model stage when off)
  kModelToggle,
  // Gain trim directly before the model stage
  kPreModelGain,
  // Additional amp-style tone controls
  kTonePresence,
  kToneDepth,
  // Master volume before IR section
  kMasterVolume,
  // Tuner mode (bypass amp/cab path when active)
  kTunerActive,
  // Tuner monitor mode while active: Mute / Bypass / Full
  kTunerMonitorMode,
  // Input transpose in semitone steps (-8..+8)
  kTransposeSemitones,
  // Gate release time in milliseconds
  kNoiseGateReleaseMs,
  // Boost pedal section
  kStompBoostLevel,
  kStompBoostActive,
  // FX section (append-only)
  kFXEQActive,
  kFXEQBand31Hz,
  kFXEQBand62Hz,
  kFXEQBand125Hz,
  kFXEQBand250Hz,
  kFXEQBand500Hz,
  kFXEQBand1kHz,
  kFXEQBand2kHz,
  kFXEQBand4kHz,
  kFXEQBand8kHz,
  kFXEQBand16kHz,
  kFXDelayActive,
  kFXDelayMix,
  kFXDelayTimeMs,
  kFXDelayFeedback,
  kFXReverbActive,
  kFXReverbMix,
  kFXReverbDecay,
  kFXReverbPreDelayMs,
  kFXReverbTone,
  kFXEQOutputGain,
  kFXDelayLowCutHz,
  kFXDelayHighCutHz,
  kFXReverbLowCutHz,
  kFXReverbHighCutHz,
  // Input mode: mono uses only input 1, stereo uses input 1+2.
  kInputStereoMode,
  kNumParams
};

const int numKnobs = 8;

enum ECtrlTags
{
  kCtrlTagModelFileBrowser = 0,
  kCtrlTagModelFileBrowser2,
  kCtrlTagModelFileBrowser3,
  kCtrlTagIRFileBrowserLeft,
  kCtrlTagIRFileBrowserRight,
  kCtrlTagIRToggle,
  kCtrlTagNoiseGateLED,
  kCtrlTagInputMeter,
  kCtrlTagOutputMeter,
  kCtrlTagSettingsBox,
  kCtrlTagOutputMode,
  kCtrlTagCalibrateInput,
  kCtrlTagInputCalibrationLevel,
  kCtrlTagTunerReadout,
  kCtrlTagTunerMute,
  kCtrlTagTunerClose,
  kCtrlTagTopNavAmp,
  kCtrlTagTopNavStomp,
  kCtrlTagTopNavCab,
  kCtrlTagTopNavFx,
  kCtrlTagTopNavTuner,
  kCtrlTagAmpSlot1,
  kCtrlTagAmpSlot2,
  kCtrlTagAmpSlot3,
  kCtrlTagPresetLabel,
  kCtrlTagMainBackground,
  kCtrlTagStompModelFileBrowser,
  kCtrlTagGateOnLED,
  kCtrlTagBoostOnLED,
  kCtrlTagFXEQOnLED,
  kCtrlTagFXDelayOnLED,
  kCtrlTagFXReverbOnLED,
  kCtrlTagAmp1HasLoudness,
  kCtrlTagAmp1HasCalibration,
  kCtrlTagAmp2HasLoudness,
  kCtrlTagAmp2HasCalibration,
  kCtrlTagAmp3HasLoudness,
  kCtrlTagAmp3HasCalibration,
  kCtrlTagStompHasLoudness,
  kCtrlTagStompHasCalibration,
  kNumCtrlTags
};

enum EMsgTags
{
  // These tags are used from UI -> DSP
  kMsgTagClearModel = 0,
  kMsgTagClearStompModel,
  kMsgTagClearIRLeft,
  kMsgTagClearIRRight,
  kMsgTagHighlightColor,
  // The following tags are from DSP -> UI
  kMsgTagLoadFailed,
  kMsgTagLoadedModel,
  kMsgTagLoadedStompModel,
  kMsgTagLoadedIRLeft,
  kMsgTagLoadedIRRight,
  kNumMsgTags
};

// Get the sample rate of a NAM model.
// Sometimes, the model doesn't know its own sample rate; this wrapper guesses 48k based on the way that most
// people have used NAM in the past.
double GetNAMSampleRate(const std::unique_ptr<nam::DSP>& model)
{
  // Some models are from when we didn't have sample rate in the model.
  // For those, this wraps with the assumption that they're 48k models, which is probably true.
  const double assumedSampleRate = 48000.0;
  const double reportedEncapsulatedSampleRate = model->GetExpectedSampleRate();
  const double encapsulatedSampleRate =
    reportedEncapsulatedSampleRate <= 0.0 ? assumedSampleRate : reportedEncapsulatedSampleRate;
  return encapsulatedSampleRate;
};

class ResamplingNAM : public nam::DSP
{
public:
  // Resampling wrapper around the NAM models
  ResamplingNAM(std::unique_ptr<nam::DSP> encapsulated, const double expected_sample_rate)
  : nam::DSP(1, 1, expected_sample_rate)
  , mEncapsulated(std::move(encapsulated))
  , mResampler(GetNAMSampleRate(mEncapsulated))
  {
    // Get the other information from the encapsulated NAM so that we can tell the outside world about what we're
    // holding.
    if (mEncapsulated->HasLoudness())
    {
      SetLoudness(mEncapsulated->GetLoudness());
    }
    if (mEncapsulated->HasInputLevel())
    {
      SetInputLevel(mEncapsulated->GetInputLevel());
    }
    if (mEncapsulated->HasOutputLevel())
    {
      SetOutputLevel(mEncapsulated->GetOutputLevel());
    }

    // NOTE: prewarm samples doesn't mean anything--we can prewarm the encapsulated model as it likes and be good to
    // go.
    // _prewarm_samples = 0;

    // And be ready
    int maxBlockSize = 2048; // Conservative
    Reset(expected_sample_rate, maxBlockSize);
  };

  ~ResamplingNAM() = default;

  void prewarm() override { mEncapsulated->prewarm(); };

  void process(NAM_SAMPLE** input, NAM_SAMPLE** output, const int num_frames) override
  {
    if (num_frames > mMaxExternalBlockSize)
    {
      // Fail safe instead of throwing in the real-time path.
      for (int i = 0; i < num_frames; ++i)
        output[0][i] = input[0][i];
      return;
    }

    if (!NeedToResample())
    {
      mEncapsulated->process(input, output, num_frames);
    }
    else
    {
      mResampler.ProcessBlock(input, output, num_frames, [&](NAM_SAMPLE** in, NAM_SAMPLE** out, int numFrames) {
        mEncapsulated->process(in, out, numFrames);
      });
    }
  };

  int GetLatency() const { return NeedToResample() ? mResampler.GetLatency() : 0; };

  void Reset(const double sampleRate, const int maxBlockSize) override
  {
    mExpectedSampleRate = sampleRate;
    mMaxExternalBlockSize = maxBlockSize;
    mResampler.Reset(sampleRate, maxBlockSize);

    // Allocations in the encapsulated model (HACK)
    // Stolen some code from the resampler; it'd be nice to have these exposed as methods? :)
    const double mUpRatio = sampleRate / GetEncapsulatedSampleRate();
    const auto maxEncapsulatedBlockSize = static_cast<int>(std::ceil(static_cast<double>(maxBlockSize) / mUpRatio));
    mEncapsulated->ResetAndPrewarm(sampleRate, maxEncapsulatedBlockSize);
  };

  // So that we can let the world know if we're resampling (useful for debugging)
  double GetEncapsulatedSampleRate() const { return GetNAMSampleRate(mEncapsulated); };

private:
  bool NeedToResample() const { return GetExpectedSampleRate() != GetEncapsulatedSampleRate(); };
  // The encapsulated NAM
  std::unique_ptr<nam::DSP> mEncapsulated;

  // The resampling wrapper
  dsp::ResamplingContainer<NAM_SAMPLE, 1, 12> mResampler;

  // Used to check that we don't get too large a block to process.
  int mMaxExternalBlockSize = 0;
};

class NeuralAmpModeler final : public iplug::Plugin
{
public:
  NeuralAmpModeler(const iplug::InstanceInfo& info);
  ~NeuralAmpModeler();

  void ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames) override;
  void OnReset() override;
  void OnIdle() override;

  bool SerializeState(iplug::IByteChunk& chunk) const override;
  int UnserializeState(const iplug::IByteChunk& chunk, int startPos) override;
  void OnUIOpen() override;
  bool OnHostRequestingSupportedViewConfiguration(int width, int height) override { return true; }

  void OnParamChange(int paramIdx) override;
  void OnParamChangeUI(int paramIdx, iplug::EParamSource source) override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

private:
  struct AmpSlotState
  {
    double modelToggle = 0.0;
    bool modelToggleTouched = false;
    double toneStackActive = 1.0;
    double preModelGain = 0.0;
    double bass = 5.0;
    double mid = 5.0;
    double treble = 5.0;
    double presence = 5.0;
    double depth = 5.0;
    double master = 5.0;
  };

  enum class TopNavSection : int
  {
    Amp = 0,
    Stomp,
    Cab,
    Fx,
    Tuner,
    Count
  };

  // Allocates mInputPointers and mOutputPointers
  void _AllocateIOPointers(const size_t nChans);
  // Moves DSP modules from staging area to the main area.
  // Also deletes DSP modules that are flagged for removal.
  // Exists so that we don't try to use a DSP module that's only
  // partially-instantiated.
  void _ApplyDSPStaging();
  // Deallocates mInputPointers and mOutputPointers
  void _DeallocateIOPointers();
  // Fallback that just copies inputs to outputs if mDSP doesn't hold a model.
  void _FallbackDSP(iplug::sample** inputs, iplug::sample** outputs, const size_t numChannels, const size_t numFrames);
  // Sizes based on mInputArray
  size_t _GetBufferNumChannels() const;
  size_t _GetBufferNumFrames() const;
  void _InitToneStack();
  // Loads a NAM model and stores it to mStagedNAM
  // Returns an empty string on success, or an error message on failure.
  std::string _StageModel(const WDL_String& dspFile, int slotIndex, int slotCtrlTag);
  int _GetAmpModelCtrlTagForSlot(int slotIndex) const;
  int _GetAmpSlotForModelCtrlTag(int ctrlTag) const;
  void _SelectAmpSlot(int slotIndex);
  // Loads left cab IR and stores it to mStagedIR.
  // Return status code so that error messages can be relayed if
  // it wasn't successful.
  std::string _StageStompModel(const WDL_String& dspFile);
  // Loads left cab IR and stores it to mStagedIR.
  // Return status code so that error messages can be relayed if
  // it wasn't successful.
  dsp::wav::LoadReturnCode _StageIRLeft(const WDL_String& irPath);
  // Loads right cab IR and stores it to mStagedIRRight.
  dsp::wav::LoadReturnCode _StageIRRight(const WDL_String& irPath);

  bool _HaveModel() const { return this->mModel != nullptr; };
  // Prepare the input & output buffers
  bool _PrepareBuffers(const size_t numChannels, const size_t numFrames, const bool allowGrowth);
  // Manage pointers
  void _PrepareIOPointers(const size_t nChans);
  // Copy the input buffer to the object, applying input level.
  // :param nChansIn: In from external
  // :param nChansOut: Out to the internal of the DSP routine
  void _ProcessInput(iplug::sample** inputs, const size_t nFrames, const size_t nChansIn, const size_t nChansOut);
  // Copy the output to the output buffer, applying output level.
  // :param nChansIn: In from internal
  // :param nChansOut: Out to external
  void _ProcessOutput(iplug::sample** inputs, iplug::sample** outputs, const size_t nFrames, const size_t nChansIn,
                      const size_t nChansOut);
  // Resetting for models and IRs, called by OnReset
  void _ResetModelAndIR(const double sampleRate, const int maxBlockSize);

  void _SetInputGain();
  void _SetOutputGain();
  void _SetMasterGain();

  // See: Unserialization.cpp
  void _UnserializeApplyConfig(nlohmann::json& config);
  // 0.7.9 and later
  int _UnserializeStateWithKnownVersion(const iplug::IByteChunk& chunk, int startPos);
  // Hopefully 0.7.3-0.7.8, but no gurantees
  int _UnserializeStateWithUnknownVersion(const iplug::IByteChunk& chunk, int startPos);

  // Update all controls that depend on a model
  void _UpdateControlsFromModel();
  void _RefreshOutputModeControlSupport();
  void _RefreshModelCapabilityIndicators();
  void _SetAmpSlotCapabilityState(int slotIndex, bool hasLoudness, bool hasCalibration);
  void _ClearAmpSlotCapabilityState(int slotIndex);
  void _SetStompCapabilityState(bool hasLoudness, bool hasCalibration);
  void _ClearStompCapabilityState();
  // Top icon-strip state handling
  void _SetTopNavActiveSection(TopNavSection section);
  void _ToggleTopNavSectionBypass(TopNavSection section);
  void _RefreshTopNavControls();
  void _SyncTunerParamToTopNav();
  void _CaptureAmpSlotState(int slotIndex);
  void _ApplyAmpSlotState(int slotIndex);
  void _ApplyAmpSlotStateToToneStack(int slotIndex);
  void _ApplyCurrentAmpParamsToActiveToneStack();
  bool _IsAmpSlotManagedParam(int paramIdx) const;
  void _RequestModelLoadForSlot(const WDL_String& modelPath, int slotIndex, int slotCtrlTag);
  void _ModelLoadWorkerLoop();
  void _StartModelLoadWorker();
  void _StopModelLoadWorker();
  void _UpdatePresetLabel();
  void _RefreshStandalonePresetList();
  bool _LoadStandalonePresetFromFile(const WDL_String& filePath);
  bool _LoadDefaultPreset();
  bool _SaveStandalonePresetToFile(const WDL_String& filePath);
  void _PromptStandalonePresetSaveAs();
  void _PromptStandalonePresetRename();
  void _PromptStandalonePresetDelete();
  void _SelectStandalonePresetRelative(int delta);
  void _ShowStandalonePresetMenu(const iplug::igraphics::IRECT& anchorArea);
  bool _IsStandaloneFactoryPresetPath(const WDL_String& filePath) const;
  void _SetStandalonePresetDirty(bool isDirty);
  void _MarkStandalonePresetDirty();
  void _ApplyInputStereoAutoDefaultIfNeeded();

  // Make sure that the latency is reported correctly.
  void _UpdateLatency();

  // Update level meters
  // Called within ProcessBlock().
  // Assume _ProcessInput() and _ProcessOutput() were run immediately before.
  void _UpdateMeters(iplug::sample** inputPointer, iplug::sample** outputPointer, const size_t nFrames,
                     const size_t nChansIn, const size_t nChansOut);

  // Member data

  // Input arrays to NAM
  std::vector<std::vector<iplug::sample>> mInputArray;
  // Output from NAM
  std::vector<std::vector<iplug::sample>> mOutputArray;
  // Pointer versions
  iplug::sample** mInputPointers = nullptr;
  iplug::sample** mOutputPointers = nullptr;

  // Input and output gain
  double mInputGain = 1.0;
  double mOutputGain = 1.0;
  double mMasterGain = 1.0;

  // Noise gates
  dsp::noise_gate::Trigger mNoiseGateTrigger;
  dsp::noise_gate::Gain mNoiseGateGain;
  // The model actually being used:
  std::unique_ptr<ResamplingNAM> mModel;
  // Plugin stereo core: right-channel model instance (independent state).
  std::unique_ptr<ResamplingNAM> mModelRight;
  // Per-slot ready model bank (inactive slots + pending active swap source).
  std::array<std::unique_ptr<ResamplingNAM>, 3> mAmpSlotModelCache;
  std::array<std::unique_ptr<ResamplingNAM>, 3> mAmpSlotModelCacheRight;
  // Worker->audio lock-free handoff (raw pointers exchanged atomically).
  std::array<std::atomic<ResamplingNAM*>, 3> mPendingLoadedSlotModel;
  std::array<std::atomic<ResamplingNAM*>, 3> mPendingLoadedSlotModelRight;
  std::array<std::atomic<uint64_t>, 3> mPendingLoadedSlotRequestId;
  std::unique_ptr<ResamplingNAM> mStompModel;
  // Plugin stereo core: right-channel stomp model instance (independent state).
  std::unique_ptr<ResamplingNAM> mStompModelRight;
  // And the IR
  std::unique_ptr<dsp::ImpulseResponse> mIR;
  // Stereo core: right-channel state for left IR.
  std::unique_ptr<dsp::ImpulseResponse> mIRChannel2;
  std::unique_ptr<dsp::ImpulseResponse> mIRRight;
  // Stereo core: right-channel state for right IR.
  std::unique_ptr<dsp::ImpulseResponse> mIRRightChannel2;
  // Manages switching what DSP is being used.
  std::unique_ptr<ResamplingNAM> mStagedModel;
  std::unique_ptr<ResamplingNAM> mStagedModelRight;
  std::unique_ptr<ResamplingNAM> mStagedStompModel;
  std::unique_ptr<ResamplingNAM> mStagedStompModelRight;
  std::unique_ptr<dsp::ImpulseResponse> mStagedIR;
  std::unique_ptr<dsp::ImpulseResponse> mStagedIRChannel2;
  std::unique_ptr<dsp::ImpulseResponse> mStagedIRRight;
  std::unique_ptr<dsp::ImpulseResponse> mStagedIRRightChannel2;
  WDL_String mStagedIRPath;
  WDL_String mStagedIRPathRight;
  // Flags to take away the modules at a safe time.
  std::atomic<bool> mShouldRemoveModel = false;
  std::array<std::atomic<bool>, 3> mShouldRemoveModelSlot;
  std::atomic<bool> mShouldRemoveStompModel = false;
  std::atomic<bool> mShouldRemoveIRLeft = false;
  std::atomic<bool> mShouldRemoveIRRight = false;

  std::atomic<bool> mNewModelLoadedInDSP = false;
  std::atomic<bool> mModelCleared = false;
  std::atomic<bool> mNoiseGateIsAttenuating = false;
  bool mNoiseGateLEDState = false;
  // Active model ownership slot is updated on audio thread in _ApplyDSPStaging().
  int mCurrentModelSlot = 1;
  std::atomic<int> mPendingAmpSlotSwitch{-1};
  TopNavSection mTopNavActiveSection = TopNavSection::Amp;
  std::array<bool, static_cast<size_t>(TopNavSection::Count)> mTopNavBypassed = {false, false, false, false, false};
  int mAmpSelectorIndex = 1;
  bool mApplyingAmpSlotState = false;
  bool mStartupDefaultLoadAttempted = false;
  bool mStandaloneStateLoadAttempted = false;
  bool mStateRestoredFromChunk = false;
  bool mInputStereoAutoDefaultApplied = false;
  std::vector<WDL_String> mStandaloneUserPresetPaths;
  std::vector<WDL_String> mStandaloneFactoryPresetPaths;
  std::vector<WDL_String> mStandalonePresetPaths;
  int mStandalonePresetIndex = -1;
  WDL_String mStandalonePresetFilePath;
  bool mStandalonePresetDirty = false;
  bool mStandalonePresetNameEntryReopenPending = false;
  WDL_String mStandalonePresetNameEntryPendingText;
  // Dynamic stereo-core optimization: collapse to mono only for sustained near-identical L/R input.
  bool mEffectiveMonoCollapseActive = false;
  size_t mEffectiveMonoCandidateSamples = 0;
  size_t mEffectiveStereoCandidateSamples = 0;
  // Stereo CPU optimization: bypass heavy per-side stages when one side is sustained silent.
  std::array<bool, kNumChannelsInternal> mStereoSideBypassActive = {};
  std::array<size_t, kNumChannelsInternal> mStereoSideSilentCandidateSamples = {};
  std::array<size_t, kNumChannelsInternal> mStereoSideActiveCandidateSamples = {};
  std::array<int, kNumChannelsInternal> mStereoSideResumeDeClickSamplesRemaining = {};
  std::array<double, kNumChannelsInternal> mStereoSideResumePrevSample = {};
  bool mDefaultPresetActive = true;
  bool mLoadingDefaultPreset = false;
  bool mDefaultPresetPostLoadSyncPending = false;
  bool mDefaultPresetCapturedFromStartup = false;
  bool mHasDefaultPresetState = false;
  iplug::IByteChunk mDefaultPresetStateChunk;
  iplug::igraphics::IPopupMenu mStandalonePresetMenu;
  std::array<AmpSlotState, 3> mAmpSlotStates = {};
  std::array<std::atomic<bool>, 3> mAmpSlotHasLoudness;
  std::array<std::atomic<bool>, 3> mAmpSlotHasCalibration;
  std::atomic<bool> mStompHasLoudness{false};
  std::atomic<bool> mStompHasCalibration{false};
  // 0=Empty, 1=Loading, 2=Ready, 3=Failed.
  std::array<std::atomic<int>, 3> mAmpSlotModelState;
  std::array<std::atomic<int>, 3> mSlotLoadUIEvent;
  std::array<std::atomic<uint64_t>, 3> mSlotLoadRequestId;
  WDL_String mLastSentIRPath;
  WDL_String mLastSentIRPathRight;
  struct ModelLoadJob
  {
    int slotIndex = 0;
    uint64_t requestId = 0;
    WDL_String modelPath;
    double sampleRate = 48000.0;
    int blockSize = 64;
  };
  std::thread mModelLoadWorker;
  std::mutex mModelLoadMutex;
  std::condition_variable mModelLoadCV;
  std::deque<ModelLoadJob> mModelLoadJobs;
  bool mModelLoadWorkerExit = false;
  std::atomic<int> mAmpSwitchDeClickSamplesRemaining = 0;
  std::array<double, kNumChannelsInternal> mAmpSwitchDeClickPrevSample = {};
  TunerAnalyzer mTunerAnalyzer;
  LightweightTransposeShifter mTransposeShifter;
  LightweightTransposeShifter mTransposeShifterRight;

  // Tone stack modules
  std::array<std::unique_ptr<dsp::tone_stack::AbstractToneStack>, 3> mToneStacks;

  // Post-IR filters
  recursive_linear_filter::HighPass mUserHighPass1;
  recursive_linear_filter::HighPass mUserHighPass2;
  recursive_linear_filter::LowPass mUserLowPass1;
  recursive_linear_filter::LowPass mUserLowPass2;
  // Post-IR FX EQ (10-band peaking cascade, stereo internal bus)
  std::array<double, 10> mFXEQB0 = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
  std::array<double, 10> mFXEQB1 = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  std::array<double, 10> mFXEQB2 = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  std::array<double, 10> mFXEQA1 = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  std::array<double, 10> mFXEQA2 = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  std::array<double, 10> mFXEQSmoothedGainDB = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  double mFXEQSmoothedOutputGain = 1.0;
  std::array<std::array<double, 10>, kNumChannelsInternal> mFXEQZ1 = {};
  std::array<std::array<double, 10>, kNumChannelsInternal> mFXEQZ2 = {};
  // Post-IR FX delay (preallocated in OnReset, no allocations in audio thread)
  std::array<std::vector<iplug::sample>, kNumChannelsInternal> mFXDelayBuffer;
  size_t mFXDelayBufferSamples = 1;
  size_t mFXDelayWriteIndex = 0;
  double mFXDelaySmoothedTimeSamples = 1.0;
  double mFXDelaySmoothedFeedback = 0.0;
  double mFXDelaySmoothedMix = 0.0;
  double mFXDelaySmoothedLowCutHz = 20.0;
  double mFXDelaySmoothedHighCutHz = 20000.0;
  std::array<double, kNumChannelsInternal> mFXDelayLowCutLPState = {};
  std::array<double, kNumChannelsInternal> mFXDelayHighCutLPState = {};
  // Post-IR FX reverb (preallocated in OnReset, no allocations in audio thread)
  std::array<std::vector<iplug::sample>, kNumChannelsInternal> mFXReverbPreDelayBuffer;
  size_t mFXReverbPreDelayBufferSamples = 1;
  size_t mFXReverbPreDelayWriteIndex = 0;
  std::array<std::array<size_t, 8>, 2> mFXReverbEarlyTapSamples = {};
  std::array<std::array<std::vector<iplug::sample>, 2>, kNumChannelsInternal> mFXReverbPreDiffAllpassBuffer;
  std::array<std::array<size_t, 2>, kNumChannelsInternal> mFXReverbPreDiffAllpassWriteIndex = {};
  std::array<std::array<size_t, 2>, kNumChannelsInternal> mFXReverbPreDiffAllpassDelaySamples = {};
  std::array<std::array<std::vector<iplug::sample>, 8>, kNumChannelsInternal> mFXReverbCombBuffer;
  std::array<std::array<size_t, 8>, kNumChannelsInternal> mFXReverbCombWriteIndex = {};
  std::array<std::array<size_t, 8>, kNumChannelsInternal> mFXReverbCombDelaySamples = {};
  std::array<std::array<double, 8>, kNumChannelsInternal> mFXReverbCombDampState = {};
  std::array<double, 8> mFXReverbCombModPhase = {0.0, 0.78, 1.57, 2.35, 3.14, 3.93, 4.71, 5.50};
  std::array<std::array<std::vector<iplug::sample>, 2>, kNumChannelsInternal> mFXReverbAllpassBuffer;
  std::array<std::array<size_t, 2>, kNumChannelsInternal> mFXReverbAllpassWriteIndex = {};
  std::array<std::array<size_t, 2>, kNumChannelsInternal> mFXReverbAllpassDelaySamples = {};
  std::array<double, kNumChannelsInternal> mFXReverbToneState = {};
  std::array<double, kNumChannelsInternal> mFXReverbEarlyToneState = {};
  double mFXReverbSmoothedMix = 0.0;
  double mFXReverbSmoothedDecaySeconds = 1.8;
  double mFXReverbSmoothedPreDelaySamples = 0.0;
  double mFXReverbSmoothedTone = 0.5;
  double mFXReverbSmoothedEarlyLevel = 1.0;
  double mFXReverbSmoothedEarlyToneHz = 3200.0;
  double mFXReverbSmoothedLowCutHz = 20.0;
  double mFXReverbSmoothedHighCutHz = 20000.0;
  std::array<double, kNumChannelsInternal> mFXReverbLowCutLPState = {};
  std::array<double, kNumChannelsInternal> mFXReverbHighCutLPState = {};
  std::array<double, kNumChannelsInternal> mFXReverbStereoDecorrelatorState = {};
  bool mFXReverbWasActive = false;
  // Keep this as a dedicated DC blocker.
  recursive_linear_filter::HighPass mHighPass;
  //  recursive_linear_filter::LowPass mLowPass;

  // Path to model's config.json or model.nam
  WDL_String mNAMPath;
  std::array<WDL_String, 3> mAmpNAMPaths;
  WDL_String mStompNAMPath;
  // Path to IR (.wav file)
  WDL_String mIRPath;
  WDL_String mIRPathRight;

  WDL_String mHighLightColor{PluginColors::NAM_THEMECOLOR.ToColorCode()};

  std::unordered_map<std::string, double> mNAMParams = {{"Input", 0.0}, {"Output", 0.0}};

  NAMSender mInputSender, mOutputSender;
};
