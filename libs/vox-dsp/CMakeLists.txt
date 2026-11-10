# vox_dsp: JUCE-free C++20 DSP shared by the suite products.
#
# FOUND-02 contents are byte-identical copies of reviewed legacy sources at
# integration/rack-1.0 f9c8bc6 (see PROVENANCE.csv; checked by
# suite_provenance). They keep their original include paths and the
# vstengine:: namespaces so that the copies stay identical; renaming is a
# separate, reviewed change.
add_library(vox_dsp STATIC
  dspcore/Primitives.h
  effects/EffectsChain.h effects/EffectsChain.cpp)
target_include_directories(vox_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(vox_dsp PUBLIC cxx_std_20)
set_target_properties(vox_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
