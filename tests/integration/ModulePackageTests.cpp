// Development package resolution: versioned <packId>.voxinst/<version> folders
// are validated (manifest, ABI range, arch, safe module path, SHA-256, instrument
// ids) and the highest valid semantic version is loaded into the real Rack.
#include "PluginProcessor.h"
#include "instrument/native/ModulePackages.h"
#include "modules/BuiltInProvider.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {
using namespace vstengine;
namespace fs = std::filesystem;
namespace native = instrument::native;
constexpr const char *toneId = "test.abi.v2.tone";
constexpr int blockSize = 128;
fs::path modulePath;
std::string moduleSha;
int checks = 0;

void check(bool ok, const std::string &message) {
  ++checks;
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

bool mentions(const std::vector<std::string> &diagnostics, const std::string &a,
              const std::string &b = {}) {
  return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const std::string &d) {
    return d.find(a) != std::string::npos && d.find(b) != std::string::npos;
  });
}

void dump(const std::vector<std::string> &diagnostics) {
  for (const auto &d : diagnostics)
    std::cout << "  diagnostic: " << d << '\n';
}

std::string sha256(const fs::path &file) {
  return juce::SHA256(juce::File(juce::String(file.string()))).toHexString().toStdString();
}

struct Manifest {
  std::string packId = toneId;
  std::string packVersion; // defaults to the folder name
  std::string abi = R"({"min": 2, "max": 2})";
  std::string arch = native::packageArch;
  std::string module; // defaults to the module file name
  std::string sha;    // defaults to the real hash
  std::string instrumentIds = R"(["test.abi.v2.tone"])";
  bool copyModule = true;
};

fs::path makeVersion(const fs::path &root, const std::string &folder, const std::string &version,
                     Manifest m = {}) {
  const auto dir = root / (folder + native::packageSuffix) / version;
  const auto moduleDir = dir / "module" / native::packageArch;
  fs::create_directories(moduleDir);
  fs::create_directories(dir / "ui");
  fs::create_directories(dir / "content");
  const auto fileName = modulePath.filename().string();
  if (m.copyModule)
    fs::copy_file(modulePath, moduleDir / fileName, fs::copy_options::overwrite_existing);
  if (m.packVersion.empty())
    m.packVersion = version;
  if (m.module.empty())
    m.module = fileName;
  if (m.sha.empty())
    m.sha = moduleSha;
  const std::string json = "{\n  \"manifestVersion\": 1,\n  \"packId\": \"" + m.packId +
                           "\",\n  \"packVersion\": \"" + m.packVersion +
                           "\",\n  \"abi\": " + m.abi + ",\n  \"arch\": \"" + m.arch +
                           "\",\n  \"module\": \"" + m.module + "\",\n  \"sha256\": \"" +
                           m.sha + "\",\n  \"instrumentIds\": " + m.instrumentIds + "\n}\n";
  check(juce::File(juce::String(dir.string()))
            .getChildFile("manifest.json")
            .replaceWithText(juce::String(json), false, false, "\n"),
        "write manifest " + dir.string());
  return dir;
}

const native::ResolvedPackage *packageOf(const native::PackageSelection &s,
                                         const std::string &packId) {
  for (const auto &p : s.packages)
    if (p.packId == packId)
      return &p;
  return nullptr;
}

void versionParsing() {
  native::PackageVersion v;
  check(native::parsePackageVersion("1.10.0", v) && v.major == 1 && v.minor == 10 &&
            v.patch == 0,
        "1.10.0 parses");
  native::PackageVersion a, b;
  check(native::parsePackageVersion("1.10.0", a) && native::parsePackageVersion("1.9.0", b) &&
            a > b,
        "versions compare numerically, not lexicographically");
  for (const char *bad : {"", "1", "1.0", "1.0.0.0", "01.0.0", "1.0.0-beta", "1..0", "-1.0.0",
                          "1.0.x", "99999999999.0.0", " 1.0.0"})
    check(!native::parsePackageVersion(bad, v), std::string("rejects version '") + bad + "'");
}

