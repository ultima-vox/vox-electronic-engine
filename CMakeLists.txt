cmake_minimum_required(VERSION 3.22)
project(VstEngine VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 9.0.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Modular architecture (issue #11).
#
#   lower-level modules -> engine/composition -> VST3 shell
#
# Each subsystem is an independent target with explicit dependencies. The
# shell target (VstEngine) only provides JUCE VST3 integration, bridges the
# APVTS/host transport/MIDI/audio and composes the editor.
# ---------------------------------------------------------------------------
set(VST_LIBS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs)

# core: shared low-level identifiers/constants (header-only).
add_library(vst_core INTERFACE)
target_include_directories(vst_core INTERFACE ${VST_LIBS_DIR})

# instrument: versioned external-module-ready ABI, provider registry, stable
# host automation schema, lifecycle and transactional state preparation.
add_library(vst_instrument STATIC
  libs/instrument/InstrumentAbi.h
  libs/instrument/InstrumentContract.h
  libs/instrument/HostParameterSchema.cpp libs/instrument/HostParameterSchema.h
  libs/instrument/InstrumentRegistry.cpp libs/instrument/InstrumentRegistry.h
  libs/instrument/ModuleState.cpp libs/instrument/ModuleState.h)
target_include_directories(vst_instrument PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_instrument PUBLIC vst_pattern)

# native: host side of the instrument module ABI V2 (controller-thread loading,
# descriptor import, provider/instance adapters). PROPOSED, not frozen.
add_library(vst_native STATIC
  libs/instrument/abi/VoxModuleAbiV2.h
  libs/instrument/native/NativeModule.cpp libs/instrument/native/NativeModule.h)
target_include_directories(vst_native PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_native PUBLIC vst_instrument ${CMAKE_DL_LIBS})


# dspcore: Vox DSP Core primitives and the per-instrument patch graph (#75):
# graph-only instruments are data (graph.json) compiled off the audio thread
# into a fixed plan and exposed through the normal InstrumentProvider contract.
add_library(vst_dspcore STATIC
  libs/dspcore/Primitives.h
  libs/dspcore/PatchGraph.cpp libs/dspcore/PatchGraph.h
  libs/dspcore/GraphInstrument.cpp libs/dspcore/GraphInstrument.h)
set_target_properties(vst_dspcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(vst_dspcore PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_dspcore PUBLIC vst_instrument juce::juce_core)

# vins: the one instrument package format (#73): one ZIP archive file per
# instrument next to the VST3, graph (DSP Core) or signed native backend.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64|x64)$")
  set(VOX_NATIVE_PLATFORM "win-x64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  set(VOX_NATIVE_PLATFORM "linux-x64")
else()
  set(VOX_NATIVE_PLATFORM "")
endif()
include(cmake/VinsPublisher.cmake)
include(cmake/Vins.cmake)
add_library(vst_vins STATIC
  libs/instrument/vins/VinsZip.cpp libs/instrument/vins/VinsZip.h
  libs/instrument/vins/VinsTrust.cpp libs/instrument/vins/VinsTrust.h
  libs/instrument/vins/VinsPackage.cpp libs/instrument/vins/VinsPackage.h)
set_target_properties(vst_vins PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(vst_vins PUBLIC ${VST_LIBS_DIR} PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(vst_vins
  PUBLIC vst_native vst_dspcore juce::juce_core juce::juce_cryptography ${CMAKE_DL_LIBS})

# Package tool (host build): writes manifests and deterministic archives.
add_executable(vox_vins_pack tools/VinsPack.cpp)
target_link_libraries(vox_vins_pack PRIVATE vst_vins)
target_compile_definitions(vox_vins_pack PRIVATE JUCE_STANDALONE_APPLICATION=1)

add_library(vst_rack STATIC
  libs/rack/AudioRouting.cpp libs/rack/AudioRouting.h
  libs/rack/RealtimeMutationGate.h
  libs/rack/RackTypes.h
  libs/rack/RackRouter.cpp libs/rack/RackRouter.h
  libs/rack/Rack.cpp libs/rack/Rack.h
  libs/rack/RackState.cpp libs/rack/RackState.h
  libs/rack/RackMigration.cpp libs/rack/RackMigration.h)
target_include_directories(vst_rack PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_rack PUBLIC vst_instrument juce::juce_data_structures)

# sequence: canonical Sequence/Step/timing model + serialization.
add_library(vst_sequence STATIC
  libs/sequence/Sequence.cpp libs/sequence/Sequence.h
  libs/sequence/PatternAdapter.cpp libs/sequence/PatternAdapter.h
  libs/sequence/RealtimeSequenceBridge.h)
target_include_directories(vst_sequence PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_sequence PUBLIC juce::juce_audio_basics)

# pattern: V2 pattern contract host model (validation, encoding, snapshots).
add_library(vst_pattern STATIC
  libs/pattern/Pattern.cpp libs/pattern/Pattern.h)
target_include_directories(vst_pattern PUBLIC ${VST_LIBS_DIR})

# transport: pure PPQ/grid musical-time math (header-only).
add_library(vst_transport INTERFACE)
target_include_directories(vst_transport INTERFACE ${VST_LIBS_DIR})

# midi: source-mode policy, MIDI export, generated-note scheduling.
add_library(vst_midi STATIC
  libs/midi/MidiExport.cpp libs/midi/MidiExport.h
  libs/midi/GeneratedNoteScheduler.cpp libs/midi/GeneratedNoteScheduler.h
  libs/midi/SourceSelector.h)
target_include_directories(vst_midi PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_midi
  PUBLIC juce::juce_audio_basics vst_sequence vst_transport)

# bass: psy-bass DSP voice (oscillator/pitch env/amp env/filter/drive/glide).
add_library(vst_bass STATIC
  libs/bass/PsyBassVoice.cpp libs/bass/PsyBassVoice.h
  libs/bass/BassGenerator.cpp libs/bass/BassGenerator.h)
target_include_directories(vst_bass PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_bass PUBLIC
  juce::juce_audio_basics juce::juce_audio_processors)

# acid: self-contained 303-style DSP, deterministic generator and provider.
add_library(vst_acid STATIC
  libs/acid/AcidEngine.cpp libs/acid/AcidEngine.h
  libs/acid/AcidGenerator.cpp libs/acid/AcidGenerator.h
  libs/acid/AcidProvider.cpp libs/acid/AcidProvider.h)
target_include_directories(vst_acid PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_acid PUBLIC vst_instrument vst_sequence)

# lead: production polyphonic lead DSP, presets and deterministic generator.
add_library(vst_lead STATIC
  libs/lead/LeadEngine.cpp libs/lead/LeadEngine.h
  libs/lead/LeadGenerator.cpp libs/lead/LeadGenerator.h
  libs/lead/LeadProvider.cpp libs/lead/LeadProvider.h)
target_include_directories(vst_lead PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_lead PUBLIC vst_instrument vst_sequence)

# semantic FX: deterministic one-shot transition/effect families.
add_library(vst_semantic_fx STATIC
  libs/semanticfx/SemanticFxEngine.cpp libs/semanticfx/SemanticFxEngine.h
  libs/semanticfx/SemanticFxGenerator.cpp libs/semanticfx/SemanticFxGenerator.h
  libs/semanticfx/SemanticFxProvider.cpp libs/semanticfx/SemanticFxProvider.h)
target_include_directories(vst_semantic_fx PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_semantic_fx PUBLIC vst_instrument vst_sequence)

# atmos: long-form deterministic evolving texture instrument.
add_library(vst_atmos STATIC
  libs/atmos/AtmosEngine.cpp libs/atmos/AtmosEngine.h
  libs/atmos/AtmosGenerator.cpp libs/atmos/AtmosGenerator.h
  libs/atmos/AtmosProvider.cpp libs/atmos/AtmosProvider.h)
target_include_directories(vst_atmos PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_atmos PUBLIC vst_instrument vst_sequence)

# shared modulation: descriptor-driven destination registry and bounded matrix.
add_library(vst_modulation STATIC
  libs/modulation/Modulation.cpp libs/modulation/Modulation.h
  libs/modulation/ModulationState.cpp libs/modulation/ModulationState.h)
target_include_directories(vst_modulation PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_modulation PUBLIC vst_instrument)
target_link_libraries(vst_rack PUBLIC vst_modulation)

# internal creative FX: fixed bounded stereo chain and canonical state.
add_library(vst_effects STATIC
  libs/effects/EffectsChain.cpp libs/effects/EffectsChain.h)
target_include_directories(vst_effects PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_rack PUBLIC vst_effects)

add_library(vst_modules STATIC
  libs/modules/BuiltInProvider.cpp libs/modules/BuiltInProvider.h)
target_include_directories(vst_modules PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_modules PUBLIC
  vst_instrument vst_sequence vst_bass vst_acid vst_lead vst_semantic_fx vst_atmos
  juce::juce_audio_basics)

# preset: user Rack scene files (current format only).
add_library(vst_preset STATIC
  libs/preset/PresetManager.cpp libs/preset/PresetManager.h)
target_include_directories(vst_preset PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_preset
  PUBLIC juce::juce_core juce::juce_data_structures vst_core)

# shared VOX design-system primitives. Product UI may depend on this target;
# the shared target must never depend on product UI or DSP targets.
add_subdirectory(libs/vox-ui)

# ui: reusable JUCE components and tab pages.
add_library(vst_ui STATIC
  libs/ui/common/UiComponents.cpp libs/ui/common/UiComponents.h
  libs/ui/ZoneRangeEditor.cpp libs/ui/ZoneRangeEditor.h
  libs/ui/MainNavigation.cpp libs/ui/MainNavigation.h
  libs/ui/GlobalHeader.cpp libs/ui/GlobalHeader.h
  libs/ui/StepSequencer.cpp libs/ui/StepSequencer.h
  libs/ui/PatternGrid.cpp libs/ui/PatternGrid.h)
target_include_directories(vst_ui PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_ui PUBLIC
  juce::juce_gui_basics juce::juce_audio_utils juce::juce_audio_processors
  vox_ui vst_sequence vst_preset vst_pattern)

# ---------------------------------------------------------------------------
# VST3 shell: composition + host integration only.
# ---------------------------------------------------------------------------
juce_add_plugin(VstEngine
  COMPANY_NAME "Ultima Vox"
  BUNDLE_ID "com.ultimavox.vstengine"
  PLUGIN_MANUFACTURER_CODE UlVx
  PLUGIN_CODE DkP1
  FORMATS VST3
  PRODUCT_NAME "Vox Electronic Engine"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT TRUE
  IS_MIDI_EFFECT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES "Instrument" "Synth")

juce_generate_juce_header(VstEngine)

target_sources(VstEngine PRIVATE
  apps/vst3/PluginProcessor.cpp apps/vst3/PluginProcessor.h
  apps/vst3/PluginEditor.cpp apps/vst3/PluginEditor.h)

target_compile_definitions(VstEngine PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(VstEngine PRIVATE
  juce::juce_audio_utils juce::juce_dsp
  vst_core vst_instrument vst_native vst_vins vst_dspcore vst_rack vst_modules vst_sequence
  vst_transport vst_midi vst_preset vst_ui vst_effects)

if(MSVC)
  # JUCE's shared-code target emits one PDB. /FS serializes concurrent compiler
  # writes when Visual Studio builds PluginProcessor and PluginEditor together.
  target_compile_options(VstEngine PRIVATE /W4 /permissive- /FS)
else()
  target_compile_options(VstEngine PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Tests: one executable per module library, mapped 1:1 to module targets, plus
# integration tests for cross-module state compatibility.
# ---------------------------------------------------------------------------
enable_testing()

add_executable(sequence_tests tests/sequence/SequenceTests.cpp)
target_link_libraries(sequence_tests PRIVATE vst_sequence)
add_test(NAME sequence_tests COMMAND sequence_tests)

add_executable(instrument_contract_tests
  tests/instrument/InstrumentContractTests.cpp)
target_link_libraries(instrument_contract_tests PRIVATE vst_instrument)
add_test(NAME instrument_contract_tests COMMAND instrument_contract_tests)

add_executable(instrument_compliance_tests
  tests/instrument/BuiltInComplianceTests.cpp
  tests/instrument/InstrumentComplianceHarness.cpp
  tests/instrument/InstrumentComplianceHarness.h)
target_include_directories(instrument_compliance_tests PRIVATE tests ${VST_LIBS_DIR})
target_link_libraries(instrument_compliance_tests PRIVATE vst_modules)
add_test(NAME instrument_compliance_tests COMMAND instrument_compliance_tests)

add_executable(rack_router_tests tests/rack/RackRouterTests.cpp)
target_link_libraries(rack_router_tests PRIVATE vst_rack)
add_test(NAME rack_router_tests COMMAND rack_router_tests)

add_executable(rack_integration_tests tests/rack/RackIntegrationTests.cpp)
target_link_libraries(rack_integration_tests PRIVATE vst_rack vst_modules)
add_test(NAME rack_integration_tests COMMAND rack_integration_tests)

add_executable(rack_lifecycle_tests
  tests/rack/RackLifecycleTests.cpp
  tests/support/ReferenceInstrument.cpp tests/support/ReferenceInstrument.h)
target_include_directories(rack_lifecycle_tests PRIVATE tests)
target_link_libraries(rack_lifecycle_tests PRIVATE vst_rack)
add_test(NAME rack_lifecycle_tests COMMAND rack_lifecycle_tests)

add_executable(rack_ducking_tests tests/rack/RackDuckingTests.cpp)
target_link_libraries(rack_ducking_tests PRIVATE vst_rack)
add_test(NAME rack_ducking_tests COMMAND rack_ducking_tests)

add_executable(rack_live_controls_tests tests/rack/RackLiveControlsTests.cpp)
target_link_libraries(rack_live_controls_tests PRIVATE vst_rack)
add_test(NAME rack_live_controls_tests COMMAND rack_live_controls_tests)

add_executable(transport_sync_tests tests/transport/TransportSyncTests.cpp)
target_link_libraries(transport_sync_tests PRIVATE vst_transport)
add_test(NAME transport_sync_tests COMMAND transport_sync_tests)

add_executable(midi_source_tests tests/midi/MidiSourceTests.cpp)
target_link_libraries(midi_source_tests PRIVATE vst_midi)
add_test(NAME midi_source_tests COMMAND midi_source_tests)

add_executable(midi_export_tests tests/midi/MidiExportTests.cpp)
target_link_libraries(midi_export_tests PRIVATE vst_midi)
add_test(NAME midi_export_tests COMMAND midi_export_tests)

add_executable(generated_note_scheduler_tests
  tests/midi/GeneratedNoteSchedulerTests.cpp)
target_link_libraries(generated_note_scheduler_tests PRIVATE vst_midi)
add_test(NAME generated_note_scheduler_tests
         COMMAND generated_note_scheduler_tests)

find_package(Threads REQUIRED)
add_executable(pattern_tests tests/pattern/PatternTests.cpp)
target_link_libraries(pattern_tests PRIVATE vst_pattern Threads::Threads)
add_test(NAME pattern_tests COMMAND pattern_tests)

add_executable(psy_bass_tests tests/bass/PsyBassTests.cpp)
target_link_libraries(psy_bass_tests PRIVATE vst_bass)
add_test(NAME psy_bass_tests COMMAND psy_bass_tests)

add_executable(acid_tests
  tests/acid/AcidTests.cpp
  tests/instrument/InstrumentComplianceHarness.cpp
  tests/instrument/InstrumentComplianceHarness.h
  tests/support/ReferenceInstrument.cpp
  tests/support/ReferenceInstrument.h)
target_include_directories(acid_tests PRIVATE tests ${VST_LIBS_DIR})
target_link_libraries(acid_tests PRIVATE vst_acid vst_instrument)
add_test(NAME acid_tests COMMAND acid_tests)

add_executable(lead_tests
  tests/lead/LeadTests.cpp
  tests/instrument/InstrumentComplianceHarness.cpp
  tests/instrument/InstrumentComplianceHarness.h)
target_include_directories(lead_tests PRIVATE tests ${VST_LIBS_DIR})
target_link_libraries(lead_tests PRIVATE vst_lead vst_instrument)
add_test(NAME lead_tests COMMAND lead_tests)

add_executable(semantic_fx_tests
  tests/semanticfx/SemanticFxTests.cpp
  tests/instrument/InstrumentComplianceHarness.cpp
  tests/instrument/InstrumentComplianceHarness.h)
target_include_directories(semantic_fx_tests PRIVATE tests ${VST_LIBS_DIR})
target_link_libraries(semantic_fx_tests PRIVATE vst_semantic_fx vst_instrument)
add_test(NAME semantic_fx_tests COMMAND semantic_fx_tests)

add_executable(atmos_tests
  tests/atmos/AtmosTests.cpp
  tests/instrument/InstrumentComplianceHarness.cpp
  tests/instrument/InstrumentComplianceHarness.h)
target_include_directories(atmos_tests PRIVATE tests ${VST_LIBS_DIR})
target_link_libraries(atmos_tests PRIVATE vst_atmos vst_instrument)
add_test(NAME atmos_tests COMMAND atmos_tests)

add_executable(modulation_tests tests/modulation/ModulationTests.cpp)
target_link_libraries(modulation_tests PRIVATE vst_modulation)
add_test(NAME modulation_tests COMMAND modulation_tests)

add_executable(effects_tail_policy_tests tests/effects/EffectsTailPolicyTests.cpp)
target_link_libraries(effects_tail_policy_tests PRIVATE vst_effects)
add_test(NAME effects_tail_policy_tests COMMAND effects_tail_policy_tests)

add_executable(effects_tests tests/effects/EffectsChainTests.cpp)
target_link_libraries(effects_tests PRIVATE vst_effects)
add_test(NAME effects_tests COMMAND effects_tests)

add_executable(preset_tests tests/preset/PresetTests.cpp)
target_link_libraries(preset_tests PRIVATE vst_preset)
add_test(NAME preset_tests COMMAND preset_tests)

add_executable(state_round_trip_tests
  tests/integration/StateRoundTripTests.cpp)
target_link_libraries(state_round_trip_tests PRIVATE vst_preset vst_sequence)
add_test(NAME state_round_trip_tests COMMAND state_round_trip_tests)

add_executable(direct_parameter_contract_tests tests/rack/DirectParameterContractTests.cpp)
target_link_libraries(direct_parameter_contract_tests PRIVATE vst_rack vst_modulation)
add_test(NAME direct_parameter_contract_tests COMMAND direct_parameter_contract_tests)

add_executable(realtime_mutation_gate_tests tests/rack/RealtimeMutationGateTests.cpp)
target_include_directories(realtime_mutation_gate_tests PRIVATE ${VST_LIBS_DIR})
add_test(NAME realtime_mutation_gate_tests COMMAND realtime_mutation_gate_tests)

foreach(direct_test IN ITEMS DirectParameterBankTests DirectProcessorStateTests RackMidiExportTests)
  if(direct_test STREQUAL "DirectParameterBankTests")
    set(direct_target direct_parameter_bank_tests)
  elseif(direct_test STREQUAL "DirectProcessorStateTests")
    set(direct_target direct_processor_state_tests)
  else()
    set(direct_target rack_midi_export_tests)
  endif()
  add_executable(${direct_target} tests/integration/${direct_test}.cpp)
  target_include_directories(${direct_target} PRIVATE apps/vst3 ${VST_LIBS_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode ${juce_SOURCE_DIR}/modules)
  target_link_libraries(${direct_target} PRIVATE VstEngine)
  add_test(NAME ${direct_target} COMMAND ${direct_target})
endforeach()

add_executable(rack_realtime_boundary_tests tests/integration/RackRealtimeBoundaryTests.cpp)
target_include_directories(rack_realtime_boundary_tests PRIVATE apps/vst3 ${VST_LIBS_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode ${juce_SOURCE_DIR}/modules)
target_link_libraries(rack_realtime_boundary_tests PRIVATE VstEngine)
if(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_definitions(rack_realtime_boundary_tests PRIVATE VOX_TEST_WRAP_MALLOC=1)
  target_link_options(rack_realtime_boundary_tests PRIVATE -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc)
endif()
add_test(NAME rack_realtime_boundary_tests COMMAND rack_realtime_boundary_tests)

add_executable(audio_routing_tests tests/rack/AudioRoutingTests.cpp)
target_include_directories(audio_routing_tests PRIVATE ${VST_LIBS_DIR})
target_link_libraries(audio_routing_tests PRIVATE vst_effects)
# AudioRouting itself is JUCE-independent; test the same production source.
target_sources(audio_routing_tests PRIVATE libs/rack/AudioRouting.cpp)
add_test(NAME audio_routing_tests COMMAND audio_routing_tests)

add_executable(rack_shell_integration_tests tests/integration/RackShellIntegrationTests.cpp)
target_include_directories(rack_shell_integration_tests PRIVATE apps/vst3 ${VST_LIBS_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode ${juce_SOURCE_DIR}/modules)
target_link_libraries(rack_shell_integration_tests PRIVATE VstEngine)
add_test(NAME rack_shell_integration_tests COMMAND rack_shell_integration_tests)

add_executable(editor_smoke_tests tests/ui/EditorSmokeTests.cpp)
target_include_directories(editor_smoke_tests PRIVATE
  apps/vst3
  ${VST_LIBS_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode
  ${juce_SOURCE_DIR}/modules)
target_link_libraries(editor_smoke_tests PRIVATE VstEngine)
add_test(NAME editor_smoke_tests COMMAND editor_smoke_tests)

add_executable(part_audio_isolation_tests
  tests/integration/PartAudioIsolationTests.cpp)
target_include_directories(part_audio_isolation_tests PRIVATE
  apps/vst3
  ${VST_LIBS_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode
  ${juce_SOURCE_DIR}/modules)
target_link_libraries(part_audio_isolation_tests PRIVATE VstEngine)
add_test(NAME part_audio_isolation_tests COMMAND part_audio_isolation_tests)

add_executable(performance_stress_tests
  tests/performance/PerformanceStressTests.cpp)
target_include_directories(performance_stress_tests PRIVATE
  apps/vst3
  ${VST_LIBS_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode
  ${juce_SOURCE_DIR}/modules)
target_link_libraries(performance_stress_tests PRIVATE VstEngine)
add_test(NAME performance_stress_tests COMMAND performance_stress_tests)

# Host-parameter pool capacity microbenchmark (not a CTest gate) for the final
# 128-per-slot layout; the binary verifies the endpoint layout before measuring.
add_executable(pool_capacity_bench tests/performance/PoolCapacityBench.cpp)
target_include_directories(pool_capacity_bench PRIVATE
  apps/vst3
  ${VST_LIBS_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode
  ${juce_SOURCE_DIR}/modules)
target_link_libraries(pool_capacity_bench PRIVATE VstEngine)

# Instrument module ABI V2, slice 1 (PROPOSED, not frozen): the C11 test module
# is a real shared library; the C++20 harness loads it by absolute path and owns
# all loading/validation. No production loader, registry or editor hosting.
add_library(vox_abi_v2_test_module SHARED tests/abi/TestModuleV2.c)
set_target_properties(vox_abi_v2_test_module PROPERTIES
  PREFIX ""
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  C_VISIBILITY_PRESET hidden)
target_include_directories(vox_abi_v2_test_module PRIVATE ${VST_LIBS_DIR})
if(UNIX)
  target_link_libraries(vox_abi_v2_test_module PRIVATE m)
endif()

add_executable(module_abi_v2_tests tests/abi/ModuleAbiV2Tests.cpp tests/abi/AbiLayoutC.c)
set_target_properties(module_abi_v2_tests PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_include_directories(module_abi_v2_tests PRIVATE ${VST_LIBS_DIR})
add_dependencies(module_abi_v2_tests vox_abi_v2_test_module)
if(UNIX AND NOT APPLE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  # glibc interposer: counts host and module allocations during the RT probe.
  target_sources(module_abi_v2_tests PRIVATE tests/abi/AllocationProbeLinux.c)
  target_compile_definitions(module_abi_v2_tests PRIVATE VOX_TEST_INTERPOSE_MALLOC=1)
  target_link_options(module_abi_v2_tests PRIVATE -rdynamic)
endif()
target_link_libraries(module_abi_v2_tests PRIVATE ${CMAKE_DL_LIBS})
add_test(NAME module_abi_v2_tests
  COMMAND module_abi_v2_tests $<TARGET_FILE:vox_abi_v2_test_module>)

# M0: the real Rack processor plays the V2 test module through a slot.
add_executable(native_rack_tests tests/integration/NativeRackTests.cpp)
target_include_directories(native_rack_tests PRIVATE apps/vst3 ${VST_LIBS_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode ${juce_SOURCE_DIR}/modules)
target_link_libraries(native_rack_tests PRIVATE VstEngine)
add_dependencies(native_rack_tests vox_abi_v2_test_module)
if(UNIX AND NOT APPLE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_sources(native_rack_tests PRIVATE tests/abi/AllocationProbeLinux.c)
  target_compile_definitions(native_rack_tests PRIVATE VOX_TEST_INTERPOSE_MALLOC=1)
  target_link_options(native_rack_tests PRIVATE -rdynamic)
endif()
add_test(NAME native_rack_tests
  COMMAND native_rack_tests $<TARGET_FILE:vox_abi_v2_test_module>)

# Owner Cubase acceptance: a fresh Rack is empty; new projects restore.
add_executable(fresh_rack_tests tests/integration/FreshRackTests.cpp)
target_include_directories(fresh_rack_tests PRIVATE apps/vst3 ${VST_LIBS_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode ${juce_SOURCE_DIR}/modules)
target_link_libraries(fresh_rack_tests PRIVATE VstEngine)
add_test(NAME fresh_rack_tests COMMAND fresh_rack_tests)

# .vins packages built in this tree. instruments/ mirrors the installed
# layout (<product>/instruments); test-vins/ holds test fixtures.
set(VOX_INSTRUMENTS_DIR ${CMAKE_BINARY_DIR}/instruments)
set(VOX_TEST_VINS_DIR ${CMAKE_BINARY_DIR}/test-vins)

# Vox Demo Synth: a small, independent, playable V2 native instrument.
add_library(VoxDemoSynth SHARED libs/demo/VoxDemoSynth.c)
set_target_properties(VoxDemoSynth PROPERTIES
  PREFIX ""
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  C_VISIBILITY_PRESET hidden)
target_include_directories(VoxDemoSynth PRIVATE ${VST_LIBS_DIR})
if(UNIX)
  target_link_libraries(VoxDemoSynth PRIVATE m)
endif()
if(MSVC)
  # Self-contained DLL: no VC++ redistributable needed on the DAW machine.
  set_property(TARGET VoxDemoSynth PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# The graph-only Demo Synth: data for the DSP Core, no executable code.
vox_add_vins(vins_demo_synth_graph KIND graph
  OUTPUT ${VOX_INSTRUMENTS_DIR}/VoxDemoSynthGraph.vins
  ID com.ultimavox.demo-synth-graph VERSION 1.0.0
  NAME "Vox Demo Synth (Graph)" VENDOR "Ultima Vox"
  INSTRUMENTS com.ultimavox.demo-synth-graph
  GRAPH graph/instrument.json ${CMAKE_CURRENT_SOURCE_DIR}/instruments/demo-synth-graph/graph.json)

if(VOX_NATIVE_PLATFORM)
  vox_add_vins(vins_demo_synth KIND native
    OUTPUT ${VOX_INSTRUMENTS_DIR}/VoxDemoSynth.vins
    ID com.ultimavox.demo-synth VERSION 1.0.0
    NAME "Vox Demo Synth" VENDOR "Ultima Vox"
    INSTRUMENTS com.ultimavox.demo-synth MODULE_TARGET VoxDemoSynth)
  vox_add_vins(vins_test_tone KIND native
    OUTPUT ${VOX_TEST_VINS_DIR}/AbiTestTone.vins
    ID test.abi.v2.tone VERSION 1.0.0 NAME "ABI Test Tone" VENDOR "Vox tests"
    INSTRUMENTS test.abi.v2.tone MODULE_TARGET vox_abi_v2_test_module)
  vox_add_vins(vins_test_tone_untrusted KIND native
    OUTPUT ${VOX_TEST_VINS_DIR}/AbiTestTone-untrusted.vins
    ID test.abi.v2.tone VERSION 1.0.0 NAME "ABI Test Tone" VENDOR "Vox tests"
    INSTRUMENTS test.abi.v2.tone MODULE_TARGET vox_abi_v2_test_module
    KEY ${VOX_UNTRUSTED_TEST_KEY} MODULUS ${VOX_UNTRUSTED_MODULUS_FILE})

  add_executable(vins_package_tests tests/integration/VinsPackageTests.cpp)
  target_include_directories(vins_package_tests PRIVATE apps/vst3 ${VST_LIBS_DIR}
      ${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode ${juce_SOURCE_DIR}/modules)
  target_link_libraries(vins_package_tests PRIVATE VstEngine vst_vins)
  add_dependencies(vins_package_tests vins_test_tone vins_test_tone_untrusted
    vins_demo_synth vins_demo_synth_graph)
  add_test(NAME vins_package_tests
    COMMAND vins_package_tests ${VOX_TEST_VINS_DIR} ${VOX_INSTRUMENTS_DIR}
      ${CMAKE_BINARY_DIR}/vins-scratch)

  add_executable(demo_synth_tests tests/integration/DemoSynthTests.cpp)
  target_include_directories(demo_synth_tests PRIVATE apps/vst3 ${VST_LIBS_DIR}
      ${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode ${juce_SOURCE_DIR}/modules)
  target_link_libraries(demo_synth_tests PRIVATE VstEngine vst_vins)
  add_dependencies(demo_synth_tests vins_demo_synth)
  if(UNIX AND NOT APPLE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_sources(demo_synth_tests PRIVATE tests/abi/AllocationProbeLinux.c)
    target_compile_definitions(demo_synth_tests PRIVATE VOX_TEST_INTERPOSE_MALLOC=1)
    target_link_options(demo_synth_tests PRIVATE -rdynamic)
  endif()
  add_test(NAME demo_synth_tests
    COMMAND demo_synth_tests ${VOX_INSTRUMENTS_DIR} ${CMAKE_BINARY_DIR}/demo-scratch)

  # #75 slice 1: the graph-only Demo .vins vs the native Demo .vins, bit for bit.
  add_executable(graph_demo_parity_tests tests/integration/GraphDemoParityTests.cpp)
  target_include_directories(graph_demo_parity_tests PRIVATE apps/vst3 ${VST_LIBS_DIR}
      ${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode ${juce_SOURCE_DIR}/modules)
  target_link_libraries(graph_demo_parity_tests PRIVATE VstEngine vst_vins)
  add_dependencies(graph_demo_parity_tests vins_demo_synth vins_demo_synth_graph)
  add_test(NAME graph_demo_parity_tests
    COMMAND graph_demo_parity_tests ${VOX_INSTRUMENTS_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/instruments/demo-synth-graph/graph.json
      ${CMAKE_BINARY_DIR}/graph-scratch)
endif()

# Full-on Drums (#71): independent eight-pad native drum kit. Not linked into
# the Rack; shipped as VoxFullOnDrums.vins.
add_library(VoxFullOnDrums SHARED libs/drums/VoxFullOnDrums.c)
set_target_properties(VoxFullOnDrums PROPERTIES
  PREFIX ""
  C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  C_VISIBILITY_PRESET hidden)
target_include_directories(VoxFullOnDrums PRIVATE ${VST_LIBS_DIR})
if(UNIX)
  target_link_libraries(VoxFullOnDrums PRIVATE m)
endif()
if(MSVC)
  set_property(TARGET VoxFullOnDrums PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(VOX_NATIVE_PLATFORM)
  vox_add_vins(vins_fullon_drums KIND native
    OUTPUT ${VOX_INSTRUMENTS_DIR}/VoxFullOnDrums.vins
    ID com.ultimavox.fullon-drums VERSION 0.1.0
    NAME "Full-on Drums" VENDOR "Ultima Vox"
    INSTRUMENTS com.ultimavox.fullon-drums MODULE_TARGET VoxFullOnDrums)

  add_executable(fullon_drums_tests tests/integration/FullOnDrumsTests.cpp)
  target_include_directories(fullon_drums_tests PRIVATE apps/vst3 ${VST_LIBS_DIR}
      ${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode ${juce_SOURCE_DIR}/modules)
  target_link_libraries(fullon_drums_tests PRIVATE VstEngine vst_vins)
  add_dependencies(fullon_drums_tests vins_fullon_drums vins_demo_synth vins_demo_synth_graph)
  if(UNIX AND NOT APPLE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_sources(fullon_drums_tests PRIVATE tests/abi/AllocationProbeLinux.c)
    target_compile_definitions(fullon_drums_tests PRIVATE VOX_TEST_INTERPOSE_MALLOC=1)
    target_link_options(fullon_drums_tests PRIVATE -rdynamic)
  endif()
  add_test(NAME fullon_drums_tests
    COMMAND fullon_drums_tests ${VOX_INSTRUMENTS_DIR} ${CMAKE_BINARY_DIR}/drums-scratch
      ${CMAKE_CURRENT_SOURCE_DIR}/docs/testing/fullon-drums)
endif()

add_executable(reliability_stress_tests
  tests/reliability/ReliabilityStressTests.cpp)
target_include_directories(reliability_stress_tests PRIVATE
  apps/vst3
  ${VST_LIBS_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode
  ${juce_SOURCE_DIR}/modules)
target_link_libraries(reliability_stress_tests PRIVATE VstEngine)
add_test(NAME reliability_stress_tests COMMAND reliability_stress_tests)

add_executable(bass_render_diagnostics
  tests/diagnostics/BassRenderDiagnostics.cpp)
target_include_directories(bass_render_diagnostics PRIVATE
  apps/vst3
  ${VST_LIBS_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode
  ${juce_SOURCE_DIR}/modules)
target_link_libraries(bass_render_diagnostics PRIVATE VstEngine)
if(MSVC)
  set_target_properties(bass_render_diagnostics PROPERTIES LINK_FLAGS "/STACK:8388608")
endif()
add_test(NAME bass_render_diagnostics COMMAND bass_render_diagnostics)

# 7C installable product. Keep VST3 bundle name stable for upgrades; CPack
# provides NSIS install/uninstall and ZIP artifact for CI/review environments.
install(DIRECTORY
  "${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/$<CONFIG>/VST3/Vox Electronic Engine.vst3"
  DESTINATION "."
  COMPONENT VoxPlugin)
set(CPACK_PACKAGE_NAME "Vox Electronic Engine")
set(CPACK_PACKAGE_VENDOR "Ultima Vox")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
# Package only product component. JUCE also publishes SDK install rules; letting
# CPack use every project rule would put JUCE headers, libraries and juceaide in
# release artifact.
set(CPACK_INSTALL_CMAKE_PROJECTS
  "${CMAKE_BINARY_DIR};${PROJECT_NAME};VoxPlugin;/")
set(CPACK_PACKAGE_INSTALL_DIRECTORY ".")
option(VOX_ENABLE_NSIS "Build Windows NSIS installer (requires makensis)" OFF)
set(CPACK_GENERATOR "ZIP")
if(VOX_ENABLE_NSIS)
  set(CPACK_GENERATOR "NSIS;ZIP")
endif()
set(CPACK_NSIS_DISPLAY_NAME "Vox Electronic Engine")
set(CPACK_NSIS_PACKAGE_NAME "Vox Electronic Engine")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH OFF)
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64/Common Files/VST3")
include(CPack)
