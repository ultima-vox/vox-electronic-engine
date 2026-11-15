#pragma once

// VOX Drums factory content: the three original kit families, each with
// five patterns (A and B) and a 16-bar song; the eight genre kits of PR 137
// §5 with their pad roles; and one song preset per factory style (§5).
// Everything is original and synthesized: a kit is a table of synthesis
// settings in this file's .cpp, the patterns are the generator's output for
// fixed settings. No audio files and no third-party content. docs/ultima-vox-music/drums/FACTORY_CONTENT.md is
// the manifest; products/drums/factory-content.csv holds the hashes and a
// test checks them.

#include "Generator.h"

#include <map>
#include <string>

namespace vox::drums {

inline constexpr int factoryKitCount = 11;          // 0-2 original, 3-10 the genre kits of PR 137 §5
inline constexpr int originalFactoryKitCount = 3;

struct FactoryKitInfo {
    const char* name;      // "Psy Full-on"
    const char* family;    // "Psy / Full-on"
    Style styles[2];       // the generator styles of its patterns
    float tempo;           // the tempo the patterns were written for (information only)
};
const FactoryKitInfo& factoryKitInfo (int kit) noexcept;

// PR 137 §5 release status of a kit and of the content written for it.
// Nothing is "release-ready" yet: every kit still needs the owner's
// listening gate, and the acoustic kits (Rock, Metal) are built from the
// synth models and need licensed acoustic samples first.
enum class FactoryRelease : std::uint8_t { listeningPending, needsLicensedSamples };
FactoryRelease factoryKitRelease (int kit) noexcept;
const char* factoryReleaseText (FactoryRelease) noexcept;   // "listening-pending", "NOT-RELEASE-READY:needs-licensed-samples"

// The kit's pad roles (PR 137 §2.3), saved with the kit as DROL. The three
// original kits have none (explicitRoles false: the identity map, no DROL).
PadRoles factoryKitRoles (int kit);

// PR 137 §5: the factory kit each style's song preset is written for.
int factoryKitOfStyle (int style) noexcept;
// One song preset per factory style: 5 slots with B variations and a 32-bar
// timeline, Generate Song with the style's defaults and a fixed seed on the
// role map of its kit. False for an unknown style.
GeneratorSettings factoryStyleGenerator (int style) noexcept;
bool factoryStyleSong (int style, PatternStore::Everything& out);

// The kit: pad structure and the values of its kit-scope host parameters
// (level, tune, decay, pan, cutoff, sends; kit volume; delay and reverb).
kit::KitSettings factoryKit (int kit);
std::map<std::string, float> factoryKitParameters (int kit);

// The kit's patterns: slots 1-5 with B variations and a 16-bar song. A genre
// kit (3-10) has the song preset of its first style.
GeneratorSettings factoryGenerator (int kit, int slot) noexcept;
void factoryPatterns (int kit, PatternStore::Everything& out);

// "item,kind,sha256,release" lines: every kit, the patterns and song of the
// original kits, and the patterns and song of every style's song preset.
std::string factoryManifest();

} // namespace vox::drums
