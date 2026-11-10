# ULTIMA VOX MUSIC suite: three standalone VST3 instruments sharing static
# libraries. Nothing here links a legacy (Rack, .vins, instrument ABI) target;
# suite_boundaries_check enforces that on every build.
include(${CMAKE_CURRENT_LIST_DIR}/SuiteIdentity.cmake)

# The VST3 module is a shared library, so every static library linked into it
# must be position independent (Linux; harmless on Windows).
set_target_properties(vox_ui PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_subdirectory(libs/vox-preset)
add_subdirectory(libs/vox-dsp)
add_subdirectory(libs/vox-testkit)
add_subdirectory(products)
add_subdirectory(tests/suite)
