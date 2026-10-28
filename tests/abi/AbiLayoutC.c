/* Compiled as C11: reports the layout the C compiler sees so the C++20
 * harness can compare it field by field (the header's static asserts already
 * pin the values in both languages). */
#include "instrument/abi/VoxModuleAbiV2.h"

#define ENTRY(type, field) (uint32_t) offsetof(type, field)

uint32_t voxAbiLayoutC(uint32_t *out, uint32_t capacity) {
  const uint32_t values[] = {
      (uint32_t)sizeof(VoxHostServicesV2),
      (uint32_t)sizeof(VoxParameterDescriptorV2),
      ENTRY(VoxParameterDescriptorV2, choices),
      ENTRY(VoxParameterDescriptorV2, type),
      ENTRY(VoxParameterDescriptorV2, minimum),
      ENTRY(VoxParameterDescriptorV2, poolIndex),
      ENTRY(VoxParameterDescriptorV2, flags),
      (uint32_t)sizeof(VoxInstrumentDescriptorV2),
      ENTRY(VoxInstrumentDescriptorV2, instrumentVersion),
      ENTRY(VoxInstrumentDescriptorV2, parameters),
      ENTRY(VoxInstrumentDescriptorV2, parameterStride),
      ENTRY(VoxInstrumentDescriptorV2, maxStateBytes),
      ENTRY(VoxInstrumentDescriptorV2, outputChannels),
      (uint32_t)sizeof(VoxCreateParamsV2),
      (uint32_t)sizeof(VoxPrepareSpecV2),
      ENTRY(VoxPrepareSpecV2, sampleRate),
      ENTRY(VoxPrepareSpecV2, maxMidiEventsPerBlock),
      (uint32_t)sizeof(VoxParamChangeV2),
      ENTRY(VoxParamChangeV2, plain),
      (uint32_t)sizeof(VoxMidiEventV2),
      ENTRY(VoxMidiEventV2, bytes),
      (uint32_t)sizeof(VoxProcessContextV2),
      ENTRY(VoxProcessContextV2, outputs),
      ENTRY(VoxProcessContextV2, paramChanges),
      ENTRY(VoxProcessContextV2, paramChangeStride),
      ENTRY(VoxProcessContextV2, midiEvents),
      ENTRY(VoxProcessContextV2, midiEventStride),
      ENTRY(VoxProcessContextV2, releaseFlags),
      ENTRY(VoxProcessContextV2, samplePosition),
      ENTRY(VoxProcessContextV2, transport),
      (uint32_t)sizeof(VoxTransportV2),
      ENTRY(VoxTransportV2, flags),
      ENTRY(VoxTransportV2, tempoBpm),
      ENTRY(VoxTransportV2, ppqPosition),
      ENTRY(VoxTransportV2, loopEndPpq),
      ENTRY(VoxTransportV2, timeInSamples),
      (uint32_t)sizeof(VoxModuleV2),
      ENTRY(VoxModuleV2, getInstrument),
      ENTRY(VoxModuleV2, process),
      ENTRY(VoxModuleV2, loadState),
      ENTRY(VoxModuleV2, content),
      ENTRY(VoxModuleV2, editor),
      ENTRY(VoxModuleV2, shutdown),
  };
  const uint32_t count = (uint32_t)(sizeof(values) / sizeof(values[0]));
  for (uint32_t i = 0; i < count && i < capacity; ++i)
    out[i] = values[i];
  return count;
}
