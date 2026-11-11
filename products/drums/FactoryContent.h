#pragma once

// VOX Drums factory content: three kit families, each with five patterns
// (A and B) and a 16-bar song. Everything is original and synthesized: a
// kit is a table of synthesis settings in this file's .cpp, the patterns
// are the generator's output for fixed settings. No audio files and no
// third-party content. docs/ultima-vox-music/drums/FACTORY_CONTENT.md is
// the manifest; products/drums/factory-content.csv holds the hashes and a
// test checks them.

#include "Generator.h"

#include <map>
#include <string>

namespace vox::drums {

inline constexpr int factoryKitCount = 3;

struct FactoryKitInfo {
    const char* name;      // "Psy Full-on"
    const char* family;    // "Psy / Full-on"
    Style styles[2];       // the generator styles of its patterns
    float tempo;           // the tempo the patterns were written for (information only)
};
const FactoryKitInfo& factoryKitInfo (int kit) noexcept;

// The kit: pad structure and the values of its kit-scope host parameters
// (level, tune, decay, pan, cutoff, sends; kit volume; delay and reverb).
kit::KitSettings factoryKit (int kit);
std::map<std::string, float> factoryKitParameters (int kit);

// The kit's patterns: slots 1-5 with B variations and a 16-bar song.
GeneratorSettings factoryGenerator (int kit, int slot) noexcept;
void factoryPatterns (int kit, PatternStore::Everything& out);

// "item,kind,sha256" lines: every kit and every pattern slot of every kit.
std::string factoryManifest();

} // namespace vox::drums
