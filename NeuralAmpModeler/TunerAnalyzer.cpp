#include "TunerAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kMinFrequencyHz = 24.0;
constexpr double kMaxFrequencyHz = 760.0;
constexpr double kLowPassHz = 2600.0;
constexpr uint32_t kAnalysisSafetySamples = 1024;

struct PitchCandidate
{
  bool valid = false;
  double frequencyHz = 0.0;
  double midiFloat = 0.0;
  int midiNote = -1;
  float cents = 0.0f;
  double clarity = 0.0;
  double rms = 0.0;
};

struct DemodulationResult
{
  double real = 0.0;
  double imag = 0.0;
  double purity = 0.0;
};

double MidiFloatFromFrequency(const double frequencyHz)
{
  return 69.0 + 12.0 * std::log2(std::max(1.0e-6, frequencyHz) / 440.0);
}

int MidiNoteFromMidiFloat(const double midiFloat)
{
  return static_cast<int>(std::clamp(std::lround(midiFloat), 0L, 127L));
}

float CentsForMidi(const double midiFloat, const int midiNote)
{
  return static_cast<float>(std::clamp(100.0 * (midiFloat - static_cast<double>(midiNote)), -50.0, 50.0));
}

double FrequencyForMidi(const int midiNote)
{
  return 440.0 * std::pow(2.0, (static_cast<double>(midiNote) - 69.0) / 12.0);
}

double CentsDeltaForMidi(const double midiFloat, const int midiNote)
{
  return 100.0 * (midiFloat - static_cast<double>(midiNote));
}

template <size_t N>
double FractionalNsdfAtLag(const std::array<float, N>& frame, const double lag)
{
  const int lagBase = static_cast<int>(std::floor(lag));
  const double frac = lag - static_cast<double>(lagBase);
  const int nMax = static_cast<int>(N) - lagBase - 1;
  if (lagBase < 1 || nMax <= 8)
    return 0.0;

  double sumXY = 0.0;
  double sumXX = 0.0;
  double sumYY = 0.0;
  for (int n = 0; n < nMax; ++n)
  {
    const double a = static_cast<double>(frame[static_cast<size_t>(n)]);
    const double b0 = static_cast<double>(frame[static_cast<size_t>(n + lagBase)]);
    const double b1 = static_cast<double>(frame[static_cast<size_t>(n + lagBase + 1)]);
    const double b = b0 + frac * (b1 - b0);
    sumXY += a * b;
    sumXX += a * a;
    sumYY += b * b;
  }

  const double denom = sumXX + sumYY + 1.0e-20;
  return denom > 0.0 ? ((2.0 * sumXY) / denom) : 0.0;
}

double WrapPhase(double phase)
{
  while (phase > kPi)
    phase -= 2.0 * kPi;
  while (phase < -kPi)
    phase += 2.0 * kPi;
  return phase;
}

template <size_t N>
DemodulationResult DemodulateAtFrequency(const std::array<float, N>& frame,
                                         const int start,
                                         const int length,
                                         const double frequencyHz,
                                         const double sampleRate)
{
  const double omega = 2.0 * kPi * frequencyHz / sampleRate;
  const double cosStep = std::cos(omega);
  const double sinStep = std::sin(omega);
  double cosPhase = std::cos(omega * static_cast<double>(start));
  double sinPhase = std::sin(omega * static_cast<double>(start));
  DemodulationResult result;
  double energy = 0.0;
  double windowSum = 0.0;
  double windowSumSq = 0.0;

  for (int i = 0; i < length; ++i)
  {
    const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) /
                                               static_cast<double>(length - 1));
    const double x = static_cast<double>(frame[static_cast<size_t>(start + i)]) * window;
    result.real += x * cosPhase;
    result.imag -= x * sinPhase;
    energy += x * x;
    windowSum += window;
    windowSumSq += window * window;

    const double nextCos = cosPhase * cosStep - sinPhase * sinStep;
    const double nextSin = sinPhase * cosStep + cosPhase * sinStep;
    cosPhase = nextCos;
    sinPhase = nextSin;
  }

  const double expectedSingleTonePower =
    (windowSumSq > 0.0) ? (energy * windowSum * windowSum / (2.0 * windowSumSq)) : 0.0;
  const double power = result.real * result.real + result.imag * result.imag;
  result.purity = expectedSingleTonePower > 0.0 ? std::clamp(power / expectedSingleTonePower, 0.0, 1.25) : 0.0;
  return result;
}

