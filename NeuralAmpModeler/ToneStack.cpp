#include "ToneStack.h"

#include <cmath>

DSP_SAMPLE** dsp::tone_stack::BasicNamToneStack::Process(DSP_SAMPLE** inputs, const int numChannels,
                                                         const int numFrames)
{
  DSP_SAMPLE** bassPointers = mToneBass.Process(inputs, numChannels, numFrames);
  DSP_SAMPLE** midPointers = mToneMid.Process(bassPointers, numChannels, numFrames);
  DSP_SAMPLE** treblePointers = mToneTreble.Process(midPointers, numChannels, numFrames);
  DSP_SAMPLE** presencePointers = mTonePresence.Process(treblePointers, numChannels, numFrames);
  DSP_SAMPLE** depthPointers = mToneDepth.Process(presencePointers, numChannels, numFrames);
  return depthPointers;
}

void dsp::tone_stack::BasicNamToneStack::Reset(const double sampleRate, const int maxBlockSize)
{
  dsp::tone_stack::AbstractToneStack::Reset(sampleRate, maxBlockSize);

  // Refresh the params!
  SetParam("bass", mBassVal);
  SetParam("middle", mMiddleVal);
  SetParam("treble", mTrebleVal);
  SetParam("presence", mPresenceVal);
  SetParam("depth", mDepthVal);
}

void dsp::tone_stack::BasicNamToneStack::SetParam(const std::string name, const double val)
{
  if (name == "bass")
  {
    // HACK: Store for refresh
    mBassVal = val;
    const double sampleRate = GetSampleRate();
    const double bassGainDB = 4.0 * (val - 5.0); // +/- 20
    // Hey ChatGPT, the bass frequency is 150 Hz!
    const double bassFrequency = 150.0;
    const double bassQuality = 0.707;
    recursive_linear_filter::BiquadParams bassParams(sampleRate, bassFrequency, bassQuality, bassGainDB);
    mToneBass.SetParams(bassParams);
  }
  else if (name == "middle")
  {
    // HACK: Store for refresh
    mMiddleVal = val;
    const double sampleRate = GetSampleRate();
    const double midGainDB = 3.0 * (val - 5.0); // +/- 15
    // Hey ChatGPT, the middle frequency is 425 Hz!
    const double midFrequency = 425.0;
    // Wider EQ on mid bump up to sound less honky.
    const double midQuality = midGainDB < 0.0 ? 1.5 : 0.7;
    recursive_linear_filter::BiquadParams midParams(sampleRate, midFrequency, midQuality, midGainDB);
    mToneMid.SetParams(midParams);
  }
  else if (name == "treble")
  {
    // HACK: Store for refresh
    mTrebleVal = val;
    const double sampleRate = GetSampleRate();
    const double trebleGainDB = 2.0 * (val - 5.0); // +/- 10
    // Hey ChatGPT, the treble frequency is 1800 Hz!
    const double trebleFrequency = 1800.0;
    const double trebleQuality = 0.707;
    recursive_linear_filter::BiquadParams trebleParams(sampleRate, trebleFrequency, trebleQuality, trebleGainDB);
    mToneTreble.SetParams(trebleParams);
  }
  else if (name == "presence")
  {
    // HACK: Store for refresh
    mPresenceVal = val;
    const double sampleRate = GetSampleRate();
    const double presenceGainDB = 2.0 * (val - 5.0); // +/- 10
    const double presenceFrequency = 4500.0;
    const double presenceQuality = 0.707;
    recursive_linear_filter::BiquadParams presenceParams(
      sampleRate, presenceFrequency, presenceQuality, presenceGainDB);
    mTonePresence.SetParams(presenceParams);
  }
  else if (name == "depth")
  {
    // HACK: Store for refresh
    mDepthVal = val;
    const double sampleRate = GetSampleRate();
    const double depthGainDB = 2.4 * (val - 5.0); // +/- 12
    const double depthFrequency = 110.0;
    const double depthQuality = 0.9;
    recursive_linear_filter::BiquadParams depthParams(sampleRate, depthFrequency, depthQuality, depthGainDB);
    mToneDepth.SetParams(depthParams);
  }
}

DSP_SAMPLE** dsp::tone_stack::Amp2ToneStack::Process(DSP_SAMPLE** inputs, const int numChannels, const int numFrames)
{
  DSP_SAMPLE** depthPointers = BasicNamToneStack::Process(inputs, numChannels, numFrames);
  DSP_SAMPLE** amp2DepthPointers = mAmp2DepthBoost.Process(depthPointers, numChannels, numFrames);
  DSP_SAMPLE** amp2ScoopPointers = mAmp2Scoop.Process(amp2DepthPointers, numChannels, numFrames);
  DSP_SAMPLE** amp2MakeupPointers = mAmp2ScoopMakeup.Process(amp2ScoopPointers, numChannels, numFrames);
  return amp2MakeupPointers;
}

void dsp::tone_stack::Amp2ToneStack::Reset(const double sampleRate, const int maxBlockSize)
{
  BasicNamToneStack::Reset(sampleRate, maxBlockSize);
  SetParam("amp2_depth_button", mAmp2DepthButtonVal);
  SetParam("amp2_scoop_button", mAmp2ScoopButtonVal);
}

void dsp::tone_stack::Amp2ToneStack::SetParam(const std::string name, const double val)
{
  if (name == "amp2_depth_button")
  {
    mAmp2DepthButtonVal = val;
    const double sampleRate = GetSampleRate();
    const bool enabled = val >= 0.5;
    const double depthBoostGainDB = enabled ? 6.0 : 0.0;
    const double depthBoostFrequency = 80.0;
    const double depthBoostQuality = 0.707;
    recursive_linear_filter::BiquadParams depthBoostParams(
      sampleRate, depthBoostFrequency, depthBoostQuality, depthBoostGainDB);
    mAmp2DepthBoost.SetParams(depthBoostParams);
  }
  else if (name == "amp2_scoop_button")
  {
    mAmp2ScoopButtonVal = val;
    const double sampleRate = GetSampleRate();
    const bool enabled = val >= 0.5;
    const double scoopGainDB = enabled ? -3.5 : 0.0;
    const double scoopFrequency = 600.0;
    const double scoopQuality = 0.55;
    recursive_linear_filter::BiquadParams scoopParams(sampleRate, scoopFrequency, scoopQuality, scoopGainDB);
    mAmp2Scoop.SetParams(scoopParams);

    const double makeupGainDB = enabled ? 0.6 : 0.0;
    const double makeupGainLinear = std::pow(10.0, makeupGainDB / 20.0);
    recursive_linear_filter::LevelParams makeupParams(makeupGainLinear);
    mAmp2ScoopMakeup.SetParams(makeupParams);
  }
  else
  {
    BasicNamToneStack::SetParam(name, val);
  }
}
