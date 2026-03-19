#pragma once

#include <cstddef>

namespace embedded_cab_ir
{
struct EmbeddedCabIRAsset
{
  const float* samples;
  size_t numSamples;
  double sampleRate;
};

const EmbeddedCabIRAsset* GetCuratedCabIRAsset(int sourceChoice, int captureIndex);
} // namespace embedded_cab_ir
