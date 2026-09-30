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

add_library(vst_rack STATIC
  libs/rack/RackTypes.h
  libs/rack/RackRouter.cpp libs/rack/RackRouter.h
  libs/rack/Rack.cpp libs/rack/Rack.h
  libs/rack/RackState.cpp libs/rack/RackState.h)
target_include_directories(vst_rack PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_rack PUBLIC vst_instrument juce::juce_data_structures)

# sequence: canonical Sequence/Step/timing model + serialization.
add_library(vst_sequence STATIC
  libs/sequence/Sequence.cpp libs/sequence/Sequence.h
  libs/sequence/PatternAdapter.cpp libs/sequence/PatternAdapter.h
  libs/sequence/RealtimeSequenceBridge.h)
target_include_directories(vst_sequence PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_sequence PUBLIC juce::juce_audio_basics)

# parts: authoritative multitimbral model, routing, state and mixer policy.
add_library(vst_parts STATIC
  libs/parts/Part.h
  libs/parts/PartRegistry.cpp libs/parts/PartRegistry.h
  libs/parts/PartRouter.cpp libs/parts/PartRouter.h
  libs/parts/PartMidiDelay.cpp libs/parts/PartMidiDelay.h
  libs/parts/PartMixer.cpp libs/parts/PartMixer.h
  libs/parts/PartState.cpp libs/parts/PartState.h)
target_include_directories(vst_parts PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_parts PUBLIC
  juce::juce_audio_basics juce::juce_data_structures vst_sequence)

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

add_library(vst_modules STATIC
  libs/modules/BuiltInProvider.cpp libs/modules/BuiltInProvider.h)
target_include_directories(vst_modules PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_modules PUBLIC
  vst_instrument vst_sequence vst_bass vst_acid vst_lead vst_semantic_fx vst_atmos
  juce::juce_audio_basics)

# preset: preset format, filesystem, factory/user distinction, migration.
add_library(vst_preset STATIC
  libs/preset/PresetManager.cpp libs/preset/PresetManager.h)
target_include_directories(vst_preset PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_preset
  PUBLIC juce::juce_core juce::juce_data_structures vst_sequence vst_core)

# generator: deterministic seed-based musical pattern generation.
add_library(vst_generator STATIC
  libs/generator/PatternGenerator.cpp libs/generator/PatternGenerator.h)
target_include_directories(vst_generator PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_generator PUBLIC vst_parts)

# shared VOX design-system primitives. Product UI may depend on this target;
# the shared target must never depend on product UI or DSP targets.
add_subdirectory(libs/vox-ui)

# ui: reusable JUCE components and tab pages.
add_library(vst_ui STATIC
  libs/ui/common/UiComponents.cpp libs/ui/common/UiComponents.h
  libs/ui/ZoneRangeEditor.cpp libs/ui/ZoneRangeEditor.h
  libs/ui/MainNavigation.cpp libs/ui/MainNavigation.h
  libs/ui/GlobalHeader.cpp libs/ui/GlobalHeader.h
  libs/ui/BassPanel.cpp libs/ui/BassPanel.h
  libs/ui/StepSequencer.cpp libs/ui/StepSequencer.h
  libs/ui/PresetBrowser.cpp libs/ui/PresetBrowser.h)
target_include_directories(vst_ui PUBLIC ${VST_LIBS_DIR})
target_link_libraries(vst_ui PUBLIC
  juce::juce_gui_basics juce::juce_audio_utils juce::juce_audio_processors
  vox_ui vst_sequence vst_parts vst_preset)

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
  vst_core vst_instrument vst_rack vst_modules vst_sequence vst_parts
  vst_transport vst_midi vst_preset vst_generator vst_ui vst_effects)

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

add_executable(part_router_tests tests/parts/PartRouterTests.cpp)
target_link_libraries(part_router_tests PRIVATE vst_parts)
add_test(NAME part_router_tests COMMAND part_router_tests)

add_executable(part_state_tests tests/parts/PartStateTests.cpp)
target_link_libraries(part_state_tests PRIVATE vst_parts)
add_test(NAME part_state_tests COMMAND part_state_tests)

add_executable(part_sequence_tests tests/parts/PartSequenceTests.cpp)
target_link_libraries(part_sequence_tests PRIVATE vst_parts)
add_test(NAME part_sequence_tests COMMAND part_sequence_tests)

add_executable(part_mixer_tests tests/parts/PartMixerTests.cpp)
target_link_libraries(part_mixer_tests PRIVATE vst_parts)
add_test(NAME part_mixer_tests COMMAND part_mixer_tests)

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

add_executable(effects_tests tests/effects/EffectsChainTests.cpp)
target_link_libraries(effects_tests PRIVATE vst_effects)
add_test(NAME effects_tests COMMAND effects_tests)

add_executable(preset_tests tests/preset/PresetTests.cpp)
target_link_libraries(preset_tests PRIVATE vst_preset vst_parts)
add_test(NAME preset_tests COMMAND preset_tests)

add_executable(generator_tests tests/generator/PatternGeneratorTests.cpp)
target_link_libraries(generator_tests PRIVATE vst_generator)
add_test(NAME generator_tests COMMAND generator_tests)

add_executable(state_round_trip_tests
  tests/integration/StateRoundTripTests.cpp)
target_link_libraries(state_round_trip_tests PRIVATE vst_preset vst_sequence vst_parts)
add_test(NAME state_round_trip_tests COMMAND state_round_trip_tests)

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

add_executable(reliability_stress_tests
  tests/reliability/ReliabilityStressTests.cpp)
target_include_directories(reliability_stress_tests PRIVATE
  apps/vst3
  ${VST_LIBS_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}/VstEngine_artefacts/JuceLibraryCode
  ${juce_SOURCE_DIR}/modules)
target_link_libraries(reliability_stress_tests PRIVATE VstEngine)
add_test(NAME reliability_stress_tests COMMAND reliability_stress_tests)

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
