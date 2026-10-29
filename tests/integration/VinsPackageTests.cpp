// .vins packages (#73): the strict ZIP reader, the manifest schema, publisher
// signatures, folder discovery, conflicts, the per-user cache and the Rack
// catalogue. Hostile archives are built in memory from the real, signed
// fixtures produced by the build (test-vins/, instruments/).
#include "PluginProcessor.h"
#include "instrument/vins/VinsPackage.h"
#include "modules/BuiltInProvider.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {
using namespace vstengine;
namespace fs = std::filesystem;
namespace vins = instrument::vins;
using Bytes = std::vector<std::uint8_t>;

fs::path testVins, instrumentsDir, scratch;
int checks = 0;

void check(bool ok, const std::string &message) {
  ++checks;
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

Bytes readFile(const fs::path &p) {
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}

void writeFile(const fs::path &p, const Bytes &data) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
}

Bytes text(const std::string &s) { return Bytes(s.begin(), s.end()); }

std::string sha256(const Bytes &b) {
  return juce::SHA256(b.data(), b.size()).toHexString().toStdString();
}

std::vector<vins::ZipMember> members(const Bytes &archive) {
  std::vector<vins::ZipMember> out;
  std::string why;
  check(vins::readZip(archive, {}, out, why), "fixture parses: " + why);
  return out;
}

vins::ZipMember *find(std::vector<vins::ZipMember> &m, const std::string &name) {
  for (auto &x : m)
    if (x.name == name)
      return &x;
  return nullptr;
}

bool zipRejects(const Bytes &archive, const std::string &reason, vins::ZipLimits limits = {}) {
  std::vector<vins::ZipMember> out;
  std::string why;
  const bool ok = vins::readZip(archive, limits, out, why);
  if (!ok && why.find(reason) == std::string::npos)
    std::cerr << "  (rejected for: " << why << ")\n";
  return !ok && why.find(reason) != std::string::npos && out.empty();
}

// A fresh instruments folder holding the given archives.
fs::path folderWith(const std::string &name, const std::map<std::string, Bytes> &archives) {
  const auto dir = scratch / name;
  fs::remove_all(dir);
  fs::create_directories(dir);
  for (const auto &[file, data] : archives)
    writeFile(dir / file, data);
  return dir;
}

vins::LoadOptions options(const std::string &cache = "cache") {
  vins::LoadOptions o;
  o.cacheRoot = scratch / cache;
  o.trusted = vins::pinnedPublisherKeys();
  return o;
}

bool hasDiagnostic(const instrument::native::LoadReport &r, const std::string &part) {
  for (const auto &d : r.diagnostics)
    if (d.find(part) != std::string::npos)
      return true;
  return false;
}

std::string describe(const instrument::native::LoadReport &r) {
  std::string s;
  for (const auto &d : r.diagnostics)
    s += " | " + d;
  return s;
}

// A graph package built in memory (unsigned; graph packages hold no code).
Bytes graphPackage(const std::string &packageId, const std::string &instrumentId,
                   const std::string &graphJson, const std::string &extraManifest = "") {
  const auto graph = text(graphJson);
  const std::string manifest = "{\"vins\": 1, \"packageId\": \"" + packageId +
                               "\", \"version\": \"1.0.0\", \"name\": \"G\", \"vendor\": \"T\", "
                               "\"executionKind\": \"graph\", \"instruments\": [\"" +
                               instrumentId + "\"], \"graph\": \"graph/instrument.json\", " +
                               extraManifest + "\"files\": {\"graph/instrument.json\": \"" +
                               sha256(graph) + "\"}}";
  return vins::writeZip({{"manifest.json", text(manifest)}, {"graph/instrument.json", graph}});
}

std::string demoGraphJson() {
  for (auto &m : members(readFile(instrumentsDir / "VoxDemoSynthGraph.vins")))
    if (m.name == "graph/instrument.json")
      return std::string(m.data.begin(), m.data.end());
  check(false, "graph fixture holds graph/instrument.json");
  return {};
}

