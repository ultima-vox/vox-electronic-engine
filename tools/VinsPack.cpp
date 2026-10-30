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
//
// Offline release signing (docs/release/RELEASE_SIGNING.md). The private key
// stays on the owner's offline machine; only these steps touch packages:
//   vox_vins_pack sign-request <unsigned.vins> <manifest.json out>
//     checks an unsigned native candidate and writes the exact manifest bytes
//     to sign (`openssl dgst -sha256 -sign release.key -out manifest.sig ...`)
//   vox_vins_pack attach <unsigned.vins> <manifest.sig> <modulus file> <out.vins>
//     verifies the signature with the public key before writing the signed
//     package; a wrong key or a tampered manifest writes nothing
//   vox_vins_pack verify <signed.vins> <modulus file>
#include "instrument/vins/VinsPackage.h"
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
namespace vins = vstengine::instrument::vins;

vins::PublisherKey publicKey(const std::string &modulusFile) {
  const auto data = readFile(modulusFile);
  vins::PublisherKey key;
  key.label = "release publisher (public key)";
  if (!vins::parseModulusHex(std::string(data.begin(), data.end()), key.modulus) ||
      key.modulus.size() * 8 < vins::minPublisherKeyBits)
    throw std::runtime_error("not an RSA-3072+ public modulus: " + modulusFile);
  key.id = vins::publisherKeyId(key.modulus);
  return key;
}

// A native package as built by the release-candidate configuration: a valid
// archive with a native manifest and no signature yet.
std::vector<ZipMember> unsignedCandidate(const std::string &path, vins::Manifest &manifest) {
  std::vector<ZipMember> members;
  std::string why;
  if (!vins::readZip(readFile(path), {}, members, why))
    throw std::runtime_error("not a valid .vins archive: " + why);
  if (members.empty() || members.front().name != "manifest.json")
    throw std::runtime_error("manifest.json must be the first member");
  for (const auto &m : members)
    if (m.name == "manifest.sig")
      throw std::runtime_error("the package is already signed");
  if (!vins::parseManifest(members.front().data, manifest, why))
    throw std::runtime_error("invalid manifest: " + why);
  if (manifest.kind != vins::ExecutionKind::native)
    throw std::runtime_error("only native packages are signed");
  return members;
}

std::string sha256(const std::vector<std::uint8_t> &data) {
  return juce::SHA256(data.data(), data.size()).toHexString().toStdString();
}

int signRequest(char **argv) {
  vins::Manifest manifest;
  const auto members = unsignedCandidate(argv[2], manifest);
  writeFile(argv[3], members.front().data);
  std::cout << "package " << manifest.packageId << " " << manifest.version << "\n"
            << "publisher key id " << manifest.publisher << "\n"
            << "manifest sha256 " << sha256(members.front().data) << "\n"
            << "sign it offline: openssl dgst -sha256 -sign <release key> -out manifest.sig "
            << argv[3] << "\n";
  return 0;
}

int attach(char **argv) {
  vins::Manifest manifest;
  auto members = unsignedCandidate(argv[2], manifest);
  const auto signature = readFile(argv[3]);
  const auto key = publicKey(argv[4]);
  if (key.id != manifest.publisher)
    throw std::runtime_error("the manifest names publisher " + manifest.publisher +
                             ", the public key is " + key.id);
  if (!vins::verifySignature(key, members.front().data, signature))
    throw std::runtime_error("the signature does not verify with this public key");
  members.insert(members.begin() + 1, ZipMember{"manifest.sig", signature});
  const auto signedArchive = vins::writeZip(members);
  writeFile(argv[5], signedArchive);
  std::cout << "signed " << manifest.packageId << " " << manifest.version << " sha256 "
            << sha256(signedArchive) << "\n";
  return 0;
}

int verify(char **argv) {
  std::vector<ZipMember> members;
  std::string why;
  if (!vins::readZip(readFile(argv[2]), {}, members, why))
    throw std::runtime_error("not a valid .vins archive: " + why);
  if (members.size() < 2 || members[0].name != "manifest.json" ||
      members[1].name != "manifest.sig")
    throw std::runtime_error("not a signed package");
  vins::Manifest manifest;
  if (!vins::parseManifest(members[0].data, manifest, why))
    throw std::runtime_error("invalid manifest: " + why);
  const auto key = publicKey(argv[3]);
  if (key.id != manifest.publisher || !vins::verifySignature(key, members[0].data, members[1].data))
    throw std::runtime_error("signature does not verify with this public key");
  std::cout << "verified " << manifest.packageId << " " << manifest.version << " publisher "
            << key.id << "\n";
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
    if (command == "sign-request" && argc == 4)
      return signRequest(argv);
    if (command == "attach" && argc == 6)
      return attach(argv);
    if (command == "verify" && argc == 4)
      return verify(argv);
    std::cerr << "usage: vox_vins_pack manifest|zip|keyid|sign-request|attach|verify ... "
                 "(see tools/VinsPack.cpp)\n";
    return 2;
  } catch (const std::exception &e) {
    std::cerr << "vox_vins_pack: " << e.what() << "\n";
    return 1;
  }
}
