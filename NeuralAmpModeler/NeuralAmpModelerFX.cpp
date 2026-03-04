#include "NeuralAmpModeler.h"

using iplug::sample;

namespace
{
constexpr double kPi = 3.14159265358979323846;

struct DelaySyncDivisionDef
{
  double quarterNoteMultiplier = 1.0;
};

constexpr std::array<DelaySyncDivisionDef, 14> kDelaySyncDivisions = {{
  {0.125},             // 1/32
  {1.0 / 6.0},         // 1/16T
  {0.1875},            // 1/32D
  {0.25},              // 1/16
  {1.0 / 3.0},         // 1/8T
  {0.375},             // 1/16D
  {0.5},               // 1/8
  {2.0 / 3.0},         // 1/4T
  {0.75},              // 1/8D
  {1.0},               // 1/4
  {4.0 / 3.0},         // 1/2T
  {1.5},               // 1/4D
  {2.0},               // 1/2
  {4.0},               // 1/1
}};

int GetDelaySyncDivisionIndexFromNormalized(const double normalizedValue)
{
  const double normalized = std::clamp(normalizedValue, 0.0, 1.0);
  const int maxIndex = static_cast<int>(kDelaySyncDivisions.size()) - 1;
  return static_cast<int>(std::llround(normalized * static_cast<double>(maxIndex)));
}

double GetDelaySyncTimeMsFromNormalized(const double normalizedValue, const double tempoBPM)
{
  const int idx = GetDelaySyncDivisionIndexFromNormalized(normalizedValue);
  const double clampedTempoBPM = (std::isfinite(tempoBPM) && tempoBPM > 1.0) ? tempoBPM : 120.0;
  const double quarterNoteMs = 60000.0 / clampedTempoBPM;
  const double delayTimeMs = quarterNoteMs * kDelaySyncDivisions[static_cast<size_t>(idx)].quarterNoteMultiplier;
  return std::clamp(delayTimeMs, 1.0, 2000.0);
}
} // namespace