std::string withId(std::string json, const std::string &id) {
  const std::string from = "\"com.ultimavox.demo-synth-graph\"";
  const auto at = json.find(from);
  check(at != std::string::npos, "graph id anchor");
  return json.replace(at, from.size(), "\"" + id + "\"");
}

// Inserts `n` junk bytes after member `k` of a writeZip archive and fixes the
// directory offsets, so only the "contiguous members" rule can catch it.
Bytes withHiddenBytes(Bytes z, std::size_t k, std::size_t n) {
  auto u16 = [&](std::size_t at) { return static_cast<std::size_t>(z[at] | (z[at + 1] << 8)); };
  auto u32 = [&](std::size_t at) {
    return static_cast<std::size_t>(z[at]) | (static_cast<std::size_t>(z[at + 1]) << 8) |
           (static_cast<std::size_t>(z[at + 2]) << 16) | (static_cast<std::size_t>(z[at + 3]) << 24);
  };
  auto put32 = [&](std::size_t at, std::size_t v) {
    for (int i = 0; i < 4; ++i)
      z[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
  };
  std::size_t at = 0;
  for (std::size_t i = 0; i <= k; ++i)
    at += 30 + u16(at + 26) + u16(at + 28) + u32(at + 18);
  z.insert(z.begin() + static_cast<std::ptrdiff_t>(at), n, 0x5A);
  const std::size_t end = z.size() - 22;
  const std::size_t entries = u16(end + 10);
  std::size_t cd = u32(end + 16) + n;
  put32(end + 16, cd);
  for (std::size_t i = 0; i < entries; ++i) {
    if (u32(cd + 42) >= at)
      put32(cd + 42, u32(cd + 42) + n);
    cd += 46 + u16(cd + 28) + u16(cd + 30) + u16(cd + 32);
  }
  return z;
}

// ----------------------------------------------------------------- ZIP
void zipReader() {
  const std::vector<vins::ZipMember> sample = {{"manifest.json", text("{}")},
                                              {"module/win-x64/a.dll", text("abc")},
                                              {"graph/instrument.json", Bytes(5000, 7)}};
  const auto z = vins::writeZip(sample);
  std::vector<vins::ZipMember> back;
  std::string why;
  check(vins::readZip(z, {}, back, why) && back.size() == 3 && back[1].data == text("abc") &&
            back[2].data == Bytes(5000, 7),
        "write/read round trip: " + why);

  // Names: portable relative paths only.
  for (const char *bad : {"../x", "/abs", "a\\b", "C:x", "./x", "a//b", "a/", "a/../b", "",
                          "a b", "ä", "x\x01"})
    check(!vins::validMemberName(bad, 200), std::string("bad member name rejected: ") + bad);
  for (const char *good : {"manifest.json", "module/win-x64/VoxDemoSynth.dll", "a/b/c-1_2.json"})
    check(vins::validMemberName(good, 200), std::string("good member name accepted: ") + good);
  auto dup = sample;
  dup.push_back({"Manifest.JSON", text("x")});
  check(zipRejects(vins::writeZip(dup), "duplicate member"), "duplicate member (any case)");
  auto traversal = sample;
  traversal[1].name = "module/../../evil.dll";
  // writeZip does not validate; the reader must.
  check(zipRejects(vins::writeZip(traversal), "invalid member name"), "zip-slip member");

  // Structure.
  check(zipRejects(Bytes(z.begin(), z.begin() + static_cast<long>(z.size() / 2)), "not a ZIP"),
        "truncated archive");
  auto appended = z;
  appended.push_back(0);
  check(zipRejects(appended, "not a ZIP"), "bytes after the end record");
  auto prepended = z;
  prepended.insert(prepended.begin(), 16, 0);
  check(zipRejects(prepended, "central directory"), "bytes before the first member");
  check(zipRejects(withHiddenBytes(z, 0, 7), "gap or overlap"), "hidden bytes between members");
  check(zipRejects(withHiddenBytes(z, 2, 7), "between the last member"),
        "hidden bytes before the central directory");
  auto crc = z;
  crc[30 + std::string("manifest.json").size()] ^= 1; // first data byte
  check(zipRejects(crc, "CRC-32"), "corrupt member data");
  auto method = z;
  const std::size_t endAt = z.size() - 22;
  const std::size_t cd = z[endAt + 16] | (z[endAt + 17] << 8) | (z[endAt + 18] << 16) |
                         (static_cast<std::size_t>(z[endAt + 19]) << 24);
  method[cd + 10] = 12; // bzip2 in the first central record
  check(zipRejects(method, "compression method"), "unsupported compression method");
  auto encrypted = z;
  encrypted[cd + 8] |= 1;
  check(zipRejects(encrypted, "encrypted"), "encrypted member");
  auto symlink = z;
  symlink[cd + 5] = 3; // made by Unix
  symlink[cd + 40] = 0x00;
  symlink[cd + 41] = 0xA0; // S_IFLNK in the external attributes
  check(zipRejects(symlink, "symbolic link"), "symbolic link member");
  auto zip64 = z;
  for (int i = 0; i < 4; ++i)
    zip64[cd + 24 + static_cast<std::size_t>(i)] = 0xFF;
  check(zipRejects(zip64, "ZIP64"), "ZIP64 sizes");

  // Bombs and limits.
  vins::ZipLimits small;
  small.maxArchiveBytes = 100;
  check(zipRejects(z, "archive larger", small), "archive size limit");
  small = {};
  small.maxEntries = 2;
  check(zipRejects(z, "members", small), "member count limit");
  small = {};
  small.maxEntryBytes = 1000;
  check(zipRejects(z, "larger than 1000", small), "member size limit");
  small = {};
  small.maxTotalBytes = 4000;
  check(zipRejects(z, "in total", small), "total size limit");

  // Deflate: correct, then lying about its size in both directions, then a ratio bomb.
  auto deflated = [](const Bytes &raw) {
    juce::MemoryOutputStream sink;
    {
      juce::GZIPCompressorOutputStream gz(sink, 9, juce::GZIPCompressorOutputStream::windowBitsRaw);
      gz.write(raw.data(), raw.size());
    }
    return Bytes(static_cast<const std::uint8_t *>(sink.getData()),
                 static_cast<const std::uint8_t *>(sink.getData()) + sink.getDataSize());
  };
  auto deflateZip = [&](const Bytes &raw, std::uint32_t declared) {
    // One deflated member, hand assembled.
    const auto data = deflated(raw);
    const std::string name = "graph/x.json";
    const auto crcValue = vins::crc32(raw);
    Bytes z2;
    auto u16 = [&](std::uint32_t v) { z2.push_back(static_cast<std::uint8_t>(v)); z2.push_back(static_cast<std::uint8_t>(v >> 8)); };
    auto u32 = [&](std::uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); };
    u32(0x04034b50); u16(20); u16(0); u16(8); u16(0); u16(0x21); u32(crcValue);
    u32(static_cast<std::uint32_t>(data.size())); u32(declared); u16(static_cast<std::uint32_t>(name.size())); u16(0);
    z2.insert(z2.end(), name.begin(), name.end());
    z2.insert(z2.end(), data.begin(), data.end());
    const auto cdAt = static_cast<std::uint32_t>(z2.size());
    u32(0x02014b50); u16(20); u16(20); u16(0); u16(8); u16(0); u16(0x21); u32(crcValue);
    u32(static_cast<std::uint32_t>(data.size())); u32(declared); u16(static_cast<std::uint32_t>(name.size()));
    u16(0); u16(0); u16(0); u16(0); u32(0); u32(0);
    z2.insert(z2.end(), name.begin(), name.end());
    const auto cdSize = static_cast<std::uint32_t>(z2.size()) - cdAt;
    u32(0x06054b50); u16(0); u16(0); u16(1); u16(1); u32(cdSize); u32(cdAt); u16(0);
    return z2;
  };
  const Bytes raw(70000, 'a');
  std::vector<vins::ZipMember> ok;
  check(vins::readZip(deflateZip(raw, 70000), {}, ok, why) && ok.size() == 1 && ok[0].data == raw,
        "deflate member decodes: " + why);
  check(zipRejects(deflateZip(raw, 60000), "declared size"), "deflate longer than declared");
  check(zipRejects(deflateZip(raw, 80000), "declared size"), "deflate shorter than declared");
  check(zipRejects(deflateZip(Bytes(4u << 20, 0), 4u << 20), "compression ratio"),
        "compression-ratio bomb");
}

