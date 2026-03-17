#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

// Backend selection:
// 1) If Rubber Band Live headers exist, use Rubber Band.
// 2) Else if NAM_USE_ZPLANE_TRANSPOSE is defined and a zplane adapter header exists, use zplane.
// 3) Otherwise, use the fallback backend (Signalsmith).
#if __has_include("third_party/rubberband/rubberband/RubberBandLiveShifter.h")

#include "third_party/rubberband/rubberband/RubberBandLiveShifter.h"

class LightweightTransposeShifter
{
public:
  void Reset(const double sampleRate, const int maxBlockSize)
  {
    // Instrument-focused tuning:
    // - Short window keeps latency as low as Rubber Band Live allows.
    // - Formant shifted avoids vocal-style formant preservation artifacts on guitar/bass.
    constexpr auto options = RubberBand::RubberBandLiveShifter::OptionWindowShort
                              | RubberBand::RubberBandLiveShifter::OptionFormantShifted;

    const size_t safeSampleRate = static_cast<size_t>(std::max(1.0, sampleRate));
    mShifter = std::make_unique<RubberBand::RubberBandLiveShifter>(safeSampleRate, 1, options);
    mShifter->setPitchScale(1.0);
    mStartDelaySamples = mShifter->getStartDelay();

    mShiftBlockSize = std::max<size_t>(1, mShifter->getBlockSize());
    const size_t scratchSize = std::max(static_cast<size_t>(std::max(64, maxBlockSize)), mShiftBlockSize);

    mInputBlock.assign(mShiftBlockSize, 0.0f);
    mOutputBlock.assign(mShiftBlockSize, 0.0f);
    mScratch.assign(scratchSize, 0.0f);

    mInputChannels[0] = mInputBlock.data();
    mOutputChannels[0] = mOutputBlock.data();

    const size_t crossfadeSamples = std::max<size_t>(1, static_cast<size_t>(sampleRate * 0.010)); // 10 ms
    mCrossfadeStep = 1.0f / static_cast<float>(crossfadeSamples);
    ResetState();
  }

  void ResetState()
  {
    if (mShifter != nullptr)
      mShifter->reset();

    std::fill(mInputBlock.begin(), mInputBlock.end(), 0.0f);
    std::fill(mOutputBlock.begin(), mOutputBlock.end(), 0.0f);
    std::fill(mScratch.begin(), mScratch.end(), 0.0f);
    mInputFill = 0;
    mOutputFill = 0;
    mOutputRead = 0;
    mCurrentSemitones = 0;
    mWasActive = false;
    mPrimeRemaining = 0;
    mWetMix = 0.0f;
  }

  template <typename SampleType>
  void ProcessBlock(SampleType* io, const size_t nFrames, const int semitones)
  {
    if (io == nullptr || nFrames == 0 || mShifter == nullptr || mShiftBlockSize == 0 || mInputBlock.empty()
        || mOutputBlock.empty())
      return;

    const int clampedSemitones = std::clamp(semitones, -8, 8);
    const bool wantsWet = (clampedSemitones != 0);
    if (!wantsWet && !mWasActive && mWetMix <= 0.0f)
      return;

    if (wantsWet && (!mWasActive || clampedSemitones != mCurrentSemitones))
    {
      if (!mWasActive)
      {
        mShifter->reset();
        mInputFill = 0;
        mOutputFill = 0;
        mOutputRead = 0;
        std::fill(mInputBlock.begin(), mInputBlock.end(), 0.0f);
        std::fill(mOutputBlock.begin(), mOutputBlock.end(), 0.0f);
        // Hold dry for the shifter's lookahead before crossfading to wet.
        mPrimeRemaining = mStartDelaySamples;
      }
      mShifter->setPitchScale(std::pow(2.0, static_cast<double>(clampedSemitones) / 12.0));
      mCurrentSemitones = clampedSemitones;
      mWasActive = true;
    }

    size_t offset = 0;
    while (offset < nFrames)
    {
      const size_t chunk = std::min(mScratch.size(), nFrames - offset);
      for (size_t i = 0; i < chunk; ++i)
      {
        const float dry = static_cast<float>(io[offset + i]);
        float wet = 0.0f;

        // Keep the shifter fed while active/fading-in/fading-out.
        if (wantsWet || mWasActive || mWetMix > 0.0f)
        {
          mInputBlock[mInputFill++] = dry;
          if (mInputFill == mShiftBlockSize)
          {
            mShifter->shift(mInputChannels, mOutputChannels);
            mInputFill = 0;
            mOutputRead = 0;
            mOutputFill = mShiftBlockSize;
          }

          if (mOutputFill > 0)
          {
            wet = mOutputBlock[mOutputRead++];
            --mOutputFill;
          }
        }

        const bool priming = wantsWet && (mPrimeRemaining > 0);
        if (priming)
          --mPrimeRemaining;

        const float targetWetMix = (wantsWet && !priming) ? 1.0f : 0.0f;
        if (mWetMix < targetWetMix)
          mWetMix = std::min(targetWetMix, mWetMix + mCrossfadeStep);
        else if (mWetMix > targetWetMix)
          mWetMix = std::max(targetWetMix, mWetMix - mCrossfadeStep);

        const float out = (1.0f - mWetMix) * dry + mWetMix * wet;
        io[offset + i] = static_cast<SampleType>(out);
      }

      offset += chunk;
    }

    if (!wantsWet && mWetMix <= 0.0f)
      mWasActive = false;
  }

private:
  std::unique_ptr<RubberBand::RubberBandLiveShifter> mShifter;
  std::vector<float> mInputBlock;
  std::vector<float> mOutputBlock;
  std::vector<float> mScratch;
  const float* mInputChannels[1] = {nullptr};
  float* mOutputChannels[1] = {nullptr};
  size_t mShiftBlockSize = 0;
  size_t mInputFill = 0;
  size_t mOutputFill = 0;
  size_t mOutputRead = 0;
  size_t mStartDelaySamples = 0;
  size_t mPrimeRemaining = 0;
  int mCurrentSemitones = 0;
  float mCrossfadeStep = 1.0f;
  float mWetMix = 0.0f;
  bool mWasActive = false;
};