void NeuralAmpModeler::_ProcessFXDelayStage(sample** ioPointers, const size_t numChannelsInternal,
                                            const size_t numChannelsMonoCore, const size_t numFrames,
                                            const double sampleRate, const bool fxDelayActive)
{
  if (ioPointers == nullptr || mFXDelayBufferSamples <= 2 || sampleRate <= 0.0)
    return;

  const bool stereoFXBusActive = (numChannelsInternal == 2);
  const bool monoSourceAtFX = (numChannelsMonoCore == 1);
  constexpr double kFXDelayFeedbackCrossStereo = 0.22;
  constexpr double kFXDelayWetCrossStereo = 0.16;
  constexpr double kFXDelayWetWidthStereo = 1.28;
  constexpr double kFXDelayMonoStereoTimeOffsetMs = 12.0;
  constexpr double kFXDelayMonoStereoPingPongFeedback = 0.75;
  const bool syncMode = (GetParam(kFXDelayTimeMode)->Int() == 0);
  const double delayTimeParamNormalized = GetParam(kFXDelayTimeMs)->GetNormalized();
  const double targetDelayTimeMs =
    syncMode ? GetDelaySyncTimeMsFromNormalized(delayTimeParamNormalized, mDelayTempoBPM.load(std::memory_order_relaxed))
             : GetParam(kFXDelayTimeMs)->Value();
  const double targetTimeSamples =
    std::clamp(targetDelayTimeMs * 0.001 * sampleRate, 1.0, static_cast<double>(mFXDelayBufferSamples - 2));
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
    const double monoStereoTimeOffsetSamples =
      monoSourceAtFX ? (kFXDelayMonoStereoTimeOffsetMs * 0.001 * sampleRate) : 0.0;
    std::array<double, kNumChannelsInternal> drySamples = {};
    std::array<double, kNumChannelsInternal> filteredDelayedSamples = {};
    std::array<double, kNumChannelsInternal> feedbackDelayedSamples = {};
    std::array<double, kNumChannelsInternal> wetDelayedSamples = {};

    for (size_t c = 0; c < numChannelsInternal; ++c)
    {
      auto& delayBuffer = mFXDelayBuffer[c];
      const double dry = ioPointers[c][s];
      drySamples[c] = dry;

      double channelTimeSamples = smoothedTimeSamples;
      if (monoSourceAtFX && stereoFXBusActive)
      {
        const double stereoOffsetSign = (c == 0) ? -1.0 : 1.0;
        channelTimeSamples = std::clamp(
          smoothedTimeSamples + stereoOffsetSign * monoStereoTimeOffsetSamples,
          1.0,
          static_cast<double>(mFXDelayBufferSamples - 2));
      }

      double readPos = static_cast<double>(writeIndex) - channelTimeSamples;
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
      filteredDelayedSamples[c] = highCutState;
      feedbackDelayedSamples[c] = filteredDelayedSamples[c];
      wetDelayedSamples[c] = filteredDelayedSamples[c];
    }

    if (stereoFXBusActive)
    {
      const double feedbackCross = monoSourceAtFX ? 0.60 : kFXDelayFeedbackCrossStereo;
      const double wetCross = monoSourceAtFX ? 0.26 : kFXDelayWetCrossStereo;
      const double wetWidth = monoSourceAtFX ? 1.40 : kFXDelayWetWidthStereo;
      const double delayedL = filteredDelayedSamples[0];
      const double delayedR = filteredDelayedSamples[1];
      feedbackDelayedSamples[0] = (1.0 - feedbackCross) * delayedL + feedbackCross * delayedR;
      feedbackDelayedSamples[1] = (1.0 - feedbackCross) * delayedR + feedbackCross * delayedL;
      wetDelayedSamples[0] = (1.0 - wetCross) * delayedL + wetCross * delayedR;
      wetDelayedSamples[1] = (1.0 - wetCross) * delayedR + wetCross * delayedL;

      const double wetMid = 0.5 * (wetDelayedSamples[0] + wetDelayedSamples[1]);
      const double wetSide = 0.5 * (wetDelayedSamples[0] - wetDelayedSamples[1]) * wetWidth;
      wetDelayedSamples[0] = wetMid + wetSide;
      wetDelayedSamples[1] = wetMid - wetSide;
    }

    for (size_t c = 0; c < numChannelsInternal; ++c)
    {
      auto& delayBuffer = mFXDelayBuffer[c];
      double feedbackForWrite = feedbackDelayedSamples[c];
      if (monoSourceAtFX && stereoFXBusActive)
      {
        const size_t otherChannel = 1 - c;
        feedbackForWrite = (1.0 - kFXDelayMonoStereoPingPongFeedback) * feedbackDelayedSamples[c]
                           + kFXDelayMonoStereoPingPongFeedback * feedbackDelayedSamples[otherChannel];
      }
      const double writeValue = drySamples[c] + smoothedFeedback * feedbackForWrite;
      delayBuffer[writeIndex] = static_cast<sample>(writeValue);

      if (fxDelayActive)
        // "Amount" behavior: keep dry at unity and add wet signal.
        ioPointers[c][s] = static_cast<sample>(drySamples[c] + wetDelayedSamples[c] * smoothedMix);
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

void NeuralAmpModeler::_ProcessFXReverbStage(sample** ioPointers, const size_t numChannelsInternal,
                                             const size_t numChannelsMonoCore, const size_t numFrames,
                                             const double sampleRate)
{
  if (ioPointers == nullptr || sampleRate <= 0.0 || mFXReverbPreDelayBufferSamples <= 2)
    return;

  const bool stereoFXBusActive = (numChannelsInternal == 2);
  const bool monoSourceAtFX = (numChannelsMonoCore == 1);
  constexpr double kFXReverbWetCrossStereo = 0.06;
  constexpr double kFXReverbMonoStereoPreDelaySkewMs = 3.0;
  constexpr double kReverbStateLimit = 32.0;
  auto finiteOrZero = [](double value) { return std::isfinite(value) ? value : 0.0; };
  auto finiteClamp = [](double value, double limit) {
    if (!std::isfinite(value))
      return 0.0;
    return std::clamp(value, -limit, limit);
  };
  const double targetMix = std::clamp(GetParam(kFXReverbMix)->Value() * 0.01, 0.0, 1.0);
  const double targetDecaySeconds = std::clamp(GetParam(kFXReverbDecay)->Value(), 0.1, 10.0);
  const double targetPreDelaySamples = std::clamp(
    GetParam(kFXReverbPreDelayMs)->Value() * 0.001 * sampleRate, 0.0, static_cast<double>(mFXReverbPreDelayBufferSamples - 2));
  const double targetTone = std::clamp(GetParam(kFXReverbTone)->Value() * 0.01, 0.0, 1.0);
  const double maxCutHz = std::max(40.0, 0.45 * sampleRate);
  const double targetLowCutHz = std::clamp(GetParam(kFXReverbLowCutHz)->Value(), 20.0, maxCutHz);
  const double targetHighCutHz =
    std::clamp(std::max(GetParam(kFXReverbHighCutHz)->Value(), targetLowCutHz + 20.0), 20.0, maxCutHz);

  constexpr double kReverbMixSmoothingMs = 40.0;
  constexpr double kReverbDecaySmoothingMs = 80.0;
  constexpr double kReverbPreDelaySmoothingMs = 120.0;
  constexpr double kReverbToneSmoothingMs = 60.0;
  constexpr double kReverbCutSmoothingMs = 60.0;
  constexpr double kReverbEarlyLevelSmoothingMs = 70.0;
  constexpr double kReverbEarlyToneSmoothingMs = 70.0;
  const double mixAlpha = 1.0 - std::exp(-1.0 / (sampleRate * kReverbMixSmoothingMs * 0.001));
  const double decayAlpha = 1.0 - std::exp(-1.0 / (sampleRate * kReverbDecaySmoothingMs * 0.001));
  const double preDelayAlpha = 1.0 - std::exp(-1.0 / (sampleRate * kReverbPreDelaySmoothingMs * 0.001));
  const double toneAlphaParam = 1.0 - std::exp(-1.0 / (sampleRate * kReverbToneSmoothingMs * 0.001));
  const double cutAlphaParam = 1.0 - std::exp(-1.0 / (sampleRate * kReverbCutSmoothingMs * 0.001));
  const double earlyLevelAlpha = 1.0 - std::exp(-1.0 / (sampleRate * kReverbEarlyLevelSmoothingMs * 0.001));
  const double earlyToneAlphaParam = 1.0 - std::exp(-1.0 / (sampleRate * kReverbEarlyToneSmoothingMs * 0.001));

  double smoothedMix = mFXReverbSmoothedMix;
  double smoothedDecaySeconds = mFXReverbSmoothedDecaySeconds;
  double smoothedPreDelaySamples = mFXReverbSmoothedPreDelaySamples;
  double smoothedTone = mFXReverbSmoothedTone;
  double smoothedEarlyLevel = mFXReverbSmoothedEarlyLevel;
  double smoothedEarlyToneHz = mFXReverbSmoothedEarlyToneHz;
  double smoothedLowCutHz = mFXReverbSmoothedLowCutHz;
  double smoothedHighCutHz = mFXReverbSmoothedHighCutHz;
  size_t preDelayWriteIndex = mFXReverbPreDelayWriteIndex;
  std::array<double, kNumChannelsInternal> stereoDecorrelatorState = mFXReverbStereoDecorrelatorState;

  constexpr double kRoomWetGain = 0.95;
  constexpr double kRoomEarlyGain = 0.46;
  constexpr double kRoomEarlyDirect = 0.02;
  constexpr double kRoomEarlyLevelBase = 0.62;
  constexpr double kRoomPreDiffAllpassGain = 0.40;
  constexpr double kRoomDecayScale = 1.20;
  constexpr double kRoomCombFeedbackMax = 0.988;
  constexpr double kRoomToneTilt = 1.00;
  constexpr double kRoomCombDampTilt = 0.90;
  constexpr double kRoomLateDiffusionGain = 0.22;
  constexpr double kRoomLateInputGain = 0.25;
  constexpr double kRoomFDNCrossFeed = 0.03;
  constexpr double kRoomExtraPreDelayMs = 2.0;
  constexpr double kRoomOutputTrim = 1.10;
  constexpr std::array<double, 8> kRoomEarlyTapGains = {1.10, 0.92, 0.80, 0.66, 0.50, 0.36, 0.25, 0.16};
  constexpr std::array<double, 8> kRoomCombModRatesHz = {0.035, 0.042, 0.050, 0.059, 0.070, 0.082, 0.095, 0.11};
  constexpr std::array<double, 8> kRoomCombModDepthSamples = {0.00, 0.003, 0.006, 0.010, 0.014, 0.019, 0.025, 0.032};
  std::array<double, 8> combModPhase = mFXReverbCombModPhase;

  for (size_t s = 0; s < numFrames; ++s)
  {
    smoothedMix += mixAlpha * (targetMix - smoothedMix);
    smoothedDecaySeconds += decayAlpha * (targetDecaySeconds - smoothedDecaySeconds);
    smoothedPreDelaySamples += preDelayAlpha * (targetPreDelaySamples - smoothedPreDelaySamples);
    smoothedTone += toneAlphaParam * (targetTone - smoothedTone);
    smoothedLowCutHz += cutAlphaParam * (targetLowCutHz - smoothedLowCutHz);
    smoothedHighCutHz += cutAlphaParam * (targetHighCutHz - smoothedHighCutHz);
    if (smoothedHighCutHz < smoothedLowCutHz + 20.0)
      smoothedHighCutHz = std::min(maxCutHz, smoothedLowCutHz + 20.0);
    const double targetEarlyLevel = kRoomEarlyLevelBase;
    smoothedEarlyLevel += earlyLevelAlpha * (targetEarlyLevel - smoothedEarlyLevel);
    const double targetEarlyToneHz = 1700.0 + smoothedTone * 3600.0;
    smoothedEarlyToneHz += earlyToneAlphaParam * (targetEarlyToneHz - smoothedEarlyToneHz);
    smoothedEarlyToneHz = std::clamp(smoothedEarlyToneHz, 900.0, 7000.0);
    const double wetCoreGain = kRoomWetGain;
    const double earlyGain = kRoomEarlyGain;
    const double preDiffAllpassGain = kRoomPreDiffAllpassGain;
    const double decayKnobNorm = std::clamp((smoothedDecaySeconds - 0.1) / 9.9, 0.0, 1.0);
    const double decaySpanCompression = 1.0 - 0.32 * decayKnobNorm * decayKnobNorm;
    const double shapedDecaySeconds = 0.20 + 9.4 * std::pow(decayKnobNorm, 1.65) * decaySpanCompression;
    const double sizeFromDecay = std::pow(decayKnobNorm, 0.82);
    const double sizeMacro = std::clamp(0.28 + 0.92 * sizeFromDecay, 0.25, 1.30);
    const double combDelayScale = std::clamp(0.95 + 1.10 * sizeMacro, 0.95, 2.38);
    const double longDelayNorm = std::clamp((combDelayScale - 1.05) / 1.20, 0.0, 1.0);
    const double earlyTapScaleRoom = std::clamp(0.90 + 0.70 * sizeMacro, 0.80, 2.00);
    const double sizePreDelaySamples = (0.8 + 5.8 * sizeMacro) * 0.001 * sampleRate;
    const double effectiveDecaySeconds = std::max(0.08, shapedDecaySeconds * kRoomDecayScale);
    const double combFeedbackMax = kRoomCombFeedbackMax;
    const double toneTilt = kRoomToneTilt;
    const double combDampTilt = kRoomCombDampTilt;
    const double lateDiffusionGain = kRoomLateDiffusionGain;
    const double lateInputGain = kRoomLateInputGain;
    const double earlyDirectMix = kRoomEarlyDirect;
    const double modeOutputTrim = kRoomOutputTrim;
    const double effectivePreDelaySamples = std::clamp(
      smoothedPreDelaySamples + kRoomExtraPreDelayMs * 0.001 * sampleRate + sizePreDelaySamples,
      0.0,
      static_cast<double>(mFXReverbPreDelayBufferSamples - 2));
    const double monoStereoPreDelaySkewSamples =
      monoSourceAtFX ? (kFXReverbMonoStereoPreDelaySkewMs * 0.001 * sampleRate) : 0.0;
    const double wetMix = std::clamp(smoothedMix, 0.0, 1.0);
    const double wetMixShaped = std::pow(wetMix, 1.48);
    const double dryMix = std::cos(0.5 * kPi * wetMixShaped);
    const double wetGain = std::sin(0.5 * kPi * wetMixShaped);
    const double decayWetCompShape = std::pow(decayKnobNorm, 1.18);
    const double baseWetMakeupTargetGain = 1.84;
    const double decayWetCompGain = 0.78 * decayWetCompShape;
    const double wetMakeupTargetGain = baseWetMakeupTargetGain + decayWetCompGain;
    const double wetMakeupCurve = std::pow(wetMixShaped, 2.35);
    const double wetMakeupGain = 1.0 + (wetMakeupTargetGain - 1.0) * wetMakeupCurve;
    const double toneCutoffHz = std::clamp((2000.0 + smoothedTone * 12000.0) * toneTilt, 1000.0, 16000.0);
    const double toneAlpha = 1.0 - std::exp(-2.0 * kPi * toneCutoffHz / sampleRate);
    const double lowDecayDamping = std::clamp((0.55 - decayKnobNorm) / 0.55, 0.0, 1.0);
    const double highDecayStabilizer = std::clamp((decayKnobNorm - 0.90) / 0.10, 0.0, 1.0);
    const double feedbackAirDecayTilt = 1.0 - 0.15 * lowDecayDamping - 0.020 * highDecayStabilizer;
    const double feedbackAirCutoffHz =
      std::clamp((4800.0 + smoothedTone * 9000.0) * combDampTilt * feedbackAirDecayTilt, 2200.0, 15000.0);
    const double feedbackAirAlpha = 1.0 - std::exp(-2.0 * kPi * feedbackAirCutoffHz / sampleRate);
    const double dynamicLateDiffusionGain =
      std::clamp(
        lateDiffusionGain + 0.06 * sizeMacro + 0.08 * longDelayNorm + 0.05 * decayKnobNorm
          + 0.04 * highDecayStabilizer,
        0.14,
        0.58);
    const double effectiveLateInputGain = lateInputGain;
    const double wetLowCutAlpha = 1.0 - std::exp(-2.0 * kPi * smoothedLowCutHz / sampleRate);
    const double wetHighCutAlpha = 1.0 - std::exp(-2.0 * kPi * smoothedHighCutHz / sampleRate);
    const double earlyToneAlpha = 1.0 - std::exp(-2.0 * kPi * smoothedEarlyToneHz / sampleRate);
    std::array<double, 8> combModOffset = {};
    for (size_t i = 0; i < combModOffset.size(); ++i)
    {
      const double baseCombModDepth = kRoomCombModDepthSamples[i];
      const double combModDepth = baseCombModDepth * (1.0 - 0.45 * highDecayStabilizer);
      const double combModRateHz = kRoomCombModRatesHz[i];
      combModOffset[i] = combModDepth * std::sin(combModPhase[i]);
      combModPhase[i] += 2.0 * kPi * combModRateHz / sampleRate;
      if (combModPhase[i] >= 2.0 * kPi)
        combModPhase[i] -= 2.0 * kPi;
    }
    std::array<double, kNumChannelsInternal> drySamples = {};
    std::array<double, kNumChannelsInternal> wetSamples = {};
    std::array<double, kNumChannelsInternal> earlyShapedSamples = {};
    std::array<double, kNumChannelsInternal> lateInputSamples = {};
    std::array<double, kNumChannelsInternal> combSumSamples = {};
    std::array<std::array<double, 8>, kNumChannelsInternal> combOutSamples = {};

    for (size_t c = 0; c < numChannelsInternal; ++c)
    {
      auto& preDelayBuffer = mFXReverbPreDelayBuffer[c];
      if (preDelayBuffer.empty())
        continue;

      const double dry = finiteOrZero(static_cast<double>(ioPointers[c][s]));
      drySamples[c] = dry;
      preDelayBuffer[preDelayWriteIndex] = static_cast<sample>(dry);

      double early = 0.0;
      for (size_t i = 0; i < kRoomEarlyTapGains.size(); ++i)
      {
        const size_t roomTapDelay = std::min(
          preDelayBuffer.size() - 1,
          std::max<size_t>(
            1, static_cast<size_t>(std::llround(static_cast<double>(mFXReverbEarlyTapSamples[0][i]) * earlyTapScaleRoom))));
        const size_t roomTapIndex = (preDelayWriteIndex + preDelayBuffer.size() - roomTapDelay) % preDelayBuffer.size();
        const double tapSample = static_cast<double>(preDelayBuffer[roomTapIndex]);
        const double tapGain = kRoomEarlyTapGains[i];
        early += tapGain * tapSample;
      }
      auto& earlyToneState = mFXReverbEarlyToneState[c];
      earlyToneState += earlyToneAlpha * (early - earlyToneState);
      const double earlyShaped = earlyToneState * smoothedEarlyLevel;

      double channelPreDelaySamples = effectivePreDelaySamples;
      if (monoSourceAtFX && stereoFXBusActive)
      {
        const double stereoSkewSign = (c == 0) ? -1.0 : 1.0;
        channelPreDelaySamples = std::clamp(
          effectivePreDelaySamples + stereoSkewSign * monoStereoPreDelaySkewSamples,
          0.0,
          static_cast<double>(mFXReverbPreDelayBufferSamples - 2));
      }

      double preReadPos = static_cast<double>(preDelayWriteIndex) - channelPreDelaySamples;
      if (preReadPos < 0.0)
        preReadPos += static_cast<double>(mFXReverbPreDelayBufferSamples);
      const auto preReadIndex0 = static_cast<size_t>(preReadPos);
      const auto preReadIndex1 = (preReadIndex0 + 1 < mFXReverbPreDelayBufferSamples) ? (preReadIndex0 + 1) : 0;
      const double preFrac = preReadPos - static_cast<double>(preReadIndex0);
      const double preDelayed = finiteOrZero(static_cast<double>(preDelayBuffer[preReadIndex0]) * (1.0 - preFrac)
                                             + static_cast<double>(preDelayBuffer[preReadIndex1]) * preFrac);

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
        const double delayed = finiteOrZero(static_cast<double>(preDiffBuffer[readIndex]));
        const double out = finiteClamp(-preDiffAllpassGain * lateInput + delayed, kReverbStateLimit);
        preDiffBuffer[preDiffWriteIndex] = static_cast<sample>(finiteClamp(lateInput + preDiffAllpassGain * out, kReverbStateLimit));
        lateInput = out;

        ++preDiffWriteIndex;
        if (preDiffWriteIndex >= preDiffBuffer.size())
          preDiffWriteIndex = 0;
      }
      earlyShapedSamples[c] = earlyShaped;
      lateInputSamples[c] = lateInput;
    }

    for (size_t c = 0; c < numChannelsInternal; ++c)
    {
      double combSum = 0.0;
      for (size_t i = 0; i < combOutSamples[c].size(); ++i)
      {
        auto& combBuffer = mFXReverbCombBuffer[c][i];
        if (combBuffer.empty())
          continue;
        auto& combWriteIndex = mFXReverbCombWriteIndex[c][i];
        const size_t combDelaySamples = mFXReverbCombDelaySamples[c][i];
        const double channelModSign = (c == 0) ? 1.0 : -1.0;
        const double scaledCombDelaySamples = static_cast<double>(combDelaySamples) * combDelayScale;
        const double modulatedDelay = std::clamp(scaledCombDelaySamples + channelModSign * combModOffset[i], 1.0,
                                                 static_cast<double>(combBuffer.size() - 2));
        double readPos = static_cast<double>(combWriteIndex) - modulatedDelay;
        if (readPos < 0.0)
          readPos += static_cast<double>(combBuffer.size());
        const auto readIndex0 = static_cast<size_t>(readPos);
        const auto readIndex1 = (readIndex0 + 1 < combBuffer.size()) ? (readIndex0 + 1) : 0;
        const double frac = readPos - static_cast<double>(readIndex0);
        const double delayed = finiteOrZero(static_cast<double>(combBuffer[readIndex0]) * (1.0 - frac)
                                            + static_cast<double>(combBuffer[readIndex1]) * frac);
        auto& combDampState = mFXReverbCombDampState[c][i];
        combDampState = finiteOrZero(combDampState);
        combDampState += feedbackAirAlpha * (delayed - combDampState);
        combDampState = finiteClamp(combDampState, kReverbStateLimit);
        combOutSamples[c][i] = combDampState;
        combSum += combDampState;
      }
      combSumSamples[c] = combSum;
    }

    constexpr double kFDNHouseholderScale = 0.25; // 2 / 8
    const double fdnCrossFeed = kRoomFDNCrossFeed;
    for (size_t c = 0; c < numChannelsInternal; ++c)
    {
      const size_t otherChannel = (c + 1) % numChannelsInternal;
      const double fdnInput = lateInputSamples[c] * effectiveLateInputGain;
      for (size_t i = 0; i < combOutSamples[c].size(); ++i)
      {
        auto& combBuffer = mFXReverbCombBuffer[c][i];
        if (combBuffer.empty())
          continue;
        auto& combWriteIndex = mFXReverbCombWriteIndex[c][i];
        const size_t combDelaySamples = mFXReverbCombDelaySamples[c][i];
        const double scaledCombDelaySamples = static_cast<double>(combDelaySamples) * combDelayScale;
        const double delaySeconds = scaledCombDelaySamples / sampleRate;
        const double decayFeedbackTrim = 0.0008 + 0.0016 * highDecayStabilizer;
        const double dynamicCombFeedbackMax = std::clamp(combFeedbackMax - decayFeedbackTrim, 0.82, 0.9975);
        const double combFeedback =
          std::clamp(std::pow(10.0, (-3.0 * delaySeconds) / effectiveDecaySeconds), 0.0, dynamicCombFeedbackMax);
        const double localMixed = finiteClamp(kFDNHouseholderScale * combSumSamples[c] - combOutSamples[c][i], kReverbStateLimit);
        const double crossMixed =
          finiteClamp(kFDNHouseholderScale * combSumSamples[otherChannel] - combOutSamples[otherChannel][i], kReverbStateLimit);
        const double mixed = finiteClamp((1.0 - fdnCrossFeed) * localMixed + fdnCrossFeed * crossMixed, kReverbStateLimit);
        const double lineWrite = finiteClamp(fdnInput + combFeedback * mixed, kReverbStateLimit);
        combBuffer[combWriteIndex] = static_cast<sample>(lineWrite);

        ++combWriteIndex;
        if (combWriteIndex >= combBuffer.size())
          combWriteIndex = 0;
      }
    }

    for (size_t c = 0; c < numChannelsInternal; ++c)
    {
      const double wet = 0.125 * combSumSamples[c];
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
        const double delayed = finiteOrZero(static_cast<double>(allpassBuffer[readIndex]));
        const double out = finiteClamp(-dynamicLateDiffusionGain * diffusedWet + delayed, kReverbStateLimit);
        allpassBuffer[allpassWriteIndex] =
          static_cast<sample>(finiteClamp(diffusedWet + dynamicLateDiffusionGain * out, kReverbStateLimit));
        diffusedWet = out;

        ++allpassWriteIndex;
        if (allpassWriteIndex >= allpassBuffer.size())
          allpassWriteIndex = 0;
      }

      mFXReverbToneState[c] = finiteOrZero(mFXReverbToneState[c]);
      mFXReverbToneState[c] += toneAlpha * (diffusedWet - mFXReverbToneState[c]);
      mFXReverbToneState[c] = finiteClamp(mFXReverbToneState[c], kReverbStateLimit);
      const double tonedWet = finiteClamp(mFXReverbToneState[c] * wetCoreGain, kReverbStateLimit);
      const double earlyWetScale = std::clamp(1.0 - wetMix, 0.0, 1.0);
      const double earlyLateBlend = earlyWetScale * earlyWetScale;
      const double highDecayEarlyTrim = std::clamp(1.0 - 0.18 * std::pow(decayKnobNorm, 1.20), 0.78, 1.0);
      const double rawWet = tonedWet + earlyShapedSamples[c] * earlyGain * earlyLateBlend * highDecayEarlyTrim;
      auto& lowCutState = mFXReverbLowCutLPState[c];
      auto& highCutState = mFXReverbHighCutLPState[c];
      lowCutState = finiteOrZero(lowCutState);
      highCutState = finiteOrZero(highCutState);
      lowCutState += wetLowCutAlpha * (rawWet - lowCutState);
      lowCutState = finiteClamp(lowCutState, kReverbStateLimit);
      const double lowCutWet = rawWet - lowCutState;
      highCutState += wetHighCutAlpha * (lowCutWet - highCutState);
      highCutState = finiteClamp(highCutState, kReverbStateLimit);
      const double earlyDirect = earlyShapedSamples[c] * earlyDirectMix * earlyWetScale * earlyWetScale;
      wetSamples[c] = finiteClamp(modeOutputTrim * highCutState + earlyDirect, kReverbStateLimit);
    }

    if (stereoFXBusActive)
    {
      const double wetCross = monoSourceAtFX ? 0.14 : kFXReverbWetCrossStereo;
      const double widthFromDecay = std::pow(decayKnobNorm, 0.90);
      const double roomWetWidth = monoSourceAtFX ? 1.40 : (1.20 + 0.38 * sizeMacro + 0.26 * widthFromDecay);
      const double wetL = wetSamples[0];
      const double wetR = wetSamples[1];
      wetSamples[0] = (1.0 - wetCross) * wetL + wetCross * wetR;
      wetSamples[1] = (1.0 - wetCross) * wetR + wetCross * wetL;

      // Subtle pre-width decorrelation to increase perceived stereo spread.
      constexpr std::array<double, 2> kStereoDecorrelatorCoeff = {0.42, -0.36};
      const double decorMix = std::clamp(0.10 + 0.20 * sizeMacro + 0.16 * widthFromDecay, 0.0, 0.52);
      for (size_t c = 0; c < 2; ++c)
      {
        const double in = wetSamples[c];
        const double g = kStereoDecorrelatorCoeff[c];
        auto& state = stereoDecorrelatorState[c];
        const double decorrelated = finiteClamp(-g * in + state, kReverbStateLimit);
        state = finiteClamp(in + g * decorrelated, kReverbStateLimit);
        wetSamples[c] = finiteClamp((1.0 - decorMix) * in + decorMix * decorrelated, kReverbStateLimit);
      }

      const double wetWidth = roomWetWidth;
      const double wetMid = 0.5 * (wetSamples[0] + wetSamples[1]);
      const double wetSide = 0.5 * (wetSamples[0] - wetSamples[1]) * wetWidth;
      wetSamples[0] = wetMid + wetSide;
      wetSamples[1] = wetMid - wetSide;
    }

    for (size_t c = 0; c < numChannelsInternal; ++c)
    {
      const double mixedOut = dryMix * drySamples[c] + (wetGain * wetMakeupGain) * wetSamples[c];
      ioPointers[c][s] = static_cast<sample>(finiteClamp(mixedOut, kReverbStateLimit));
    }

    ++preDelayWriteIndex;
    if (preDelayWriteIndex >= mFXReverbPreDelayBufferSamples)
      preDelayWriteIndex = 0;
  }

  mFXReverbSmoothedMix = smoothedMix;
  mFXReverbSmoothedDecaySeconds = smoothedDecaySeconds;
  mFXReverbSmoothedPreDelaySamples = smoothedPreDelaySamples;
  mFXReverbSmoothedTone = smoothedTone;
  mFXReverbSmoothedEarlyLevel = smoothedEarlyLevel;
  mFXReverbSmoothedEarlyToneHz = smoothedEarlyToneHz;
  mFXReverbSmoothedLowCutHz = smoothedLowCutHz;
  mFXReverbSmoothedHighCutHz = smoothedHighCutHz;
  mFXReverbPreDelayWriteIndex = preDelayWriteIndex;
  mFXReverbStereoDecorrelatorState = stereoDecorrelatorState;
  mFXReverbCombModPhase = combModPhase;
}