// --------------------------------------------------------- signatures
void publisherKeys() {
  const auto &keys = vins::pinnedPublisherKeys();
  check(keys.size() == 1 && keys[0].modulus.size() * 8 >= vins::minPublisherKeyBits &&
            keys[0].id.size() == 16,
        "one pinned publisher key of at least 3072 bits");
  std::cout << "pinned publisher key " << keys[0].id << " (" << keys[0].label << ")\n";
  auto m = members(readFile(testVins / "AbiTestTone.vins"));
  const auto manifest = find(m, "manifest.json")->data;
  const auto sig = find(m, "manifest.sig")->data;
  check(vins::verifySignature(keys[0], manifest, sig), "the build's own signature verifies");
  auto tampered = manifest;
  tampered[tampered.size() / 2] ^= 1;
  check(!vins::verifySignature(keys[0], tampered, sig), "a changed manifest fails");
  auto badSig = sig;
  badSig.back() ^= 1;
  check(!vins::verifySignature(keys[0], manifest, badSig), "a changed signature fails");
  check(!vins::verifySignature(keys[0], manifest, Bytes(sig.begin(), sig.end() - 1)),
        "a short signature fails");
  check(!vins::verifySignature(keys[0], manifest, Bytes(sig.size(), 0xFF)),
        "a signature >= the modulus fails");
  auto shortKey = keys[0];
  shortKey.modulus.resize(256);
  check(!vins::verifySignature(shortKey, manifest, Bytes(256, 1)), "keys under 3072 bits are refused");
  auto untrusted = members(readFile(testVins / "AbiTestTone-untrusted.vins"));
  check(!vins::verifySignature(keys[0], find(untrusted, "manifest.json")->data,
                               find(untrusted, "manifest.sig")->data),
        "a foreign publisher's signature fails");
}

