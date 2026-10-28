#pragma once

#include "acid/AcidEngine.h"
#include <array>
#include <cstddef>
#include <string_view>

namespace vstengine::acid {

// Identity inventory only. Pool indices are proposed until the Rack contract
// is reviewed; physical units, types and conversions are not yet frozen.
struct AcidControlIdentity {
  std::string_view id;
  std::size_t engineIndex; // parameterCount means an independent play-mode flag
  unsigned poolIndex;
};

inline constexpr std::array<AcidControlIdentity, 80> acidControlInventory{{
    {"waveform", 0, 0},        {"cutoff", 1, 1},
    {"resonance", 2, 2},       {"envelope", 3, 3},
    {"decay", 4, 4},           {"accent", 5, 5},
    {"slide", 6, 6},           {"drive", 7, 7},
    {"output", 8, 8},          {"osc_tune", 9, 9},
    {"osc_fine", 10, 10},      {"osc_pw", 11, 11},
    {"filter_mode", 12, 12},   {"key_tracking", 13, 13},
    {"amp_attack", 14, 14},    {"amp_decay", 15, 15},
    {"amp_sustain", 16, 16},   {"amp_release", 17, 17},
    {"flt_attack", 18, 18},    {"flt_sustain", 19, 19},
    {"flt_release", 20, 20},   {"dist_mode", 21, 21},
    {"dist_tone", 22, 22},     {"dist_mix", 23, 23},
    {"pan", 24, 24},           {"mod_target", 25, 25},
    {"mod_amount", 26, 26},    {"mod_attack", 27, 27},
    {"mod_decay", 28, 28},     {"mod_sustain", 29, 29},
    {"mod_release", 30, 30},   {"portamento", 31, 31},
    {"unison_voices", 32, 32}, {"unison_detune", 33, 33},
    {"stereo_spread", 34, 34}, {"lfo1_rate", 36, 35},
    {"lfo1_depth", 37, 36},    {"lfo1_wave", 38, 37},
    {"lfo1_target", 39, 38},   {"env2_target", 40, 39},
    {"env2_amount", 41, 40},   {"env2_attack", 42, 41},
    {"env2_decay", 43, 42},    {"env2_sustain", 44, 43},
    {"env2_release", 45, 44},  {"lfo2_rate", 46, 45},
    {"lfo2_depth", 47, 46},    {"lfo2_wave", 48, 47},
    {"lfo2_target", 49, 48},   {"lfo3_rate", 50, 49},
    {"lfo3_depth", 51, 50},    {"lfo3_wave", 52, 51},
    {"lfo3_target", 53, 52},   {"step_amount", 54, 53},
    {"step_target", 55, 54},   {"step_rate", 56, 55},
    {"step_smooth", 57, 56},   {"step_01", 58, 57},
    {"step_02", 59, 58},       {"step_03", 60, 59},
    {"step_04", 61, 60},       {"step_05", 62, 61},
    {"step_06", 63, 62},       {"step_07", 64, 63},
    {"step_08", 65, 64},       {"step_09", 66, 65},
    {"step_10", 67, 66},       {"step_11", 68, 67},
    {"step_12", 69, 68},       {"step_13", 70, 69},
    {"step_14", 71, 70},       {"step_15", 72, 71},
    {"step_16", 73, 72},       {"lfo1_sync", 74, 73},
    {"lfo2_sync", 75, 74},     {"lfo3_sync", 76, 75},
    {"legato", 77, 76},        {"retrigger", 77, 77},
    {"vel_to_accent", 77, 78}, {"slide_on_overlap", 77, 79},
}};

} // namespace vstengine::acid