template <size_t N>
bool RefineLockedNotePitchFromPhase(const std::array<float, N>& frame,
                                    const double sampleRate,
                                    const int midiNote,
                                    double& frequencyHz,
                                    double& midiFloat,
                                    double& purity)
{
  constexpr int kHighWindowSize = 2048;
  constexpr int kLowWindowSize = 3072;
  constexpr int kHopSize = 1024;
  static_assert(N >= kLowWindowSize + kHopSize, "phase estimator requires overlapping windows");

  const double noteFrequencyHz = FrequencyForMidi(midiNote);
  const int windowSize = midiNote < 35 ? kLowWindowSize : kHighWindowSize;
  const auto first = DemodulateAtFrequency(frame, 0, windowSize, noteFrequencyHz, sampleRate);
  const auto second = DemodulateAtFrequency(frame, kHopSize, windowSize, noteFrequencyHz, sampleRate);
  const double firstPower = first.real * first.real + first.imag * first.imag;
  const double secondPower = second.real * second.real + second.imag * second.imag;
  if (firstPower <= 1.0e-14 || secondPower <= 1.0e-14)
    return false;

  const double firstPhase = std::atan2(first.imag, first.real);
  const double secondPhase = std::atan2(second.imag, second.real);
  const double phaseDelta = WrapPhase(secondPhase - firstPhase);
  const double frequencyOffsetHz = phaseDelta * sampleRate / (2.0 * kPi * static_cast<double>(kHopSize));
  const double refinedFrequencyHz = noteFrequencyHz + frequencyOffsetHz;
  const double refinedMidiFloat = MidiFloatFromFrequency(refinedFrequencyHz);
  const double cents = CentsDeltaForMidi(refinedMidiFloat, midiNote);
  if (refinedFrequencyHz < kMinFrequencyHz || refinedFrequencyHz > kMaxFrequencyHz || std::fabs(cents) > 58.0)
    return false;

  frequencyHz = refinedFrequencyHz;
  midiFloat = refinedMidiFloat;
  purity = std::min(first.purity, second.purity);
  return true;
}

int RequiredConfirmationsForMidi(const int midiNote)
{
  return midiNote >= 42 ? 3 : 2; // A2 and above are more prone to short false locks.
}
} // namespace

void TunerAnalyzer::Reset() noexcept
{
  mHasPitch.store(false, std::memory_order_relaxed);
  mMidiNote.store(0, std::memory_order_relaxed);
  mCents.store(0.0f, std::memory_order_relaxed);
  mWriteIndex.store(0, std::memory_order_relaxed);
#if NAM_DEV_DIAGNOSTICS
  mDebugCandidateValid.store(false, std::memory_order_relaxed);
  mDebugPhaseAttempted.store(false, std::memory_order_relaxed);
  mDebugPhaseEstimated.store(false, std::memory_order_relaxed);
  mDebugPhaseUsed.store(false, std::memory_order_relaxed);
  mDebugRawMidiNote.store(-1, std::memory_order_relaxed);
  mDebugPhaseMidiNote.store(-1, std::memory_order_relaxed);
  mDebugRawCents.store(0.0f, std::memory_order_relaxed);
  mDebugPhaseCents.store(0.0f, std::memory_order_relaxed);
  mDebugPhasePurity.store(0.0f, std::memory_order_relaxed);
#endif
  mPublishedMidiNote = -1;
  mPendingMidiNote = -1;
  mPendingMidiCount = 0;
  mMissCount = 0;
  mDisplayCents = 0.0f;
  mDisplayVelocityCents = 0.0f;
  mFilteredTargetCents = 0.0f;
  mFilteredTargetMidiNote = -1;
  mPreviousRms = 0.0;
  mAttackSettleFrames = 0;
}