// ----------------------------------------------------------- manifest
void manifestSchema() {
  vins::Manifest m;
  std::string why;
  auto fixture = members(readFile(testVins / "AbiTestTone.vins"));
  const auto good = find(fixture, "manifest.json")->data;
  check(vins::parseManifest(good, m, why) && m.kind == vins::ExecutionKind::native &&
            m.packageId == "test.abi.v2.tone" && m.instruments.size() == 1 && m.publisher.size() == 16,
        "the built native manifest parses: " + why);
  const std::string base = std::string(good.begin(), good.end());
  auto rejects = [&](const std::string &from, const std::string &to, const std::string &reason) {
    auto t = base;
    const auto at = t.find(from);
    check(at != std::string::npos, "manifest anchor " + from);
    t.replace(at, from.size(), to);
    vins::Manifest out;
    std::string w;
    check(!vins::parseManifest(text(t), out, w) && w.find(reason) != std::string::npos,
          "manifest rejects " + to + " (" + w + ")");
  };
  rejects("\"vins\": 1", "\"vins\": 2", "schema");
  rejects("\"version\": \"1.0.0\"", "\"version\": \"1.0\"", "MAJOR.MINOR.PATCH");
  rejects("\"version\": \"1.0.0\"", "\"version\": \"01.0.0\"", "MAJOR.MINOR.PATCH");
  rejects("\"packageId\": \"test.abi.v2.tone\"", "\"packageId\": \"Test Tone\"", "packageId");
  rejects("\"executionKind\": \"native\"", "\"executionKind\": \"script\"", "executionKind");
  rejects("\"abi\": 2", "\"abi\": 3", "abi 2");
  rejects("\"vins\": 1", "\"vins\": 1, \"autorun\": \"x\"", "unknown key");
  rejects(std::string("\"") + vins::nativePlatform + "\": ", "\"mac-arm64\": ",
          "native module entry");
}

