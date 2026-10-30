// vox_vins_pack: builds .vins instrument packages (docs/architecture/
// VINS_PACKAGES.md). Signing is done between the two steps by the caller
// (cmake/MakeVins.cmake uses `openssl dgst -sha256 -sign`), so no private
// key ever reaches this tool.
//
//   vox_vins_pack manifest <manifest.json> key=value... member=<name>=<file>...
//     keys: id, version, name, vendor, kind (graph|native), instrument
//     (repeatable), graph (member), module (<platform>=<member>), publisher
//     (key id; native only), ui (<instrument>=<layout member>, repeatable)
//   vox_vins_pack zip <out.vins> <manifest.json> [sig=<manifest.sig>] member=<name>=<file>...
//   vox_vins_pack keyid <modulus file from `openssl rsa -modulus -noout`>
#include "instrument/vins/VinsTrust.h"
#include "instrument/vins/VinsZip.h"

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {
using vstengine::instrument::vins::ZipMember;

std::vector<std::uint8_t> readFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("cannot read " + path);
  return {std::istreambuf_iterator<char>(in), {}};
}

void writeFile(const std::string &path, const std::vector<std::uint8_t> &data) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
  if (!out)
    throw std::runtime_error("cannot write " + path);
}

std::string jsonString(const std::string &s) {
  return juce::JSON::toString(juce::var(juce::String::fromUTF8(s.c_str()))).toStdString();
}

// member=<name>=<file> arguments, in argument order.
std::vector<std::pair<std::string, std::string>> memberArgs(int argc, char **argv, int from) {
  std::vector<std::pair<std::string, std::string>> out;
  for (int i = from; i < argc; ++i) {
    const std::string a = argv[i];
    if (a.rfind("member=", 0) != 0)
      continue;
    const auto rest = a.substr(7);
    const auto eq = rest.find('=');
    if (eq == std::string::npos)
      throw std::runtime_error("member needs <name>=<file>: " + a);
    const auto name = rest.substr(0, eq);
    if (!vstengine::instrument::vins::validMemberName(name, 200))
      throw std::runtime_error("invalid member name " + name);
    out.emplace_back(name, rest.substr(eq + 1));
  }
  return out;
}

int manifest(int argc, char **argv) {
  std::map<std::string, std::string> keys;
  std::vector<std::string> instruments, modules, ui;
  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    const auto eq = a.find('=');
    if (eq == std::string::npos || a.rfind("member=", 0) == 0)
      continue;
    const auto k = a.substr(0, eq), v = a.substr(eq + 1);
    if (k == "instrument")
      instruments.push_back(v);
    else if (k == "module")
      modules.push_back(v);
    else if (k == "ui")
      ui.push_back(v);
    else
      keys[k] = v;
  }
  std::map<std::string, std::string> files;
  for (const auto &[name, file] : memberArgs(argc, argv, 3)) {
    const auto data = readFile(file);
    files[name] = juce::SHA256(data.data(), data.size()).toHexString().toStdString();
  }
  std::string j = "{\n  \"vins\": 1,\n";
  for (const char *k : {"id", "version", "name", "vendor", "kind"})
    if (keys.count(k) == 0)
      throw std::runtime_error(std::string("missing ") + k);
  j += "  \"packageId\": " + jsonString(keys["id"]) + ",\n";
  j += "  \"version\": " + jsonString(keys["version"]) + ",\n";
  j += "  \"name\": " + jsonString(keys["name"]) + ",\n";
  j += "  \"vendor\": " + jsonString(keys["vendor"]) + ",\n";
  j += "  \"executionKind\": " + jsonString(keys["kind"]) + ",\n";
  j += "  \"instruments\": [";
  for (std::size_t i = 0; i < instruments.size(); ++i)
    j += (i ? ", " : "") + jsonString(instruments[i]);
  j += "],\n";
  if (keys["kind"] == "graph") {
    j += "  \"graph\": " + jsonString(keys["graph"]) + ",\n";
  } else {
    j += "  \"native\": {\n    \"abi\": 2,\n    \"publisher\": " + jsonString(keys["publisher"]) +
         ",\n    \"modules\": {";
    for (std::size_t i = 0; i < modules.size(); ++i) {
      const auto eq = modules[i].find('=');
      j += (i ? ", " : "") + jsonString(modules[i].substr(0, eq)) + ": " +
           jsonString(modules[i].substr(eq + 1));
    }
    j += "}\n  },\n";
  }
  if (!ui.empty()) {
    j += "  \"ui\": {";
    for (std::size_t i = 0; i < ui.size(); ++i) {
      const auto eq = ui[i].find('=');
      if (eq == std::string::npos)
        throw std::runtime_error("ui needs <instrument>=<member>: " + ui[i]);
      j += (i ? ", " : "") + jsonString(ui[i].substr(0, eq)) + ": " +
           jsonString(ui[i].substr(eq + 1));
    }
    j += "},\n";
  }
  j += "  \"files\": {";
  std::size_t n = 0;
  for (const auto &[name, hash] : files)
    j += std::string(n++ ? "," : "") + "\n    " + jsonString(name) + ": " + jsonString(hash);
  j += "\n  }\n}\n";
  writeFile(argv[2], std::vector<std::uint8_t>(j.begin(), j.end()));
  return 0;
}

int zip(int argc, char **argv) {
  std::vector<ZipMember> members;
  members.push_back({"manifest.json", readFile(argv[3])});
  for (int i = 4; i < argc; ++i) {
    const std::string a = argv[i];
    if (a.rfind("sig=", 0) == 0)
      members.push_back({"manifest.sig", readFile(a.substr(4))});
  }
  auto listed = memberArgs(argc, argv, 4);
  std::sort(listed.begin(), listed.end());
  for (const auto &[name, file] : listed)
    members.push_back({name, readFile(file)});
  writeFile(argv[2], vstengine::instrument::vins::writeZip(members));
  return 0;
}

int keyid(char **argv) {
  const auto data = readFile(argv[2]);
  std::vector<std::uint8_t> modulus;
  if (!vstengine::instrument::vins::parseModulusHex(std::string(data.begin(), data.end()), modulus))
    throw std::runtime_error("not an RSA modulus");
  std::cout << vstengine::instrument::vins::publisherKeyId(modulus) << " " << modulus.size() * 8
            << "\n";
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  try {
    const std::string command = argc > 1 ? argv[1] : "";
    if (command == "manifest" && argc >= 3)
      return manifest(argc, argv);
    if (command == "zip" && argc >= 4)
      return zip(argc, argv);
    if (command == "keyid" && argc == 3)
      return keyid(argv);
    std::cerr << "usage: vox_vins_pack manifest|zip|keyid ... (see tools/VinsPack.cpp)\n";
    return 2;
  } catch (const std::exception &e) {
    std::cerr << "vox_vins_pack: " << e.what() << "\n";
    return 1;
  }
}
