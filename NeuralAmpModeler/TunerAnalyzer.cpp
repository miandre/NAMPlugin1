#include "TunerAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace
{
float _MedianFromHistory(const std::array<float, 5>& history, const int count)
{
  if (count <= 0)
    return 0.0f;
  std::array<float, 5> sorted = history;
  std::sort(sorted.begin(), sorted.begin() + count);
  return sorted[static_cast<size_t>(count / 2)];
}
} // namespace

void TunerAnalyzer::Reset() noexcept
{
  mHasPitch.store(false, std::memory_order_relaxed);
  mMidiNote.store(0, std::memory_order_relaxed);
  mCents.store(0.0f, std::memory_order_relaxed);
  mWriteIndex.store(0, std::memory_order_relaxed);
  mHoldFrames = 0;
  mSmoothedFrequencyHz = 0.0f;
  mSmoothedCents = 0.0f;
  mFrequencyHistory.fill(0.0f);
  mFrequencyHistoryCount = 0;
  mFrequencyHistoryIndex = 0;
  mLockedMidiNote = -1;
  mNeedleHoldFrames = 0;
  mLastDetectedFrequencyHz = 0.0f;
  mStableDetections = 0;
  mPrevRms = 0.0;
  mAttackIgnoreFrames = 0;
  mPostAttackSettleFrames = 0;
}

