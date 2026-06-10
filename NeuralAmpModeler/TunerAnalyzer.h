#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "config.h"

#if NAM_DEV_DIAGNOSTICS
struct TunerAnalyzerDebugSnapshot
{
  bool candidateValid = false;
  bool phaseAttempted = false;
  bool phaseEstimated = false;
  bool phaseUsed = false;
  int rawMidiNote = -1;
  int phaseMidiNote = -1;
  float rawCents = 0.0f;
  float phaseCents = 0.0f;
  float phasePurity = 0.0f;
};
#endif

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
    mWriteIndex.store(writeIndex, std::memory_order_release);
  }

  bool HasPitch() const noexcept { return mHasPitch.load(std::memory_order_relaxed); }
  int MidiNote() const noexcept { return mMidiNote.load(std::memory_order_relaxed); }
  float Cents() const noexcept { return mCents.load(std::memory_order_relaxed); }
#if NAM_DEV_DIAGNOSTICS
  TunerAnalyzerDebugSnapshot DebugSnapshot() const noexcept
  {
    return {mDebugCandidateValid.load(std::memory_order_relaxed),
            mDebugPhaseAttempted.load(std::memory_order_relaxed),
            mDebugPhaseEstimated.load(std::memory_order_relaxed),
            mDebugPhaseUsed.load(std::memory_order_relaxed),
            mDebugRawMidiNote.load(std::memory_order_relaxed),
            mDebugPhaseMidiNote.load(std::memory_order_relaxed),
            mDebugRawCents.load(std::memory_order_relaxed),
            mDebugPhaseCents.load(std::memory_order_relaxed),
            mDebugPhasePurity.load(std::memory_order_relaxed)};
  }
#endif

private:
  static constexpr uint32_t kBufferSize = 65536; // power-of-two for wrap mask
  static constexpr int kAnalysisSize = 8192;
  static_assert((kBufferSize & (kBufferSize - 1)) == 0, " kBufferSize must be power-of-two");

  std::array<float, kBufferSize> mBuffer{};
  std::atomic<uint32_t> mWriteIndex = 0;
  std::atomic<bool> mHasPitch = false;
  std::atomic<int> mMidiNote = 0;
  std::atomic<float> mCents = 0.0f;
#if NAM_DEV_DIAGNOSTICS
  std::atomic<bool> mDebugCandidateValid = false;
  std::atomic<bool> mDebugPhaseAttempted = false;
  std::atomic<bool> mDebugPhaseEstimated = false;
  std::atomic<bool> mDebugPhaseUsed = false;
  std::atomic<int> mDebugRawMidiNote = -1;
  std::atomic<int> mDebugPhaseMidiNote = -1;
  std::atomic<float> mDebugRawCents = 0.0f;
  std::atomic<float> mDebugPhaseCents = 0.0f;
  std::atomic<float> mDebugPhasePurity = 0.0f;
#endif
  int mPublishedMidiNote = -1;
  int mPendingMidiNote = -1;
  int mPendingMidiCount = 0;
  int mMissCount = 0;
  float mDisplayCents = 0.0f;
  float mDisplayVelocityCents = 0.0f;
  float mFilteredTargetCents = 0.0f;
  int mFilteredTargetMidiNote = -1;
  double mPreviousRms = 0.0;
  int mAttackSettleFrames = 0;
};
