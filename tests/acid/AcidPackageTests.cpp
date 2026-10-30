// Loads the development-signed VoxAcid303.vins the way the Rack loads an
// instruments folder. Not a DAW acceptance test.
#include "instrument/vins/VinsPackage.h"
#include "instrument/vins/VinsTrust.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
int checks = 0;

void check(bool ok, const std::string &message) {
  ++checks;
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

vstengine::instrument::native::LoadReport load(const fs::path &folder, const fs::path &cache) {
  vstengine::instrument::vins::LoadOptions options;
  options.cacheRoot = cache;
  options.trusted = vstengine::instrument::vins::pinnedPublisherKeys();
  return vstengine::instrument::vins::loadFolder(folder, options);
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: acid_package_tests <VoxAcid303.vins> <scratch>\n";
    return 2;
  }
  const fs::path package = argv[1];
  const fs::path scratch = argv[2];
  fs::remove_all(scratch);
  const fs::path folder = scratch / "instruments";
  fs::create_directories(folder);
  fs::copy_file(package, folder / "VoxAcid303.vins");
  const auto loaded = load(folder, scratch / "cache");
  if (loaded.providers.size() != 1) {
    for (const auto &line : loaded.diagnostics)
      std::cerr << line << '\n';
  }
  check(loaded.providers.size() == 1, "VoxAcid303.vins loads under the development key");
  const auto descriptors = loaded.providers[0]->descriptors();
  check(descriptors.size() == 1 && descriptors[0].id == "com.ultimavox.acid",
        "package publishes com.ultimavox.acid");
  const auto &acid = descriptors[0];
  check(acid.directParameters && acid.parameters.size() == 80, "80 direct parameters");
  std::set<std::int16_t> pools;
  for (const auto &parameter : acid.parameters) {
    check(parameter.automatable, parameter.id + " is automatable");
    check(parameter.poolIndex >= 0 && parameter.poolIndex < 80, parameter.id + " pool index");
    check(pools.insert(parameter.poolIndex).second, parameter.id + " pool index is unique");
  }
  check(pools.size() == 80, "poolIndex covers 0..79");
  check(acid.soundLayout && acid.soundLayout->sections.size() >= 1, "SOUND layout is attached");
  std::size_t controls = 0;
  for (const auto &section : acid.soundLayout->sections)
    controls += section.controls.size();
  check(controls == 80, "SOUND layout names every parameter once");
  check(acid.patternContent &&
            acid.patternContent->caps.playback == VOX_PATTERN_PLAYBACK_HOST &&
            acid.patternContent->caps.laneCount == 1,
        "pattern is one HOST lane");
  const auto fields = acid.patternContent->caps.stepFields;
  check((fields & VOX_FIELD_NOTE) && (fields & VOX_FIELD_OCTAVE) && (fields & VOX_FIELD_ACCENT) &&
            (fields & VOX_FIELD_SLIDE) && (fields & VOX_FIELD_GATE),
        "NOTE / OCTAVE / ACCENT / SLIDE / GATE are declared");
  check(!acid.patternContent->factory.empty(), "factory pattern is present");

  auto bytes = std::vector<char>(fs::file_size(package));
  {
    std::ifstream in(package, std::ios::binary);
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  bytes.back() = static_cast<char>(bytes.back() ^ 0x5a);
  const fs::path badFolder = scratch / "tampered";
  fs::create_directories(badFolder);
  {
    std::ofstream out(badFolder / "VoxAcid303.vins", std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  const auto rejected = load(badFolder, scratch / "cache-bad");
  check(rejected.providers.empty(), "a tampered .vins is not loaded");
  std::cout << "Acid package tests passed (" << checks << ")\n";
  return 0;
}
