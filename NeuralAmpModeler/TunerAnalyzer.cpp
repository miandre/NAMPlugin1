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
  mAnalysisDecim = 0;
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
}

void TunerAnalyzer::Update(const double pluginSampleRate) noexcept
{
  if (pluginSampleRate <= 0.0)
    return;

  mAnalysisDecim = (mAnalysisDecim + 1) % 2; // Analyze at ~30 Hz
  if (mAnalysisDecim != 0)
    return;

  constexpr int kTunerDownsample = 4;
  constexpr double kTunerMinHz = 24.0;
  constexpr double kTunerMaxHz = 350.0;
  constexpr double kTunerLowPassHz = 900.0;
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
    if (rawRms > onsetThreshold)
    {
      // Briefly ignore attack transients; use a slightly longer gate on strong plucks.
      mAttackIgnoreFrames = strongOnset ? 2 : 1;
      if (strongOnset)
      {
        // New pluck/string transition: clear short-term frequency memory so previous-string
        // inertia does not pull early estimates.
        if (!mHasPitch.load(std::memory_order_relaxed))
          mLockedMidiNote = -1;
        mFrequencyHistoryCount = 0;
        mFrequencyHistoryIndex = 0;
        mLastDetectedFrequencyHz = 0.0f;
        mStableDetections = 0;
        mNeedleHoldFrames = std::max(mNeedleHoldFrames, 3);
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
        if (bestLag * 2 <= maxLag)
        {
          const double corr2 = correlationAtLag(bestLag * 2);
          if (corr2 > bestCorr * 0.93)
          {
            bestLag *= 2;
            bestCorr = corr2;
          }
        }

        const bool lowCandidate = bestLag > static_cast<int>(sampleRate / 90.0);
        const double corrThreshold = lowCandidate ? 0.60 : 0.68;
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
          const bool isLargeJump =
            (mSmoothedFrequencyHz > 0.0f) &&
            (frequency < 0.40 * static_cast<double>(mSmoothedFrequencyHz) ||
             frequency > 2.50 * static_cast<double>(mSmoothedFrequencyHz));

          if (!isLargeJump)
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
              if (prevHz > 0.0f)
              {
                const float relDiff = std::fabs(medianHz - prevHz) / std::max(1.0f, prevHz);
                float alphaHz = 0.18f;
                if (relDiff > 0.12f)
                  alphaHz = 0.60f;
                else if (relDiff > 0.05f)
                  alphaHz = 0.45f;
                mSmoothedFrequencyHz = (1.0f - alphaHz) * prevHz + alphaHz * medianHz;
              }
              else
              {
                mSmoothedFrequencyHz = medianHz;
              }

              const double midiFloat =
                69.0 + 12.0 * std::log2(std::max(1e-6, static_cast<double>(mSmoothedFrequencyHz)) / 440.0);
              const int previousLockedMidi = mLockedMidiNote;
              int midi = previousLockedMidi;
              if (midi < 0 || midi > 127)
              {
                midi = static_cast<int>(std::lround(midiFloat));
              }
              else
              {
                if (midiFloat > static_cast<double>(midi) + 0.58 || midiFloat < static_cast<double>(midi) - 0.58)
                  midi = static_cast<int>(std::lround(midiFloat));
              }

              if (midi >= 0 && midi <= 127)
              {
                const bool noteChanged = (previousLockedMidi >= 0 && midi != previousLockedMidi);
                if (noteChanged)
                {
                  // Keep needle steady briefly when changing strings/notes to avoid sharp->flat swing,
                  // and reset frequency-memory anchors to the newly selected note.
                  mNeedleHoldFrames = std::max(mNeedleHoldFrames, 3);
                  mSmoothedCents = 0.0f;
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
                  const float centsDelta = std::fabs(centsRaw - mSmoothedCents);
                  const float alphaCents = centsDelta > 10.0f ? 0.55f : 0.28f;
                  mSmoothedCents = (1.0f - alphaCents) * mSmoothedCents + alphaCents * centsRaw;
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
    mHoldFrames = lowNote ? 18 : 10;
    mHasPitch.store(true, std::memory_order_relaxed);
  }
  else
  {
    if (mHasPitch.load(std::memory_order_relaxed))
    {
      const bool lowNote = (mSmoothedFrequencyHz > 0.0f && mSmoothedFrequencyHz < 90.0f);
      const double signalKeepThreshold = lowNote ? 0.0009 : 0.0016;
      if (rawRms > signalKeepThreshold)
        mHoldFrames = std::max(mHoldFrames, lowNote ? 8 : 5);
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
    }
  }
}