void resolution(const fs::path &root) {
  const auto rel = native::resolvePackages("relative/root");
  check(rel.modules.empty() && rel.diagnostics.size() == 1, "relative root is rejected");

  // 1.0.0 valid; 1.2.0 has a wrong hash -> skipped with diagnostic, 1.0.0 wins.
  makeVersion(root, toneId, "1.0.0");
  Manifest corrupt;
  corrupt.sha = std::string(64, '0');
  makeVersion(root, toneId, "1.2.0", corrupt);
  auto s = native::resolvePackages(root);
  dump(s.diagnostics);
  check(s.modules.size() == 1 && s.packages.size() == 1, "one package selected");
  check(s.packages[0].packVersion == "1.0.0", "corrupt 1.2.0 skipped, 1.0.0 selected");
  check(mentions(s.diagnostics, "1.2.0", "sha256"), "wrong hash is reported");
  check(s.modules[0].is_absolute() && fs::exists(s.modules[0]), "selected module is absolute");

  // 1.9.0 and 1.10.0 valid: numeric ordering selects 1.10.0.
  makeVersion(root, toneId, "1.9.0");
  makeVersion(root, toneId, "1.10.0");
  s = native::resolvePackages(root);
  check(s.packages.size() == 1 && s.packages[0].packVersion == "1.10.0",
        "1.10.0 selected over 1.9.0 and 1.2.0");
  check(s.diagnostics.empty(), "no diagnostics once the highest version is valid");

  // Higher, invalid versions are reported and skipped.
  Manifest abi;
  abi.abi = R"({"min": 3, "max": 4})";
  makeVersion(root, toneId, "2.0.0", abi);
  Manifest escape;
  escape.module = std::string("../../../1.10.0/module/") + native::packageArch + "/" +
                  modulePath.filename().string();
  escape.copyModule = false;
  makeVersion(root, toneId, "3.0.0", escape);
  Manifest missing;
  missing.copyModule = false;
  makeVersion(root, toneId, "4.0.0", missing);
  Manifest wrongVersion;
  wrongVersion.packVersion = "5.0.1";
  makeVersion(root, toneId, "5.0.0", wrongVersion);
  Manifest wrongArch;
  wrongArch.arch = "arm64";
  makeVersion(root, toneId, "6.0.0", wrongArch);
  fs::create_directories(root / (std::string(toneId) + native::packageSuffix) / "latest");

  // packId/folder mismatch and a manifest whose instrument ids the module lacks.
  makeVersion(root, "wrong.pack", "1.0.0");
  Manifest ids;
  ids.packId = "ids.mismatch";
  ids.instrumentIds = R"(["test.abi.v2.other"])";
  makeVersion(root, "ids.mismatch", "1.0.0", ids);
  fs::create_directories(root / "not-a-package");

  s = native::resolvePackages(root);
  dump(s.diagnostics);
  const auto *tone = packageOf(s, toneId);
  check(tone != nullptr && tone->packVersion == "1.10.0",
        "invalid higher versions fall back to 1.10.0");
  check(mentions(s.diagnostics, "2.0.0", "excludes host ABI 2"), "ABI range is reported");
  check(mentions(s.diagnostics, "3.0.0", "'..'"), "'..' module path is reported");
  check(mentions(s.diagnostics, "4.0.0", "module file missing"), "missing module reported");
  check(mentions(s.diagnostics, "5.0.0", "packVersion"), "packVersion mismatch reported");
  check(mentions(s.diagnostics, "6.0.0", "arch"), "arch mismatch reported");
  check(mentions(s.diagnostics, "latest", "MAJOR.MINOR.PATCH"), "bad version folder reported");
  check(mentions(s.diagnostics, "wrong.pack/1.0.0", "packId"), "packId mismatch reported");
  check(mentions(s.diagnostics, "wrong.pack: no valid version"), "pack without valid version");
  check(packageOf(s, "wrong.pack") == nullptr, "mismatched packId is not selected");
  check(packageOf(s, "ids.mismatch") != nullptr, "ids mismatch is only detectable after load");
  check(s.packages.size() == 2 && s.modules.size() == 2, "two packages resolved");
  check(!mentions(s.diagnostics, "not-a-package"), "non-package folders are ignored");

  const auto again = native::resolvePackages(root);
  check(again.modules == s.modules && again.diagnostics == s.diagnostics,
        "resolution is deterministic");
}

