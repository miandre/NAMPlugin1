#include "NeuralAmpModeler.h"

using iplug::sample;

namespace
{
constexpr double kPi = 3.14159265358979323846;
inline double DBToAmp(const double db)
{
  return std::pow(10.0, db / 20.0);
}
} // namespace

void NeuralAmpModeler::_ProcessPostCabEQStage(sample** ioPointers, const size_t numChannelsInternal, const size_t numFrames,
                                         const double sampleRate)
{
  if (ioPointers == nullptr || sampleRate <= 0.0)
    return;

  constexpr std::array<int, 10> kFXEQParamIdx = {
    kFXEQBand31Hz, kFXEQBand62Hz, kFXEQBand125Hz, kFXEQBand250Hz, kFXEQBand500Hz,
    kFXEQBand1kHz, kFXEQBand2kHz, kFXEQBand4kHz, kFXEQBand8kHz, kFXEQBand16kHz
  };
  constexpr std::array<double, 10> kFXEQCenterHz = {31.0, 62.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0};
  constexpr double kFXEQQ = 1.41421356237;
  constexpr double kFXEQSmoothingMs = 30.0;
  const double smoothingAlpha =
    1.0 - std::exp(-(static_cast<double>(numFrames) / (sampleRate * kFXEQSmoothingMs * 0.001)));
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
      double x = ioPointers[c][s];
      for (size_t band = 0; band < kFXEQCenterHz.size(); ++band)
      {
        const double y = mFXEQB0[band] * x + z1[band];
        z1[band] = mFXEQB1[band] * x - mFXEQA1[band] * y + z2[band];
        z2[band] = mFXEQB2[band] * x - mFXEQA2[band] * y;
        x = y;
      }
      ioPointers[c][s] = static_cast<sample>(x);
    }
  }

  if (std::abs(mFXEQSmoothedOutputGain - 1.0) > 1e-6)
  {
    for (size_t c = 0; c < numChannelsInternal; ++c)
      for (size_t s = 0; s < numFrames; ++s)
        ioPointers[c][s] = static_cast<sample>(ioPointers[c][s] * mFXEQSmoothedOutputGain);
  }
}