void NeuralAmpModeler::_ResetFXReverbState()
{
  mFXReverbPreDelayWriteIndex = 0;
  for (auto& channelBuffer : mFXReverbPreDelayBuffer)
    std::fill(channelBuffer.begin(), channelBuffer.end(), 0.0f);

  for (size_t c = 0; c < kNumChannelsInternal; ++c)
  {
    for (size_t i = 0; i < 2; ++i)
    {
      std::fill(mFXReverbPreDiffAllpassBuffer[c][i].begin(), mFXReverbPreDiffAllpassBuffer[c][i].end(), 0.0f);
      mFXReverbPreDiffAllpassWriteIndex[c][i] = 0;
    }
    for (size_t i = 0; i < 8; ++i)
    {
      std::fill(mFXReverbCombBuffer[c][i].begin(), mFXReverbCombBuffer[c][i].end(), 0.0f);
      mFXReverbCombWriteIndex[c][i] = 0;
    }
    for (size_t i = 0; i < 2; ++i)
    {
      std::fill(mFXReverbAllpassBuffer[c][i].begin(), mFXReverbAllpassBuffer[c][i].end(), 0.0f);
      mFXReverbAllpassWriteIndex[c][i] = 0;
    }
    mFXReverbCombDampState[c].fill(0.0);
    mFXReverbToneState[c] = 0.0;
    mFXReverbEarlyToneState[c] = 0.0;
    mFXReverbLowCutLPState[c] = 0.0;
    mFXReverbHighCutLPState[c] = 0.0;
    mFXReverbStereoDecorrelatorState[c] = 0.0;
  }
}