// ------------------------------------------------- package loading
void loadsGoodPackages() {
  const auto dir = folderWith("good", {{"Tone.vins", readFile(testVins / "AbiTestTone.vins")},
                                       {"Graph.vins", readFile(instrumentsDir / "VoxDemoSynthGraph.vins")},
                                       {"readme.txt", text("not a package")}});
  const auto r = vins::loadFolder(dir, options());
  check(r.diagnostics.empty() && r.providers.size() == 2,
        "a native and a graph package load together" + describe(r));
  check(vins::loadFolder(scratch / "does-not-exist", options()).providers.empty() &&
            vins::loadFolder(scratch / "does-not-exist", options()).diagnostics.empty(),
        "no instruments folder: nothing installed, no error");
  // Unicode and spaces in the install path.
  const auto unicode = scratch / fs::path(u8"Vox Müsic — инструменты") / "instruments";
  fs::remove_all(unicode.parent_path());
  writeFile(unicode / "Tone.vins", readFile(testVins / "AbiTestTone.vins"));
  const auto u = vins::loadFolder(unicode, options("cache-unicode"));
  check(u.providers.size() == 1 && u.diagnostics.empty(), "Unicode/space install path" + describe(u));
}

void rejectsHostilePackages() {
  const auto tone = readFile(testVins / "AbiTestTone.vins");
  auto variant = [&](auto edit) {
    auto m = members(tone);
    edit(m);
    return vins::writeZip(m);
  };
  struct Case {
    std::string name;
    Bytes archive;
    std::string reason;
  };
  const std::string moduleName = std::string("module/") + vins::nativePlatform + "/";
  auto moduleMember = [&](std::vector<vins::ZipMember> &m) -> vins::ZipMember & {
    for (auto &x : m)
      if (x.name.rfind(moduleName, 0) == 0)
        return x;
    check(false, "fixture has a module for this platform");
    return m.front();
  };
  std::vector<Case> cases = {
      {"unsigned", variant([](auto &m) {
         m.erase(std::remove_if(m.begin(), m.end(), [](auto &x) { return x.name == "manifest.sig"; }),
                 m.end());
       }),
       "signature invalid"},
      {"foreign-key", readFile(testVins / "AbiTestTone-untrusted.vins"), "is not trusted"},
      {"edited-manifest", variant([](auto &m) {
         auto &j = find(m, "manifest.json")->data;
         auto s = std::string(j.begin(), j.end());
         s.replace(s.find("ABI Test Tone"), 13, "ABI Test Tonf");
         j = text(s);
       }),
       "signature invalid"},
      {"patched-dll", variant([&](auto &m) { moduleMember(m).data[100] ^= 1; }), "fails its SHA-256"},
      {"extra-member", variant([](auto &m) { m.push_back({"module/extra.dll", text("MZ")}); }),
       "not listed"},
      {"missing-member", variant([&](auto &m) {
         const auto name = moduleMember(m).name;
         m.erase(std::remove_if(m.begin(), m.end(), [&](auto &x) { return x.name == name; }), m.end());
       }),
       "is missing from the archive"},
      {"no-manifest", variant([](auto &m) {
         m.erase(std::remove_if(m.begin(), m.end(), [](auto &x) { return x.name == "manifest.json"; }),
                 m.end());
       }),
       "manifest.json missing"},
      {"not-zip", text("MZ this is a DLL renamed to .vins"), "not a ZIP"},
      {"bad-graph", graphPackage("t.bad-graph", "t.bad-graph", "{\"graphVersion\": 9}"), "graph rejected"},
      {"graph-with-native", graphPackage("t.mixed", "t.mixed", demoGraphJson(), "\"native\": {}, "),
       "no native section"},
      {"graph-wrong-id", graphPackage("t.wrong", "t.other-id", demoGraphJson()), "differs from the manifest"},
  };
  for (const auto &c : cases) {
    const auto dir = folderWith("hostile-" + c.name, {{c.name + ".vins", c.archive}});
    const auto r = vins::loadFolder(dir, options("cache-" + c.name));
    check(r.providers.empty() && hasDiagnostic(r, c.reason),
          c.name + " is rejected with '" + c.reason + "'" + describe(r));
    // Signature and hashes are checked before anything reaches the cache.
    check(!fs::exists(scratch / ("cache-" + c.name) / "test.abi.v2.tone"),
          c.name + ": nothing was extracted to the cache");
  }
}