std::unique_ptr<VstEngineAudioProcessor> makeProcessor(native::LoadReport report) {
  auto p = std::make_unique<VstEngineAudioProcessor>(modules::createBuiltInProvider(),
                                                     std::move(report));
  for (const char *id : {"slot01Enabled", "slot02Enabled"})
    if (auto *enabled = p->parameters().getParameter(id))
      enabled->setValueNotifyingHost(0.0f);
  p->prepareToPlay(48000.0, blockSize);
  return p;
}

int channelOf(VstEngineAudioProcessor &p, std::size_t slot) {
  char id[32];
  std::snprintf(id, sizeof(id), "slot%02zuMidiIn", slot + 1);
  const auto *value = p.parameters().getRawParameterValue(id);
  check(value != nullptr, "slot MidiIn parameter exists");
  return static_cast<int>(std::lround(value->load()));
}

double playEnergy(VstEngineAudioProcessor &p, int channel, int blocks, int on, int fromBlock) {
  juce::AudioBuffer<float> audio(2, blockSize);
  juce::MidiBuffer midi;
  double sum = 0;
  for (int b = 0; b < blocks; ++b) {
    midi.clear();
    if (b == on)
      midi.addEvent(juce::MidiMessage::noteOn(channel, 60, 0.9f), 0);
    audio.clear();
    p.processBlock(audio, midi);
    if (b >= fromBlock)
      for (int i = 0; i < blockSize; ++i)
        sum += static_cast<double>(audio.getSample(0, i)) * audio.getSample(0, i);
  }
  return sum;
}

void loadsIntoRack(const fs::path &root) {
  auto report = native::loadPackages(root);
  dump(report.diagnostics);
  check(report.providers.size() == 1, "only the matching package yields a provider");
  check(mentions(report.diagnostics, "ids.mismatch/1.0.0", "differ from manifest"),
        "instrument id mismatch drops the provider");

  auto p = makeProcessor(std::move(report));
  check(mentions(p->nativeModuleDiagnostics(), "ids.mismatch"),
        "package diagnostics reach the processor");
  bool listed = false;
  for (const auto *d : p->availableInstruments())
    listed |= d->id == toneId && d->providerId == native::providerId;
  check(listed, "packaged instrument offered next to built-ins");

  constexpr std::size_t slot = 2;
  juce::String diagnostic;
  check(p->loadSlotInstrument(slot, toneId, diagnostic),
        "packaged instrument loads into slot: " + diagnostic.toStdString());
  check(p->instrumentRack().state()[slot].instrumentId == toneId, "slot holds packaged id");
  const int channel = channelOf(*p, slot);
  check(channel >= 1, "slot got a MIDI channel");
  check(playEnergy(*p, channel, 4, -1, 0) == 0.0, "silent without MIDI");
  check(playEnergy(*p, channel, 8, 1, 2) > 1.0, "NoteOn through the Rack is audible");
}

void buildTreePackage(const fs::path &root) {
  const auto s = native::resolvePackages(root);
  dump(s.diagnostics);
  const auto *tone = packageOf(s, toneId);
  check(tone != nullptr && tone->packVersion == "1.0.0", "build-tree package resolves");
  check(tone->instrumentIds == std::vector<std::string>{toneId}, "build-tree ids");
  check(s.diagnostics.empty(), "build-tree package has no diagnostics");
  const auto report = native::loadPackages(root);
  check(report.providers.size() == 1 && report.diagnostics.empty(),
        "build-tree package loads");
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: module_package_tests <absolute test module path> <build package root>\n";
    return 2;
  }
  modulePath = fs::path(argv[1]);
  const fs::path buildRoot(argv[2]);
  juce::ScopedJuceInitialiser_GUI gui;
  moduleSha = sha256(modulePath);
  check(moduleSha.size() == 64, "module hash computed");

  std::random_device rd;
  const auto root = fs::temp_directory_path() /
                    ("vox_module_packages_" + std::to_string(rd()) + std::to_string(rd()));
  fs::remove_all(root);
  fs::create_directories(root);

  versionParsing();
  resolution(root);
  loadsIntoRack(root);
  buildTreePackage(buildRoot);

  // Modules stay mapped until process exit (no hot unload); on POSIX removing
  // the files is still safe, elsewhere leftovers are ignored.
  std::error_code ec;
  fs::remove_all(root, ec);
  std::cout << "module_package_tests: " << checks << " checks passed\n";
  return 0;
}
