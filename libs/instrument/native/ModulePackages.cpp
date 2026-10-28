#include "instrument/native/ModulePackages.h"
#include "instrument/abi/VoxModuleAbiV2.h"

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <charconv>
#include <limits>
#include <system_error>
#include <utility>

namespace vstengine::instrument::native {
namespace fs = std::filesystem;

namespace {
constexpr std::uintmax_t maxManifestBytes = 64 * 1024;
constexpr std::size_t maxIdLength = 128;
constexpr std::size_t maxModulePathLength = 256;

std::string utf8(const fs::path &p) {
  const auto u = p.u8string();
  return {reinterpret_cast<const char *>(u.data()), u.size()};
}

fs::path pathFromUtf8(const std::string &s) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}

juce::File juceFile(const fs::path &p) {
  const auto s = utf8(p);
  return juce::File(juce::String::fromUTF8(s.data(), static_cast<int>(s.size())));
}

bool validId(std::string_view id) noexcept {
  if (id.empty() || id.size() > maxIdLength)
    return false;
  return std::all_of(id.begin(), id.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
           c == '-';
  });
}

bool lowerHex64(std::string_view s) noexcept {
  return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

// Relative path below module/<arch>/: forward slashes only, no root, no drive,
// no empty/'.'/'..' segments.
bool safeRelativeModulePath(std::string_view s) noexcept {
  if (s.empty() || s.size() > maxModulePathLength || s.front() == '/')
    return false;
  for (const char c : s)
    if (c == '\\' || c == ':' || c == '\0' || static_cast<unsigned char>(c) < 0x20)
      return false;
  std::size_t start = 0;
  while (start <= s.size()) {
    const auto end = std::min(s.find('/', start), s.size());
    const auto segment = s.substr(start, end - start);
    if (segment.empty() || segment == "." || segment == "..")
      return false;
    start = end + 1;
  }
  return true;
}

bool isWithin(const fs::path &child, const fs::path &parent) {
  auto c = child.begin();
  for (auto p = parent.begin(); p != parent.end(); ++p, ++c)
    if (c == child.end() || *c != *p)
      return false;
  return c != child.end();
}

bool readInt(const juce::var &v, long long &out) {
  if (!(v.isInt() || v.isInt64()) || v.isBool())
    return false;
  out = static_cast<juce::int64>(v);
  return true;
}

bool readString(const juce::var &v, std::string &out) {
  if (!v.isString())
    return false;
  out = v.toString().toStdString();
  return true;
}

struct Candidate {
  PackageVersion version;
  std::string name;
  fs::path dir;
};

// Validates one <packId>.voxinst/<version> folder. Returns false with a reason.
bool validateVersion(const std::string &packId, const Candidate &candidate,
                     ResolvedPackage &out, std::string &why) {
  std::error_code ec;
  const auto manifestPath = candidate.dir / "manifest.json";
  if (!fs::is_regular_file(manifestPath, ec)) {
    why = "manifest.json missing";
    return false;
  }
  const auto size = fs::file_size(manifestPath, ec);
  if (ec || size == 0 || size > maxManifestBytes) {
    why = "manifest.json empty, unreadable or larger than 64 KiB";
    return false;
  }
  juce::var json;
  const auto parsed = juce::JSON::parse(juceFile(manifestPath).loadFileAsString(), json);
  auto *object = json.getDynamicObject();
  if (parsed.failed() || object == nullptr) {
    why = "manifest.json is not a JSON object";
    return false;
  }
  const auto field = [object](const char *name) { return object->getProperty(name); };

  long long manifestVersion = 0;
  if (!readInt(field("manifestVersion"), manifestVersion) ||
      manifestVersion != packageManifestVersion) {
    why = "unsupported manifestVersion (expected 1)";
    return false;
  }
  std::string id, version, arch, module, sha;
  if (!readString(field("packId"), id) || id != packId) {
    why = "manifest packId does not match folder '" + packId + packageSuffix + "'";
    return false;
  }
  if (!readString(field("packVersion"), version) || version != candidate.name) {
    why = "manifest packVersion does not match folder '" + candidate.name + "'";
    return false;
  }
  const auto abi = field("abi");
  long long abiMin = 0, abiMax = 0;
  if (abi.getDynamicObject() == nullptr || !readInt(abi["min"], abiMin) ||
      !readInt(abi["max"], abiMax) || abiMin > abiMax) {
    why = "abi must be {\"min\": int, \"max\": int} with min <= max";
    return false;
  }
  if (abiMin > static_cast<long long>(VOX_MODULE_ABI_V2) ||
      abiMax < static_cast<long long>(VOX_MODULE_ABI_V2)) {
    why = "abi range [" + std::to_string(abiMin) + ", " + std::to_string(abiMax) +
          "] excludes host ABI " + std::to_string(VOX_MODULE_ABI_V2);
    return false;
  }
  if (!readString(field("arch"), arch) || arch != packageArch) {
    why = "arch '" + arch + "' does not match host arch '" + packageArch + "'";
    return false;
  }
  if (!readString(field("module"), module) || !safeRelativeModulePath(module)) {
    why = "module must be a relative path below module/" + std::string(packageArch) +
          "/ without '..', '.', '\\', ':' or a leading '/'";
    return false;
  }
  if (!readString(field("sha256"), sha) || !lowerHex64(sha)) {
    why = "sha256 must be 64 lowercase hex digits";
    return false;
  }
  const auto ids = field("instrumentIds");
  const auto *idArray = ids.getArray();
  if (idArray == nullptr || idArray->isEmpty() ||
      idArray->size() > static_cast<int>(maxInstrumentsPerModule)) {
    why = "instrumentIds must be a non-empty array of at most " +
          std::to_string(maxInstrumentsPerModule) + " ids";
    return false;
  }
  std::vector<std::string> instrumentIds;
  for (const auto &entry : *idArray) {
    std::string value;
    if (!readString(entry, value) || !validId(value)) {
      why = "instrumentIds contains an invalid id";
      return false;
    }
    instrumentIds.push_back(std::move(value));
  }
  std::sort(instrumentIds.begin(), instrumentIds.end());
  if (std::adjacent_find(instrumentIds.begin(), instrumentIds.end()) != instrumentIds.end()) {
    why = "instrumentIds contains duplicates";
    return false;
  }

  const auto archDir = candidate.dir / "module" / packageArch;
  const auto modulePath = archDir / pathFromUtf8(module);
  if (!fs::is_regular_file(modulePath, ec)) {
    why = "module file missing: " + utf8(modulePath);
    return false;
  }
  // A symlink inside the package must not lead outside module/<arch>/.
  const auto realArch = fs::canonical(archDir, ec);
  const auto realModule = ec ? fs::path{} : fs::canonical(modulePath, ec);
  if (ec || !isWithin(realModule, realArch)) {
    why = "module resolves outside module/" + std::string(packageArch) + "/";
    return false;
  }
  // Corruption check only; not an authenticity/signature check.
  const auto actual = juce::SHA256(juceFile(realModule)).toHexString().toStdString();
  if (actual != sha) {
    why = "module sha256 mismatch (manifest " + sha + ", file " + actual + ")";
    return false;
  }
  out.packId = packId;
  out.packVersion = candidate.name;
  out.module = realModule;
  out.instrumentIds = std::move(instrumentIds);
  return true;
}

std::vector<fs::path> sortedSubdirectories(const fs::path &dir) {
  std::vector<fs::path> result;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    if (it->is_directory(ec))
      result.push_back(it->path());
  std::sort(result.begin(), result.end());
  return result;
}
} // namespace