void TunerAnalyzer::Update(const double pluginSampleRate) noexcept
{
  if (pluginSampleRate <= 0.0)
    return;

  int downsample = 1;
  if (pluginSampleRate > 64000.0)
    downsample = std::max(1, static_cast<int>(std::ceil(pluginSampleRate / 56000.0)));
  const double sampleRate = pluginSampleRate / static_cast<double>(downsample);
  const uint32_t analysisSamples = static_cast<uint32_t>(kAnalysisSize * downsample);
  const uint32_t requiredSamples = analysisSamples + kAnalysisSafetySamples + 4;
  const uint32_t writeIndex = mWriteIndex.load(std::memory_order_acquire);
  PitchCandidate candidate;
#if NAM_DEV_DIAGNOSTICS
  bool debugCandidateValid = false;
  bool debugPhaseAttempted = false;
  bool debugPhaseEstimated = false;
  bool debugPhaseUsed = false;
  int debugRawMidiNote = -1;
  int debugPhaseMidiNote = -1;
  float debugRawCents = 0.0f;
  float debugPhaseCents = 0.0f;
  float debugPhasePurity = 0.0f;
#endif

  if (requiredSamples < kBufferSize && writeIndex > requiredSamples)
  {
    std::array<float, kAnalysisSize> frame{};
    constexpr uint32_t kMask = kBufferSize - 1;
    const uint32_t readEnd = writeIndex - kAnalysisSafetySamples;
    const uint32_t start = readEnd - analysisSamples;

    double rawSumSq = 0.0;
    for (int i = 0; i < kAnalysisSize; ++i)
    {
      const uint32_t readIndex = start + static_cast<uint32_t>(i * downsample);
      float sample = 0.0f;
      for (int ds = 0; ds < downsample; ++ds)
        sample += mBuffer[(readIndex + static_cast<uint32_t>(ds)) & kMask];
      sample /= static_cast<float>(downsample);
      frame[static_cast<size_t>(i)] = sample;
      rawSumSq += static_cast<double>(sample) * static_cast<double>(sample);
    }

    candidate.rms = std::sqrt(rawSumSq / static_cast<double>(kAnalysisSize));
    if (candidate.rms >= 0.0007)
    {
      double mean = 0.0;
      for (const float sample : frame)
        mean += static_cast<double>(sample);
      mean /= static_cast<double>(kAnalysisSize);

      const double oneMinusAlpha = 1.0 - std::exp(-2.0 * kPi * kLowPassHz / sampleRate);
      double conditionedSumSq = 0.0;
      float lpState = 0.0f;
      for (int i = 0; i < kAnalysisSize; ++i)
      {
        const float centered = static_cast<float>(static_cast<double>(frame[static_cast<size_t>(i)]) - mean);
        lpState = static_cast<float>(
          static_cast<double>(lpState) + oneMinusAlpha * (static_cast<double>(centered) - lpState));
        frame[static_cast<size_t>(i)] = lpState;
        conditionedSumSq += static_cast<double>(lpState) * static_cast<double>(lpState);
      }

      const std::array<float, kAnalysisSize> phaseFrame = frame;

      conditionedSumSq = 0.0;
      for (int i = 0; i < kAnalysisSize; ++i)
      {
        const double window =
          0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(kAnalysisSize - 1));
        frame[static_cast<size_t>(i)] = static_cast<float>(static_cast<double>(frame[static_cast<size_t>(i)]) * window);
        conditionedSumSq += static_cast<double>(frame[static_cast<size_t>(i)]) * static_cast<double>(frame[static_cast<size_t>(i)]);
      }

      const double conditionedRms = std::sqrt(conditionedSumSq / static_cast<double>(kAnalysisSize));
      if (conditionedRms >= 0.00030)
      {
        const int minLag = std::max(2, static_cast<int>(sampleRate / kMaxFrequencyHz));
        const int maxLag =
          std::min(kAnalysisSize / 2, std::max(minLag + 1, static_cast<int>(sampleRate / kMinFrequencyHz)));
        std::array<double, kAnalysisSize / 2 + 1> nsdf{};

        for (int lag = minLag; lag <= maxLag; ++lag)
        {
          const int nMax = kAnalysisSize - lag;
          double sumXY = 0.0;
          double sumXX = 0.0;
          double sumYY = 0.0;
          for (int n = 0; n < nMax; ++n)
          {
            const double a = static_cast<double>(frame[static_cast<size_t>(n)]);
            const double b = static_cast<double>(frame[static_cast<size_t>(n + lag)]);
            sumXY += a * b;
            sumXX += a * a;
            sumYY += b * b;
          }

          const double denom = sumXX + sumYY + 1.0e-20;
          nsdf[static_cast<size_t>(lag)] = denom > 0.0 ? ((2.0 * sumXY) / denom) : 0.0;
        }

        auto isLocalPeak = [&](const int lag) {
          return lag > minLag && lag < maxLag && nsdf[static_cast<size_t>(lag)] > 0.0
                 && nsdf[static_cast<size_t>(lag)] >= nsdf[static_cast<size_t>(lag - 1)]
                 && nsdf[static_cast<size_t>(lag)] > nsdf[static_cast<size_t>(lag + 1)];
        };

        int strongestLag = -1;
        double strongestPeak = -1.0;
        for (int lag = minLag + 1; lag < maxLag; ++lag)
        {
          if (!isLocalPeak(lag))
            continue;
          const double peak = nsdf[static_cast<size_t>(lag)];
          if (peak > strongestPeak)
          {
            strongestPeak = peak;
            strongestLag = lag;
          }
        }

        int bestLag = strongestLag;
        double bestPeak = strongestPeak;
        if (strongestLag > 0)
        {
          // MPM-style pick: use the earliest clear peak, then promote to a near-equal lower octave if present.
          const double earlyPeakRatio = 0.93;
          for (int lag = minLag + 1; lag < maxLag; ++lag)
          {
            if (!isLocalPeak(lag))
              continue;
            const double peak = nsdf[static_cast<size_t>(lag)];
            if (peak >= strongestPeak * earlyPeakRatio)
            {
              bestLag = lag;
              bestPeak = peak;
              break;
            }
          }

          const int octaveLag = bestLag * 2;
          if (octaveLag <= maxLag)
          {
            const double octavePeak = nsdf[static_cast<size_t>(octaveLag)];
            const double bestMidi = MidiFloatFromFrequency(sampleRate / std::max(1.0, static_cast<double>(bestLag)));
            const double octaveRatio = bestMidi >= 42.0 ? 1.06 : 0.94;
            if (octavePeak >= bestPeak * octaveRatio)
            {
              bestLag = octaveLag;
              bestPeak = octavePeak;
            }
          }
        }

        const double clarityThreshold = bestLag > static_cast<int>(sampleRate / 90.0) ? 0.66 : 0.70;
        if (bestLag > 0 && bestPeak >= clarityThreshold)
        {
          double refinedLag = static_cast<double>(bestLag);
          if (bestLag > minLag && bestLag < maxLag)
          {
            const double yPrev = nsdf[static_cast<size_t>(bestLag - 1)];
            const double y0 = nsdf[static_cast<size_t>(bestLag)];
            const double yNext = nsdf[static_cast<size_t>(bestLag + 1)];
            const double denom = yPrev - 2.0 * y0 + yNext;
            if (std::fabs(denom) > 1.0e-12)
              refinedLag += std::clamp(0.5 * (yPrev - yNext) / denom, -0.5, 0.5);
          }

          const double preliminaryMidi = MidiFloatFromFrequency(sampleRate / std::max(1.0, refinedLag));
          if (preliminaryMidi >= 58.5)
          {
            double fractionalLag = refinedLag;
            double fractionalPeak = FractionalNsdfAtLag(frame, fractionalLag);
            for (int step = -10; step <= 10; ++step)
            {
              const double testLag = static_cast<double>(bestLag) + 0.05 * static_cast<double>(step);
              if (testLag <= static_cast<double>(minLag) || testLag >= static_cast<double>(maxLag))
                continue;

              const double testPeak = FractionalNsdfAtLag(frame, testLag);
              if (testPeak > fractionalPeak)
              {
                fractionalPeak = testPeak;
                fractionalLag = testLag;
              }
            }

            refinedLag = fractionalLag;
          }

          candidate.frequencyHz = sampleRate / std::max(1.0, refinedLag);
          if (candidate.frequencyHz >= kMinFrequencyHz && candidate.frequencyHz <= kMaxFrequencyHz)
          {
            candidate.midiFloat = MidiFloatFromFrequency(candidate.frequencyHz);
            candidate.midiNote = MidiNoteFromMidiFloat(candidate.midiFloat);
            candidate.cents = CentsForMidi(candidate.midiFloat, candidate.midiNote);
            candidate.clarity = bestPeak;
            candidate.valid = true;
#if NAM_DEV_DIAGNOSTICS
            debugCandidateValid = true;
            debugRawMidiNote = candidate.midiNote;
            debugRawCents = candidate.cents;
#endif

            const bool candidateMatchesLockedNote =
              mPublishedMidiNote >= 55 && std::fabs(CentsDeltaForMidi(candidate.midiFloat, mPublishedMidiNote)) <= 58.0;
            const int phaseReferenceMidiNote =
              candidateMatchesLockedNote ? mPublishedMidiNote : (candidate.midiNote >= 21 ? candidate.midiNote : -1);
#if NAM_DEV_DIAGNOSTICS
            debugPhaseAttempted = phaseReferenceMidiNote >= 21;
            debugPhaseMidiNote = phaseReferenceMidiNote;
#endif
            double phaseFrequencyHz = candidate.frequencyHz;
            double phaseMidiFloat = candidate.midiFloat;
            double phasePurity = 0.0;
            if (phaseReferenceMidiNote >= 21
                && RefineLockedNotePitchFromPhase(phaseFrame, sampleRate, phaseReferenceMidiNote, phaseFrequencyHz,
                                                  phaseMidiFloat, phasePurity))
            {
              const float rawCentsForPhaseNote =
                static_cast<float>(
                  std::clamp(CentsDeltaForMidi(candidate.midiFloat, phaseReferenceMidiNote), -50.0, 50.0));
              const float phaseCents = CentsForMidi(phaseMidiFloat, phaseReferenceMidiNote);
              constexpr double kPhasePurityThreshold = 0.82;
              constexpr float kHighNotePhaseAgreementCents = 2.0f;
              const bool phaseTonePureEnough = phasePurity >= kPhasePurityThreshold;
              const bool phaseAgreesWithRaw =
                phaseReferenceMidiNote <= 50
                || std::fabs(phaseCents - rawCentsForPhaseNote) <= kHighNotePhaseAgreementCents;
#if NAM_DEV_DIAGNOSTICS
              debugPhaseEstimated = true;
              debugPhaseCents = phaseCents;
              debugPhasePurity = static_cast<float>(phasePurity);
#endif
              if (phaseTonePureEnough && phaseAgreesWithRaw)
              {
                candidate.frequencyHz = phaseFrequencyHz;
                candidate.midiFloat = phaseMidiFloat;
                candidate.midiNote = phaseReferenceMidiNote;
                candidate.cents = phaseCents;
#if NAM_DEV_DIAGNOSTICS
                debugPhaseUsed = true;
#endif
              }
            }
          }
        }

      }
    }
  }