void TunerAnalyzer::Update(const double pluginSampleRate) noexcept
{
  if (pluginSampleRate <= 0.0)
    return;

  constexpr int kTunerDownsample = 4;
  constexpr double kTunerMinHz = 24.0;
  constexpr double kTunerMaxHz = 450.0;
  constexpr double kTunerLowPassHz = 1400.0;
  constexpr int kTunerHistoryWindow = 3;
  constexpr double kPi = 3.14159265358979323846;
  bool pitchValid = false;
  double rawRms = 0.0;
  const uint32_t writeIndex = mWriteIndex.load(std::memory_order_relaxed);
  const uint32_t requiredSamples = static_cast<uint32_t>(kAnalysisSize * kTunerDownsample + 4);
  if (writeIndex > requiredSamples)
  {
    std::array<float, kAnalysisSize> x{};
    constexpr uint32_t kMask = kBufferSize - 1;
    const uint32_t start = writeIndex - static_cast<uint32_t>(kAnalysisSize * kTunerDownsample);

    double rawSumSq = 0.0;
    for (int i = 0; i < kAnalysisSize; ++i)
    {
      const uint32_t readIndex = start + static_cast<uint32_t>(i * kTunerDownsample);
      float sample = 0.0f;
      for (int ds = 0; ds < kTunerDownsample; ++ds)
        sample += mBuffer[(readIndex + static_cast<uint32_t>(ds)) & kMask];
      sample /= static_cast<float>(kTunerDownsample);
      x[static_cast<size_t>(i)] = sample;
      rawSumSq += static_cast<double>(sample) * static_cast<double>(sample);
    }

    rawRms = std::sqrt(rawSumSq / static_cast<double>(kAnalysisSize));
    const double onsetThreshold = std::max(0.004, mPrevRms * 1.5);
    const bool strongOnset = rawRms > std::max(0.008, mPrevRms * 2.2);
    const bool hadTrackedPitch =
      mHasPitch.load(std::memory_order_relaxed) && mLockedMidiNote >= 0 && mSmoothedFrequencyHz > 0.0f;
    if (rawRms > onsetThreshold)
    {
      // Briefly ignore attack transients. On a strong onset, clear old-note inertia so
      // a new string can be acquired quickly instead of easing in from the last pitch.
      mAttackIgnoreFrames = strongOnset ? 3 : 1;
      mPostAttackSettleFrames = strongOnset ? 4 : 2;
      if (strongOnset)
      {
        if (!hadTrackedPitch)
          mLockedMidiNote = -1;
        mFrequencyHistoryCount = 0;
        mFrequencyHistoryIndex = 0;
        mLastDetectedFrequencyHz = 0.0f;
        mStableDetections = 0;
        if (!hadTrackedPitch)
        {
          mSmoothedFrequencyHz = 0.0f;
          mSmoothedCents = 0.0f;
        }
        mNeedleHoldFrames = std::max(mNeedleHoldFrames, 1);
      }
    }
    mPrevRms = 0.85 * mPrevRms + 0.15 * rawRms;

    if (mAttackIgnoreFrames > 0)
    {
      --mAttackIgnoreFrames;
    }
    else if (rawRms > 0.0014)
    {
      const double sampleRate = pluginSampleRate / static_cast<double>(kTunerDownsample);
      const double oneMinusAlpha = 1.0 - std::exp(-2.0 * kPi * kTunerLowPassHz / sampleRate);

      // Precondition the frame for more stable pitch picks:
      // remove DC, apply Hann window, then lightly low-pass.
      double mean = 0.0;
      for (int i = 0; i < kAnalysisSize; ++i)
        mean += static_cast<double>(x[static_cast<size_t>(i)]);
      mean /= static_cast<double>(kAnalysisSize);

      double conditionedSumSq = 0.0;
      float lpState = 0.0f;
      for (int i = 0; i < kAnalysisSize; ++i)
      {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) /
                                              static_cast<double>(kAnalysisSize - 1));
        const float centered = static_cast<float>((static_cast<double>(x[static_cast<size_t>(i)]) - mean) * w);
        lpState = static_cast<float>(
          static_cast<double>(lpState) + oneMinusAlpha * (static_cast<double>(centered) - lpState));
        x[static_cast<size_t>(i)] = lpState;
        conditionedSumSq += static_cast<double>(lpState) * static_cast<double>(lpState);
      }

      const double conditionedRms = std::sqrt(conditionedSumSq / static_cast<double>(kAnalysisSize));
      if (conditionedRms >= 0.0008)
      {
        const int minLag = std::max(1, static_cast<int>(sampleRate / kTunerMaxHz));
        const int maxLag =
          std::min(kAnalysisSize / 2, std::max(minLag + 1, static_cast<int>(sampleRate / kTunerMinHz)));
        auto correlationAtLag = [&](const int lag) {
          const int nMax = kAnalysisSize - lag;
          double sumXY = 0.0;
          double sumXX = 0.0;
          double sumYY = 0.0;
          for (int n = 0; n < nMax; ++n)
          {
            const double a = static_cast<double>(x[static_cast<size_t>(n)]);
            const double b = static_cast<double>(x[static_cast<size_t>(n + lag)]);
            sumXY += a * b;
            sumXX += a * a;
            sumYY += b * b;
          }
          const double denom = std::sqrt(sumXX * sumYY + 1e-20);
          return denom > 0.0 ? (sumXY / denom) : 0.0;
        };

        int bestLag = minLag;
        double bestCorr = -1.0;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
          const double corr = correlationAtLag(lag);
          if (corr > bestCorr)
          {
            bestCorr = corr;
            bestLag = lag;
          }
        }
        const bool initialHighCandidate = bestLag < static_cast<int>(sampleRate / 180.0);
        if (bestLag * 2 <= maxLag)
        {
          const double corr2 = correlationAtLag(bestLag * 2);
          const double octavePromotionRatio = initialHighCandidate ? 0.975 : 0.93;
          if (corr2 > bestCorr * octavePromotionRatio)
          {
            bestLag *= 2;
            bestCorr = corr2;
          }
        }

        const bool lowCandidate = bestLag > static_cast<int>(sampleRate / 90.0);
        const bool highCandidate = bestLag < static_cast<int>(sampleRate / 180.0);
        const double corrThreshold = lowCandidate ? 0.60 : (highCandidate ? 0.62 : 0.68);
        if (bestCorr > corrThreshold)
        {
          double refinedLag = static_cast<double>(bestLag);
          if (bestLag > minLag && bestLag < maxLag)
          {
            const double yPrev = correlationAtLag(bestLag - 1);
            const double y0 = correlationAtLag(bestLag);
            const double yNext = correlationAtLag(bestLag + 1);
            const double denom = (yPrev - 2.0 * y0 + yNext);
            if (std::fabs(denom) > 1e-12)
            {
              const double delta = std::clamp(0.5 * (yPrev - yNext) / denom, -0.5, 0.5);
              refinedLag += delta;
            }
          }

          const double frequency = sampleRate / std::max(1.0, refinedLag);
          const bool trackingLocked =
            mHasPitch.load(std::memory_order_relaxed) && mLockedMidiNote >= 0 && mStableDetections >= 2;
          const double detectedMidiFloat = 69.0 + 12.0 * std::log2(std::max(1e-6, frequency) / 440.0);
          const double semitoneJump =
            trackingLocked ? std::fabs(detectedMidiFloat - static_cast<double>(mLockedMidiNote)) : 0.0;
          const bool isLargeJump =
            (mSmoothedFrequencyHz > 0.0f) &&
            (frequency < 0.40 * static_cast<double>(mSmoothedFrequencyHz) ||
             frequency > 2.50 * static_cast<double>(mSmoothedFrequencyHz));
          const bool isTrackedOutlier =
            trackingLocked && !strongOnset && semitoneJump > 2.25 && bestCorr < (highCandidate ? 0.80 : 0.82);

          if (!isLargeJump && !isTrackedOutlier)
          {
            bool plausible = true;
            if (mLastDetectedFrequencyHz > 0.0f)
            {
              const double ratio = frequency / static_cast<double>(mLastDetectedFrequencyHz);
              const bool lowTracked = (mSmoothedFrequencyHz > 0.0f && mSmoothedFrequencyHz < 90.0f);
              plausible = lowTracked ? (ratio > 0.35 && ratio < 2.80) : (ratio > 0.50 && ratio < 2.00);
              if (ratio > 0.90 && ratio < 1.11)
                ++mStableDetections;
              else
                mStableDetections = 1;
            }
            else
            {
              mStableDetections = 1;
            }
            mLastDetectedFrequencyHz = static_cast<float>(frequency);

            if (plausible)
            {
              mFrequencyHistory[static_cast<size_t>(mFrequencyHistoryIndex)] = static_cast<float>(frequency);
              mFrequencyHistoryIndex = (mFrequencyHistoryIndex + 1) % kTunerHistoryWindow;
              if (mFrequencyHistoryCount < kTunerHistoryWindow)
                ++mFrequencyHistoryCount;

              const float medianHz = _MedianFromHistory(mFrequencyHistory, mFrequencyHistoryCount);
              const float prevHz = mSmoothedFrequencyHz;
              const bool acquiringPitch =
                !mHasPitch.load(std::memory_order_relaxed) || mStableDetections < 2 || mLockedMidiNote < 0;
              if (prevHz > 0.0f)
              {
                const float relDiff = std::fabs(medianHz - prevHz) / std::max(1.0f, prevHz);
                float alphaHz = acquiringPitch ? 0.72f : 0.24f;
                if (relDiff > 0.12f)
                  alphaHz = acquiringPitch ? 0.88f : 0.68f;
                else if (relDiff > 0.05f)
                  alphaHz = acquiringPitch ? 0.80f : 0.50f;
                mSmoothedFrequencyHz = (1.0f - alphaHz) * prevHz + alphaHz * medianHz;
              }
              else
              {
                mSmoothedFrequencyHz = medianHz;
              }

              const double midiFloat =
                69.0 + 12.0 * std::log2(std::max(1e-6, static_cast<double>(mSmoothedFrequencyHz)) / 440.0);
              const int previousLockedMidi = mLockedMidiNote;
              int candidateMidi = previousLockedMidi;
              if (candidateMidi < 0 || candidateMidi > 127)
              {
                candidateMidi = static_cast<int>(std::lround(midiFloat));
              }
              else
              {
                const double noteChangeThreshold = acquiringPitch ? 0.34 : 0.50;
                if (midiFloat > static_cast<double>(candidateMidi) + noteChangeThreshold
                    || midiFloat < static_cast<double>(candidateMidi) - noteChangeThreshold)
                  candidateMidi = static_cast<int>(std::lround(midiFloat));
              }

              int midi = candidateMidi;
              if (midi >= 0 && midi <= 127)
              {
                const bool noteChanged = (previousLockedMidi >= 0 && midi != previousLockedMidi);
                if (noteChanged)
                {
                  // Reset frequency-memory anchors to the newly selected note, but do not hold the
                  // display for long; the goal is a fast, decisive string change.
                  mNeedleHoldFrames = std::max(mNeedleHoldFrames, 1);
                  mSmoothedFrequencyHz = static_cast<float>(frequency);
                  mLastDetectedFrequencyHz = static_cast<float>(frequency);
                  mFrequencyHistory.fill(0.0f);
                  mFrequencyHistory[0] = static_cast<float>(frequency);
                  mFrequencyHistoryCount = 1;
                  mFrequencyHistoryIndex = 1 % kTunerHistoryWindow;
                }
                mLockedMidiNote = midi;
                const float centsRaw =
                  static_cast<float>(std::clamp(100.0 * (midiFloat - static_cast<double>(midi)), -50.0, 50.0));
                float displayCents = mCents.load(std::memory_order_relaxed);
                if (mNeedleHoldFrames > 0)
                {
                  --mNeedleHoldFrames;
                  displayCents = mSmoothedCents;
                }
                else if (mHasPitch.load(std::memory_order_relaxed))
                {
                  const bool settling = acquiringPitch || noteChanged || mStableDetections < 3;
                  const bool highTracked = (midi >= 64) || (mSmoothedFrequencyHz > 150.0f);
                  const bool weakTracking = !settling && (bestCorr - corrThreshold) < (highTracked ? 0.12 : 0.10);
                  const bool farFromCenter =
                    !settling && (std::max(std::fabs(centsRaw), std::fabs(mSmoothedCents)) > 12.0f);
                  float centsTarget = centsRaw;
                  if (mPostAttackSettleFrames > 0)
                  {
                    // Right after a pick attack, keep the cents estimate from jumping too far in
                    // a single update. This targets sharp attack spikes without slowing the whole tuner.
                    const float maxAttackStep = highTracked ? 5.0f : 6.0f;
                    const float limitedDelta =
                      std::clamp(centsTarget - mSmoothedCents, -maxAttackStep, maxAttackStep);
                    centsTarget = mSmoothedCents + limitedDelta;
                  }
                  else if (!settling)
                  {
                    // Once the note is locked, avoid large same-note cents jumps from a single noisy
                    // estimate. This keeps tuning movement smoother without slowing acquisition.
                    const float maxCentsStep = weakTracking
                                                 ? (farFromCenter ? (highTracked ? 1.6f : 2.1f)
                                                                  : (highTracked ? 2.5f : 3.25f))
                                                 : (farFromCenter ? (highTracked ? 2.2f : 2.8f)
                                                                  : (highTracked ? 4.0f : 5.0f));
                    const float limitedDelta = std::clamp(centsRaw - mSmoothedCents, -maxCentsStep, maxCentsStep);
                    centsTarget = mSmoothedCents + limitedDelta;
                  }

                  const float centsDelta = std::fabs(centsTarget - mSmoothedCents);
                  const float alphaCents =
                    settling ? (centsDelta > 8.0f ? 0.82f : 0.62f)
                             : (weakTracking ? (farFromCenter ? (highTracked ? 0.08f : 0.10f)
                                                              : (highTracked ? 0.14f : 0.18f))
                                             : (farFromCenter ? (highTracked ? 0.14f : 0.18f)
                                                              : (highTracked ? (centsDelta > 5.0f ? 0.32f : 0.18f)
                                                                             : (centsDelta > 6.0f ? 0.38f : 0.22f))));
                  const float maxAnalyzerStep = farFromCenter ? (highTracked ? 2.5f : 3.0f)
                                                              : (highTracked ? 4.5f : 5.5f);
                  const float smoothedStep = alphaCents * (centsTarget - mSmoothedCents);
                  mSmoothedCents += std::clamp(smoothedStep, -maxAnalyzerStep, maxAnalyzerStep);
                  displayCents = mSmoothedCents;
                }
                else
                {
                  mSmoothedCents = centsRaw;
                  displayCents = mSmoothedCents;
                }

                mMidiNote.store(midi, std::memory_order_relaxed);
                mCents.store(displayCents, std::memory_order_relaxed);
                pitchValid = true;
                if (mPostAttackSettleFrames > 0)
                  --mPostAttackSettleFrames;
              }
            }
          }
        }
      }
    }
  }

  if (pitchValid)
  {
    const bool lowNote = (mSmoothedFrequencyHz > 0.0f && mSmoothedFrequencyHz < 90.0f);
    mHoldFrames = lowNote ? 16 : 10;
    mHasPitch.store(true, std::memory_order_relaxed);
  }
  else
  {
    if (mHasPitch.load(std::memory_order_relaxed))
    {
      const bool lowNote = (mSmoothedFrequencyHz > 0.0f && mSmoothedFrequencyHz < 90.0f);
      const double signalKeepThreshold = lowNote ? 0.0009 : 0.0016;
      if (rawRms > signalKeepThreshold)
        mHoldFrames = std::max(mHoldFrames, lowNote ? 8 : 6);
      else if (rawRms < 0.00035)
        mHoldFrames = std::min(mHoldFrames, 2);
    }

    if (mHoldFrames > 0)
    {
      --mHoldFrames;
    }
    else
    {
      mHasPitch.store(false, std::memory_order_relaxed);
      mSmoothedFrequencyHz = 0.0f;
      mSmoothedCents = 0.0f;
      mFrequencyHistoryCount = 0;
      mFrequencyHistoryIndex = 0;
      mLockedMidiNote = -1;
      mNeedleHoldFrames = 0;
      mLastDetectedFrequencyHz = 0.0f;
      mStableDetections = 0;
      mPostAttackSettleFrames = 0;
    }
  }
}