bool parsePackageVersion(std::string_view text, PackageVersion &out) noexcept {
  std::uint32_t parts[3]{};
  for (int i = 0; i < 3; ++i) {
    const auto end = i < 2 ? text.find('.') : text.size();
    if (end == std::string_view::npos || end == 0)
      return false;
    const auto token = text.substr(0, end);
    if (token.size() > 1 && token.front() == '0')
      return false;
    if (!std::all_of(token.begin(), token.end(), [](char c) { return c >= '0' && c <= '9'; }))
      return false;
    const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), parts[i]);
    if (ec != std::errc{} || ptr != token.data() + token.size())
      return false;
    text.remove_prefix(i < 2 ? end + 1 : end);
  }
  out = {parts[0], parts[1], parts[2]};
  return true;
}

PackageSelection resolvePackages(const fs::path &root) {
  PackageSelection selection;
  if (packageArch == nullptr) {
    selection.diagnostics.push_back("instrument packages: unsupported host architecture");
    return selection;
  }
  std::error_code ec;
  if (!root.is_absolute() || !fs::is_directory(root, ec)) {
    selection.diagnostics.push_back("instrument packages: root must be an absolute directory: " +
                                    utf8(root));
    return selection;
  }
  const std::string suffix = packageSuffix;
  for (const auto &packDir : sortedSubdirectories(root)) {
    const auto folder = utf8(packDir.filename());
    if (folder.size() <= suffix.size() ||
        folder.compare(folder.size() - suffix.size(), suffix.size(), suffix) != 0)
      continue; // not a package folder
    const auto packId = folder.substr(0, folder.size() - suffix.size());
    if (!validId(packId)) {
      selection.diagnostics.push_back("package '" + folder + "': invalid packId");
      continue;
    }
    std::vector<Candidate> candidates;
    for (const auto &versionDir : sortedSubdirectories(packDir)) {
      Candidate candidate;
      candidate.name = utf8(versionDir.filename());
      candidate.dir = versionDir;
      if (!parsePackageVersion(candidate.name, candidate.version)) {
        selection.diagnostics.push_back("package " + packId + "/" + candidate.name +
                                        ": folder is not MAJOR.MINOR.PATCH");
        continue;
      }
      candidates.push_back(std::move(candidate));
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) { return a.version > b.version; });
    bool selected = false;
    for (const auto &candidate : candidates) {
      ResolvedPackage package;
      std::string why;
      if (validateVersion(packId, candidate, package, why)) {
        selection.modules.push_back(package.module);
        selection.packages.push_back(std::move(package));
        selected = true;
        break; // lower versions are not considered
      }
      selection.diagnostics.push_back("package " + packId + "/" + candidate.name +
                                      " skipped: " + why);
    }
    if (!selected)
      selection.diagnostics.push_back("package " + packId + ": no valid version");
  }
  return selection;
}

LoadReport loadPackages(const fs::path &root) {
  auto selection = resolvePackages(root);
  LoadReport report;
  report.diagnostics = std::move(selection.diagnostics);
  for (auto &package : selection.packages) {
    const auto label = "package " + package.packId + "/" + package.packVersion;
    std::string diagnostic;
    auto module = NativeModule::acquire(package.module, diagnostic);
    if (!module) {
      report.diagnostics.push_back(label + " failed to load: " + diagnostic);
      continue;
    }
    std::vector<std::string> actual;
    for (const auto &descriptor : module->descriptors())
      actual.push_back(descriptor.id);
    std::sort(actual.begin(), actual.end());
    if (actual != package.instrumentIds) {
      report.diagnostics.push_back(label +
                                   ": module instrument ids differ from manifest instrumentIds");
      continue; // module stays loaded process-wide (no hot unload) but is not offered
    }
    report.providers.push_back(makeProvider(std::move(module)));
  }
  return report;
}

} // namespace vstengine::instrument::native