#elif defined(NAM_USE_ZPLANE_TRANSPOSE) && __has_include("third_party/zplane/ZplaneTransposeAdapter.h")

#include "third_party/zplane/ZplaneTransposeAdapter.h"

class LightweightTransposeShifter
{
public:
  void Reset(const double sampleRate, const int maxBlockSize)
  {
    mAdapter.Reset(sampleRate, maxBlockSize);
    mScratch.assign(static_cast<size_t>(std::max(64, maxBlockSize)), 0.0f);
  }
  void ResetState() { mAdapter.ResetState(); }

  template <typename SampleType>
  void ProcessBlock(SampleType* io, const size_t nFrames, const int semitones)
  {
    if (io == nullptr || nFrames == 0 || mScratch.empty())
      return;

    size_t offset = 0;
    while (offset < nFrames)
    {
      const size_t chunk = std::min(mScratch.size(), nFrames - offset);
      for (size_t i = 0; i < chunk; ++i)
        mScratch[i] = static_cast<float>(io[offset + i]);
      mAdapter.ProcessBlock(mScratch.data(), chunk, semitones);
      for (size_t i = 0; i < chunk; ++i)
        io[offset + i] = static_cast<SampleType>(mScratch[i]);
      offset += chunk;
    }
  }

private:
  ZplaneTransposeAdapter mAdapter;
  std::vector<float> mScratch;
};

#else

#include "third_party/signalsmith-stretch/signalsmith-stretch.h"

class LightweightTransposeShifter
{
public:
  void Reset(const double sampleRate, const int maxBlockSize)
  {
    mSampleRate = static_cast<float>(sampleRate);
    const int scratchSize = std::max(64, maxBlockSize);
    mInputBuffer.assign(static_cast<size_t>(scratchSize), 0.0f);
    mOutputBuffer.assign(static_cast<size_t>(scratchSize), 0.0f);

    // Live-quality config: much lower latency than presetDefault(), while keeping acceptable quality.
    // Increase block/interval to improve quality; decrease to improve responsiveness.
    constexpr int channels = 1;
    constexpr int kStretchBlockSamples = 8192;
    constexpr int kStretchIntervalSamples = 128;
    mStretch.configure(channels, kStretchBlockSamples, kStretchIntervalSamples, false);
    mStretch.setTransposeSemitones(0.0f);

    ResetState();
  }

  void ResetState()
  {
    std::fill(mInputBuffer.begin(), mInputBuffer.end(), 0.0f);
    std::fill(mOutputBuffer.begin(), mOutputBuffer.end(), 0.0f);
    mStretch.reset();
    mWasActive = false;
  }

  template <typename SampleType>
  void ProcessBlock(SampleType* io, const size_t nFrames, const int semitones)
  {
    if (io == nullptr || nFrames == 0 || mInputBuffer.empty() || mOutputBuffer.empty())
      return;

    const int clampedSemitones = std::clamp(semitones, -8, 8);
    if (clampedSemitones == 0)
    {
      mWasActive = false;
      return;
    }

    if (!mWasActive)
    {
      mStretch.reset();
      mWasActive = true;
    }

    // Keep pure semitone transposition map (no non-linear tonality remap).
    mStretch.setTransposeSemitones(static_cast<float>(clampedSemitones));

    size_t offset = 0;
    while (offset < nFrames)
    {
      const size_t chunk = std::min(mInputBuffer.size(), nFrames - offset);
      for (size_t i = 0; i < chunk; ++i)
        mInputBuffer[i] = static_cast<float>(io[offset + i]);

      mInputChannels[0] = mInputBuffer.data();
      mOutputChannels[0] = mOutputBuffer.data();
      mStretch.process(mInputChannels, static_cast<int>(chunk), mOutputChannels, static_cast<int>(chunk));

      for (size_t i = 0; i < chunk; ++i)
        io[offset + i] = static_cast<SampleType>(mOutputBuffer[i]);

      offset += chunk;
    }
  }

private:
  signalsmith::stretch::SignalsmithStretch<float> mStretch;
  std::vector<float> mInputBuffer;
  std::vector<float> mOutputBuffer;
  float* mInputChannels[1] = {nullptr};
  float* mOutputChannels[1] = {nullptr};
  float mSampleRate = 48000.0f;
  bool mWasActive = false;
};

#endif