void conflictsRejectEveryone() {
  const auto tone = readFile(testVins / "AbiTestTone.vins");
  auto same = vins::loadFolder(folderWith("dup-package", {{"A.vins", tone}, {"B.vins", tone}}), options());
  check(same.providers.empty() && hasDiagnostic(same, "A.vins") && hasDiagnostic(same, "B.vins"),
        "two packages with one packageId are both rejected" + describe(same));
  auto clash = vins::loadFolder(
      folderWith("dup-instrument",
                 {{"A.vins", tone},
                  {"B.vins", graphPackage("t.clash", "test.abi.v2.tone",
                                          withId(demoGraphJson(), "test.abi.v2.tone"))}}),
      options());
  check(clash.providers.empty() && clash.diagnostics.size() == 2,
        "two packages offering one instrument id are both rejected" + describe(clash));
}

void notRegularFilesAreIgnored() {
  const auto dir = folderWith("odd", {});
  fs::create_directories(dir / "Folder.vins");
  writeFile(dir / "Real.vins", readFile(instrumentsDir / "VoxDemoSynthGraph.vins"));
  std::error_code ec;
  fs::create_symlink(dir / "Real.vins", dir / "Link.vins", ec);
  const auto r = vins::loadFolder(dir, options());
  check(r.providers.size() == 1 && hasDiagnostic(r, "Folder.vins") &&
            (ec || hasDiagnostic(r, "Link.vins")),
        "directories and symlinks named *.vins are not loaded" + describe(r));
}

void cacheIsVerifiedAndReused() {
  const auto dir = folderWith("cache-test", {{"Tone.vins", readFile(testVins / "AbiTestTone.vins")}});
  const auto cache = scratch / "cache-reuse";
  fs::remove_all(cache);
  auto o = options("cache-reuse");
  check(vins::loadFolder(dir, o).providers.size() == 1, "first load extracts to the cache");
  fs::path module;
  for (const auto &e : fs::recursive_directory_iterator(cache))
    if (e.is_regular_file())
      module = e.path();
  check(!module.empty() && module.string().find("test.abi.v2.tone") != std::string::npos,
        "the module lives in the per-package cache");
  const auto stamp = fs::last_write_time(module);
  check(vins::loadFolder(dir, o).providers.size() == 1 && fs::last_write_time(module) == stamp,
        "an intact cache entry is reused, not rewritten");
  // A planted extra file makes the entry untrusted; it is rebuilt.
  writeFile(module.parent_path() / "planted.dll", text("MZ"));
  // (the module itself stays loaded process-wide, so the rebuilt entry is
  // verified but the same loaded image is reused)
  const auto r = vins::loadFolder(dir, o);
  check(r.providers.size() == 1 || hasDiagnostic(r, "cache"),
        "a tampered cache entry is replaced or reported" + describe(r));
  check(vins::defaultCacheRoot().is_absolute(), "a per-user cache root exists");
}

