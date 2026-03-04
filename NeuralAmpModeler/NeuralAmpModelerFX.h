// Included from NeuralAmpModeler.h inside NeuralAmpModeler private section.
void _ProcessFXDelayStage(iplug::sample** ioPointers, const size_t numChannelsInternal, const size_t numChannelsMonoCore,
                          const size_t numFrames, const double sampleRate, const bool fxDelayActive);
void _ResetFXReverbState();
void _ProcessFXReverbStage(iplug::sample** ioPointers, const size_t numChannelsInternal,
                           const size_t numChannelsMonoCore, const size_t numFrames, const double sampleRate);