#if NAM_DEV_DIAGNOSTICS
  mDebugCandidateValid.store(debugCandidateValid, std::memory_order_relaxed);
  mDebugPhaseAttempted.store(debugPhaseAttempted, std::memory_order_relaxed);
  mDebugPhaseEstimated.store(debugPhaseEstimated, std::memory_order_relaxed);
  mDebugPhaseUsed.store(debugPhaseUsed, std::memory_order_relaxed);
  mDebugRawMidiNote.store(debugRawMidiNote, std::memory_order_relaxed);
  mDebugPhaseMidiNote.store(debugPhaseMidiNote, std::memory_order_relaxed);
  mDebugRawCents.store(debugRawCents, std::memory_order_relaxed);
  mDebugPhaseCents.store(debugPhaseCents, std::memory_order_relaxed);
  mDebugPhasePurity.store(debugPhasePurity, std::memory_order_relaxed);
#endif

  if (candidate.rms > 0.0)
  {
    const bool hasPublishedPitch = mPublishedMidiNote >= 0;
    const double attackCentsDelta = hasPublishedPitch && candidate.valid
                                      ? CentsDeltaForMidi(candidate.midiFloat, mPublishedMidiNote)
                                      : 0.0;
    const bool attackOnset =
      hasPublishedPitch && candidate.rms > std::max(0.0025, mPreviousRms * 1.45);
    const bool attackSharpMeasurement =
      hasPublishedPitch && candidate.valid && attackCentsDelta > static_cast<double>(mDisplayCents + 1.0f)
      && candidate.rms > std::max(0.0020, mPreviousRms * 1.10);
    const bool lowAttackNote = mPublishedMidiNote <= 50;
    const bool midAttackNote = mPublishedMidiNote <= 55;
    if (attackOnset)
      mAttackSettleFrames = lowAttackNote ? 10 : (midAttackNote ? 7 : 4);
    else if (attackSharpMeasurement)
      mAttackSettleFrames = std::max(mAttackSettleFrames, lowAttackNote ? 8 : (midAttackNote ? 6 : 3));
    mPreviousRms = 0.82 * mPreviousRms + 0.18 * candidate.rms;
  }
  else
  {
    mPreviousRms *= 0.96;
  }

  auto clearPublishedPitch = [this]() {
    mHasPitch.store(false, std::memory_order_relaxed);
    mMidiNote.store(0, std::memory_order_relaxed);
    mCents.store(0.0f, std::memory_order_relaxed);
    mPublishedMidiNote = -1;
    mPendingMidiNote = -1;
    mPendingMidiCount = 0;
    mMissCount = 0;
    mDisplayCents = 0.0f;
    mDisplayVelocityCents = 0.0f;
    mFilteredTargetCents = 0.0f;
    mFilteredTargetMidiNote = -1;
    mAttackSettleFrames = 0;
  };

  auto holdPublishedPitch = [this]() {
    if (mPublishedMidiNote >= 0)
    {
      mMidiNote.store(mPublishedMidiNote, std::memory_order_relaxed);
      mCents.store(mDisplayCents, std::memory_order_relaxed);
      mHasPitch.store(true, std::memory_order_relaxed);
    }
  };

  auto publishAcceptedPitch = [this](const int midiNote, const float targetCents, const double clarity,
                                     const bool snap) {
    mPublishedMidiNote = midiNote;
    mPendingMidiNote = -1;
    mPendingMidiCount = 0;
    mMissCount = 0;

    if (snap)
    {
      mDisplayCents = targetCents;
      mDisplayVelocityCents = 0.0f;
      mFilteredTargetCents = targetCents;
      mFilteredTargetMidiNote = midiNote;
    }
    else
    {
      if (mFilteredTargetMidiNote != midiNote)
      {
        mFilteredTargetCents = targetCents;
        mFilteredTargetMidiNote = midiNote;
      }
      else if (midiNote >= 59)
      {
        const float targetDelta = targetCents - mFilteredTargetCents;
        const bool targetNearCenter =
          std::max(std::fabs(targetCents), std::fabs(mFilteredTargetCents)) <= 18.0f;
        const bool targetCrossingCenter = targetNearCenter && (targetCents * mFilteredTargetCents) < 0.0f;
        float targetAlpha = targetNearCenter ? (targetCrossingCenter ? 0.20f : 0.10f) : 0.14f;
        if (std::fabs(targetDelta) > 18.0f)
          targetAlpha = 0.22f;
        if (clarity < 0.82)
          targetAlpha *= 0.70f;

        const float maxTargetStep = targetCrossingCenter ? 1.20f : (targetNearCenter ? 0.85f : 1.8f);
        mFilteredTargetCents += std::clamp(targetAlpha * targetDelta, -maxTargetStep, maxTargetStep);
      }
      else
      {
        mFilteredTargetCents = targetCents;
      }

      float targetForDisplay = mFilteredTargetCents;
      bool attackAllowsTracking = false;
      const float attackDelta = targetForDisplay - mDisplayCents;
      const bool attackSharpBias =
        mAttackSettleFrames > 0 && attackDelta > 0.5f;
      if (attackSharpBias)
      {
        const bool alreadyMovingSharp =
          mDisplayVelocityCents > (midiNote <= 50 ? 0.020f : (midiNote <= 55 ? 0.028f : 0.035f));
        const float persistentLeadThreshold = midiNote <= 50 ? 5.0f : (midiNote <= 55 ? 3.5f : 2.0f);
        const int initialGuardFrames = midiNote <= 50 ? 8 : (midiNote <= 55 ? 5 : 3);
        const bool persistentTargetLead =
          attackDelta > persistentLeadThreshold && mAttackSettleFrames <= initialGuardFrames;
        attackAllowsTracking = alreadyMovingSharp || persistentTargetLead;

        const float baseAttackStep = midiNote <= 50 ? 0.28f : (midiNote <= 55 ? 0.36f : 0.48f);
        const float trackingAttackStep = midiNote <= 50 ? 0.80f : (midiNote <= 55 ? 1.00f : 1.20f);
        const float catchUpAttackStep = midiNote <= 50 ? 0.55f : (midiNote <= 55 ? 0.72f : 0.90f);
        const float attackMaxStep =
          alreadyMovingSharp ? trackingAttackStep : (persistentTargetLead ? catchUpAttackStep : baseAttackStep);
        targetForDisplay = std::min(targetForDisplay, mDisplayCents + attackMaxStep);
      }

      const float delta = targetForDisplay - mDisplayCents;
      const bool fineTuneRegion = std::max(std::fabs(targetForDisplay), std::fabs(mDisplayCents)) <= 22.0f;
      const bool crossingCenter = fineTuneRegion && (targetForDisplay * mDisplayCents) < 0.0f;
      const bool movingTowardCenter = crossingCenter || std::fabs(targetForDisplay) < std::fabs(mDisplayCents);
      const bool largeCorrection = std::fabs(delta) > 28.0f;
      float velocityGain = fineTuneRegion ? (movingTowardCenter ? 0.070f : 0.035f)
                                          : (movingTowardCenter ? 0.16f : 0.105f);
      if (largeCorrection)
        velocityGain = movingTowardCenter ? 0.21f : 0.15f;
      if (clarity < 0.78)
        velocityGain *= 0.70f;
      if (attackSharpBias)
      {
        const float attackTrackingGain = midiNote <= 50 ? 0.052f : (midiNote <= 55 ? 0.060f : 0.070f);
        const float attackHoldGain = midiNote <= 50 ? 0.025f : 0.040f;
        velocityGain = attackAllowsTracking ? std::max(velocityGain, attackTrackingGain)
                                            : std::min(velocityGain, attackHoldGain);
      }

      const float maxAttackVelocity = attackAllowsTracking
                                        ? (midiNote <= 50 ? 0.36f : (midiNote <= 55 ? 0.48f : 0.58f))
                                        : (midiNote <= 50 ? 0.14f : (midiNote <= 55 ? 0.18f : 0.22f));
      const float maxVelocity = attackSharpBias ? maxAttackVelocity
                                                : (movingTowardCenter ? (fineTuneRegion ? 0.70f : 2.6f)
                                                                      : (fineTuneRegion ? 0.34f : 1.8f));
      const float maxAcceleration =
        attackSharpBias ? (attackAllowsTracking
                             ? (midiNote <= 50 ? 0.070f : (midiNote <= 55 ? 0.085f : 0.105f))
                             : (midiNote <= 50 ? 0.030f : 0.05f))
                        : (fineTuneRegion ? (movingTowardCenter ? 0.075f : 0.045f)
                                          : (largeCorrection ? 0.42f : 0.28f));
      const float targetVelocity = std::clamp(velocityGain * delta, -maxVelocity, maxVelocity);
      const float acceleration = std::clamp(targetVelocity - mDisplayVelocityCents,
                                            -maxAcceleration,
                                            maxAcceleration);
      mDisplayVelocityCents += acceleration;
      if (fineTuneRegion && (mDisplayVelocityCents * delta) < 0.0f)
        mDisplayVelocityCents = 0.0f;
      if (std::fabs(mDisplayVelocityCents) > std::fabs(delta))
        mDisplayVelocityCents = delta;
      mDisplayCents += mDisplayVelocityCents;
    }

    if (mAttackSettleFrames > 0)
      --mAttackSettleFrames;

    mDisplayCents = static_cast<float>(std::clamp(static_cast<double>(mDisplayCents), -50.0, 50.0));
    mMidiNote.store(mPublishedMidiNote, std::memory_order_relaxed);
    mCents.store(mDisplayCents, std::memory_order_relaxed);
    mHasPitch.store(true, std::memory_order_relaxed);
  };

  if (!candidate.valid)
  {
    if (mPublishedMidiNote >= 0)
    {
      ++mMissCount;
      if (mMissCount <= 5)
        holdPublishedPitch();
      else
        clearPublishedPitch();
    }
    else
    {
      mPendingMidiNote = -1;
      mPendingMidiCount = 0;
      mHasPitch.store(false, std::memory_order_relaxed);
    }
    return;
  }

  const bool acquiring = mPublishedMidiNote < 0;
  if (acquiring)
  {
    if (mPendingMidiNote == candidate.midiNote)
      ++mPendingMidiCount;
    else
    {
      mPendingMidiNote = candidate.midiNote;
      mPendingMidiCount = 1;
    }

    if (mPendingMidiCount >= RequiredConfirmationsForMidi(candidate.midiNote))
      publishAcceptedPitch(candidate.midiNote, candidate.cents, candidate.clarity, true);
    else
      mHasPitch.store(false, std::memory_order_relaxed);
    return;
  }

  const double centsDeltaAgainstPublished = CentsDeltaForMidi(candidate.midiFloat, mPublishedMidiNote);
  const bool candidateMatchesPublished = std::fabs(centsDeltaAgainstPublished) <= 58.0;
  if (candidateMatchesPublished)
  {
    const float centsAgainstPublished =
      static_cast<float>(std::clamp(centsDeltaAgainstPublished, -50.0, 50.0));
    publishAcceptedPitch(mPublishedMidiNote, centsAgainstPublished, candidate.clarity, false);
    return;
  }

  if (mPendingMidiNote == candidate.midiNote)
    ++mPendingMidiCount;
  else
  {
    mPendingMidiNote = candidate.midiNote;
    mPendingMidiCount = 1;
  }

  if (mPendingMidiCount >= RequiredConfirmationsForMidi(candidate.midiNote))
    publishAcceptedPitch(candidate.midiNote, candidate.cents, candidate.clarity, true);
  else
    holdPublishedPitch();
}
