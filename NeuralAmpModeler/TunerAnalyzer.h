#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

class TunerAnalyzer
{
public:
  void Reset() noexcept;
  void Update(double pluginSampleRate) noexcept;

  template <typename SampleT>
  void PushInputMono(const SampleT* input, const size_t numFrames) noexcept
  {
    if (!input || numFrames == 0)
      return;
    uint32_t writeIndex = mWriteIndex.load(std::memory_order_relaxed);
    for (size_t i = 0; i < numFrames; ++i)
    {
      mBuffer[writeIndex & (kBufferSize - 1)] = static_cast<float>(input[i]);
      ++writeIndex;
    }
    mWriteIndex.store(writeIndex, std::memory_order_relaxed);
  }

  bool HasPitch() const noexcept { return mHasPitch.load(std::memory_order_relaxed); }
  int MidiNote() const noexcept { return mMidiNote.load(std::memory_order_relaxed); }
  float Cents() const noexcept { return mCents.load(std::memory_order_relaxed); }

private:
  static constexpr uint32_t kBufferSize = 8192; // power-of-two for wrap mask
  static constexpr int kAnalysisSize = 2048;
  static_assert((kBufferSize & (kBufferSize - 1)) == 0, "kBufferSize must be power-of-two");

  std::array<float, kBufferSize> mBuffer{};
  std::atomic<uint32_t> mWriteIndex = 0;
  std::atomic<bool> mHasPitch = false;
  std::atomic<int> mMidiNote = 0;
  std::atomic<float> mCents = 0.0f;
  int mAnalysisDecim = 0;
  int mHoldFrames = 0;
  float mSmoothedFrequencyHz = 0.0f;
  float mSmoothedCents = 0.0f;
  std::array<float, 5> mFrequencyHistory{};
  int mFrequencyHistoryCount = 0;
  int mFrequencyHistoryIndex = 0;
  int mLockedMidiNote = -1;
  int mNeedleHoldFrames = 0;
  float mLastDetectedFrequencyHz = 0.0f;
  int mStableDetections = 0;
  double mPrevRms = 0.0;
  int mAttackIgnoreFrames = 0;
};