void pluginFolderResolution() {
#if defined(_WIN32)
  const fs::path product = L"C:\\Program Files\\Common Files\\VST3\\vox music";
  const auto bundle = product / L"Vox Electronic Engine.vst3" / L"Contents" / L"x86_64-win" /
                      L"Vox Electronic Engine.vst3";
#else
  const fs::path product = "/usr/lib/vst3/vox music";
  const auto bundle = product / "Vox Electronic Engine.vst3" / "Contents" / "x86_64-linux" /
                      "Vox Electronic Engine.so";
#endif
  check(vins::instrumentsFolderForPlugin(bundle) == product / "instruments",
        "bundle: instruments next to the .vst3 folder");
  check(vins::instrumentsFolderForPlugin(product / "Vox Electronic Engine.vst3") ==
            product / "instruments",
        "single-file VST3: instruments next to it");
  check(!vins::instrumentsFolderForPlugin(product / "vox.exe") &&
            !vins::instrumentsFolderForPlugin("relative/Vox.vst3"),
        "no instruments folder for other binaries or relative paths");
  check(vins::loadInstalled().providers.empty(),
        "this test executable is not a VST3: the DAW path finds nothing");
}

void rackCatalogue() {
  const auto dir = folderWith("rack", {{"Tone.vins", readFile(testVins / "AbiTestTone.vins")},
                                       {"Graph.vins", readFile(instrumentsDir / "VoxDemoSynthGraph.vins")}});
  auto with = std::make_unique<VstEngineAudioProcessor>(modules::createBuiltInProvider(),
                                                        vins::loadFolder(dir, options()));
  auto without = std::make_unique<VstEngineAudioProcessor>(modules::createBuiltInProvider(),
                                                           instrument::native::LoadReport{});
  bool tone = false, graph = false;
  for (const auto *d : with->availableInstruments()) {
    tone = tone || d->id == "test.abi.v2.tone";
    graph = graph || d->id == "com.ultimavox.demo-synth-graph";
  }
  check(tone && graph && with->nativeModuleDiagnostics().empty(), "both packages in the slot catalogue");
  check(with->getParameters().size() == without->getParameters().size(),
        "installing packages adds no host parameter");
  with->prepareToPlay(48000.0, 256);
  juce::String diagnostic;
  check(with->loadSlotInstrument(4, "com.ultimavox.demo-synth-graph", diagnostic),
        "graph package loads into a slot: " + diagnostic.toStdString());
  juce::AudioBuffer<float> audio(2, 256);
  juce::MidiBuffer midi;
  double energy = 0;
  for (int b = 0; b < 8; ++b) {
    midi.clear();
    if (b == 1)
      midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    audio.clear();
    with->processBlock(audio, midi);
    for (int i = 0; i < 256; ++i)
      energy += audio.getSample(0, i) * audio.getSample(0, i);
  }
  check(energy > 1.0, "the graph package plays on CH1");
  auto reported = std::make_unique<VstEngineAudioProcessor>(
      modules::createBuiltInProvider(),
      vins::loadFolder(folderWith("rack-bad", {{"Bad.vins", text("junk")}}), options()));
  check(!reported->nativeModuleDiagnostics().empty(),
        "a rejected package is reported by the Rack, which still starts");
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "usage: vins_package_tests <test-vins dir> <instruments dir> <scratch dir>\n";
    return 2;
  }
  testVins = fs::absolute(argv[1]);
  instrumentsDir = fs::absolute(argv[2]);
  scratch = fs::absolute(argv[3]);
  fs::remove_all(scratch);
  fs::create_directories(scratch);
  juce::ScopedJuceInitialiser_GUI gui;
  zipReader();
  publisherKeys();
  manifestSchema();
  loadsGoodPackages();
  rejectsHostilePackages();
  conflictsRejectEveryone();
  notRegularFilesAreIgnored();
  cacheIsVerifiedAndReused();
  pluginFolderResolution();
  rackCatalogue();
  std::cout << "vins_package_tests: " << checks << " checks passed\n";
  return 0;
}
