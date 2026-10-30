#include "instrument/vins/VinsPackage.h"

#include "dspcore/GraphInstrument.h"
#include "instrument/abi/VoxModuleAbiV2.h"

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <system_error>

namespace vstengine::instrument::vins {
namespace fs = std::filesystem;
using native::LoadReport;

namespace {
constexpr std::size_t maxIdLength = 128;
constexpr std::size_t maxTextLength = 128;

std::string utf8(const fs::path &p) {
  const auto u = p.u8string();
  return {reinterpret_cast<const char *>(u.data()), u.size()};
}

fs::path fromUtf8(const std::string &s) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}

bool validId(const std::string &id) {
  return !id.empty() && id.size() <= maxIdLength &&
         std::all_of(id.begin(), id.end(), [](char c) {
           return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                  c == '-';
         });
}

bool validVersion(const std::string &v) {
  int parts = 0;
  std::size_t i = 0;
  while (i < v.size()) {
    const std::size_t start = i;
    while (i < v.size() && v[i] >= '0' && v[i] <= '9')
      ++i;
    const std::size_t digits = i - start;
    if (digits == 0 || digits > 9 || (digits > 1 && v[start] == '0'))
      return false;
    ++parts;
    if (i == v.size())
      break;
    if (v[i] != '.' || parts == 3)
      return false;
    ++i;
    if (i == v.size())
      return false;
  }
  return parts == 3;
}

bool validText(const std::string &s) {
  return !s.empty() && s.size() <= maxTextLength &&
         std::none_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

bool lowerHex(const std::string &s, std::size_t length) {
  return s.size() == length && std::all_of(s.begin(), s.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

std::string sha256Hex(std::span<const std::uint8_t> bytes) {
  return juce::SHA256(bytes.data(), bytes.size()).toHexString().toStdString();
}

bool readString(const juce::var &v, std::string &out) {
  if (!v.isString())
    return false;
  out = v.toString().toStdString();
  return true;
}

bool fail(std::string &why, std::string reason) {
  why = std::move(reason);
  return false;
}

// True when `p` itself is a symlink, junction or other reparse point.
bool isLinkOrReparsePoint(const fs::path &p) {
#if defined(_WIN32)
  const auto attributes = ::GetFileAttributesW(p.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  std::error_code ec;
  return fs::is_symlink(fs::symlink_status(p, ec));
#endif
}

bool readFileBounded(const fs::path &path, std::size_t limit, std::vector<std::uint8_t> &out,
                     std::string &why) {
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  if (ec)
    return fail(why, "unreadable");
  if (size > limit)
    return fail(why, "larger than " + std::to_string(limit) + " bytes");
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return fail(why, "cannot be opened");
  out.resize(static_cast<std::size_t>(size));
  in.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(out.size()));
  if (static_cast<std::size_t>(in.gcount()) != out.size() || in.peek() != std::char_traits<char>::eof())
    return fail(why, "changed while being read");
  return true;
}

// A verified archive, not yet turned into a provider.
struct Candidate {
  fs::path archive;
  std::string label;
  Manifest manifest;
  std::string manifestHash;
  std::vector<ZipMember> members; // without manifest.json / manifest.sig
  std::vector<std::uint8_t> manifestBytes, signature;
};

const ZipMember *findMember(const Candidate &c, const std::string &name) {
  for (const auto &m : c.members)
    if (m.name == name)
      return &m;
  return nullptr;
}

bool verifyArchive(const fs::path &archive, const LoadOptions &options, Candidate &out,
                   std::string &why) {
  std::vector<std::uint8_t> bytes;
  if (!readFileBounded(archive, options.limits.maxArchiveBytes, bytes, why))
    return false;
  std::vector<ZipMember> members;
  if (!readZip(bytes, options.limits, members, why))
    return false;
  Candidate c;
  c.archive = archive;
  bool haveManifest = false;
  for (auto &m : members) {
    if (m.name == manifestMember) {
      if (m.data.size() > maxManifestBytes)
        return fail(why, "manifest.json larger than 64 KiB");
      c.manifestBytes = std::move(m.data);
      haveManifest = true;
    } else if (m.name == signatureMember) {
      c.signature = std::move(m.data);
    } else {
      c.members.push_back(std::move(m));
    }
  }
  if (!haveManifest)
    return fail(why, "manifest.json missing");
  if (!parseManifest(c.manifestBytes, c.manifest, why))
    return false;
  c.manifestHash = sha256Hex(c.manifestBytes);
  // Exactly the listed members, each with its listed SHA-256.
  for (const auto &m : c.members) {
    const auto listed = c.manifest.files.find(m.name);
    if (listed == c.manifest.files.end())
      return fail(why, "member '" + m.name + "' is not listed in the manifest");
    if (sha256Hex(m.data) != listed->second)
      return fail(why, "member '" + m.name + "' fails its SHA-256");
  }
  if (c.members.size() != c.manifest.files.size())
    return fail(why, "a member listed in the manifest is missing from the archive");
  out = std::move(c);
  return true;
}

// Every listed file present in `dir` with its hash, nothing else, no links.
bool cacheMatches(const fs::path &dir, const Candidate &c) {
  std::error_code ec;
  if (isLinkOrReparsePoint(dir) || !fs::is_directory(dir, ec))
    return false;
  std::size_t files = 0;
  for (fs::recursive_directory_iterator it(dir, fs::directory_options::none, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (isLinkOrReparsePoint(it->path()))
      return false;
    if (it->is_directory(ec))
      continue;
    if (!it->is_regular_file(ec))
      return false;
    const auto relative = fs::relative(it->path(), dir, ec).generic_u8string();
    const std::string name(relative.begin(), relative.end());
    const auto *member = findMember(c, name);
    std::vector<std::uint8_t> bytes;
    std::string why;
    if (member == nullptr || !readFileBounded(it->path(), member->data.size(), bytes, why) ||
        bytes != member->data)
      return false;
    ++files;
  }
  return !ec && files == c.members.size();
}

bool writeFile(const fs::path &path, const std::vector<std::uint8_t> &data) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
  out.close();
  return static_cast<bool>(out);
}

// Copies the verified members into cacheRoot/<packageId>/<version>-<hash16>/
// with a staged rename; an existing directory is reused only if it matches.
bool materialize(const Candidate &c, const fs::path &cacheRoot, fs::path &dir, std::string &why) {
  std::error_code ec;
  if (cacheRoot.empty() || !cacheRoot.is_absolute())
    return fail(why, "no usable instrument cache folder");
  const auto packageDir = cacheRoot / fromUtf8(c.manifest.packageId);
  dir = packageDir / fromUtf8(c.manifest.version + "-" + c.manifestHash.substr(0, 16));
  if (fs::exists(fs::symlink_status(dir, ec))) {
    if (cacheMatches(dir, c))
      return true;
    fs::remove_all(dir, ec);
    if (ec)
      return fail(why, "corrupt cache entry could not be replaced: " + utf8(dir));
  }
  fs::create_directories(packageDir, ec);
  if (ec || isLinkOrReparsePoint(packageDir))
    return fail(why, "cannot create the instrument cache folder " + utf8(packageDir));
  const auto staging =
      packageDir / fromUtf8(".staging-" + juce::Uuid().toString().toStdString());
  fs::create_directory(staging, ec);
  if (ec)
    return fail(why, "cannot create a cache staging folder");
  bool written = true;
  for (const auto &m : c.members) {
    const auto target = staging / fromUtf8(m.name);
    fs::create_directories(target.parent_path(), ec);
    if (ec || !writeFile(target, m.data)) {
      written = false;
      break;
    }
  }
  if (!written || !cacheMatches(staging, c)) {
    fs::remove_all(staging, ec);
    return fail(why, "writing the instrument cache failed");
  }
  fs::rename(staging, dir, ec);
  if (ec) { // another Rack instance may have committed the same content
    std::error_code ignore;
    fs::remove_all(staging, ignore);
    if (!cacheMatches(dir, c))
      return fail(why, "committing the instrument cache failed");
  }
  return true;
}

// The package's provider with its sound layouts attached to the descriptors.
class LayoutProvider final : public InstrumentProvider {
public:
  LayoutProvider(std::unique_ptr<InstrumentProvider> inner,
                 std::vector<InstrumentDescriptor> descriptors)
      : inner_(std::move(inner)), descriptors_(std::move(descriptors)) {}
  std::span<const InstrumentDescriptor> descriptors() const noexcept override {
    return descriptors_;
  }
  std::unique_ptr<InstrumentInstance> create(std::string_view id,
                                             const CreateContext &context) override {
    return inner_->create(id, context);
  }
  std::span<const ContentDescriptor> contentDescriptors(std::string_view id) const noexcept override {
    return inner_->contentDescriptors(id);
  }
  ContentStatus applySoundPreset(std::string_view id, std::string_view preset,
                                 InstrumentInstance &instance) const noexcept override {
    return inner_->applySoundPreset(id, preset, instance);
  }
  ContentStatus generatePattern(std::string_view id, std::string_view profile,
                                const VoxGenerationContextV1 &context, const VoxPatternV1 *seed,
                                VoxPatternV1 &out) const noexcept override {
    return inner_->generatePattern(id, profile, context, seed, out);
  }

private:
  std::unique_ptr<InstrumentProvider> inner_;
  std::vector<InstrumentDescriptor> descriptors_;
};

// Offers the provider with the manifest's sound layouts. A layout that does
// not describe its instrument exactly rejects the package (fail closed).
void offer(const Candidate &c, std::unique_ptr<InstrumentProvider> provider, LoadReport &report) {
  if (c.manifest.ui.empty()) {
    report.providers.push_back(std::move(provider));
    return;
  }
  auto descriptors = std::vector<InstrumentDescriptor>(provider->descriptors().begin(),
                                                       provider->descriptors().end());
  for (const auto &[id, memberName] : c.manifest.ui) {
    const auto *member = findMember(c, memberName);
    auto descriptor = std::find_if(descriptors.begin(), descriptors.end(),
                                   [&](const InstrumentDescriptor &d) { return d.id == id; });
    SoundLayout layout;
    std::string why;
    if (member == nullptr || descriptor == descriptors.end())
      why = "layout member or instrument missing";
    else if (parseSoundLayout(member->data, layout, why) &&
             validateSoundLayout(layout, *descriptor, why))
      descriptor->soundLayout = std::make_shared<const SoundLayout>(std::move(layout));
    if (descriptor == descriptors.end() || descriptor->soundLayout == nullptr) {
      report.diagnostics.push_back(c.label + ": ui of " + id + " rejected: " + why);
      return;
    }
  }
  report.providers.push_back(
      std::make_unique<LayoutProvider>(std::move(provider), std::move(descriptors)));
}

void loadGraph(Candidate &c, LoadReport &report) {
  const auto *member = findMember(c, c.manifest.graph);
  std::string why;
  auto patch = member == nullptr
                   ? nullptr
                   : dspcore::loadPatch(std::string(member->data.begin(), member->data.end()), why);
  if (patch == nullptr) {
    report.diagnostics.push_back(c.label + ": graph rejected: " + why);
    return;
  }
  if (c.manifest.instruments.size() != 1 || patch->descriptor.id != c.manifest.instruments[0]) {
    report.diagnostics.push_back(c.label + ": graph instrument id differs from the manifest");
    return;
  }
  offer(c, dspcore::makeGraphProvider(std::move(patch)), report);
}

void loadNative(Candidate &c, const LoadOptions &options, LoadReport &report) {
  const PublisherKey *key = nullptr;
  for (const auto &k : options.trusted)
    if (k.id == c.manifest.publisher)
      key = &k;
  if (key == nullptr) {
    report.diagnostics.push_back(c.label + ": publisher " + c.manifest.publisher +
                                 " is not trusted by this Rack build; native code not loaded");
    return;
  }
  if (c.signature.empty() || !verifySignature(*key, c.manifestBytes, c.signature)) {
    report.diagnostics.push_back(c.label + ": manifest signature invalid; native code not loaded");
    return;
  }
  if (nativePlatform == nullptr || c.manifest.modules.count(nativePlatform) == 0) {
    report.diagnostics.push_back(c.label + ": no native module for this platform");
    return;
  }
  fs::path dir;
  std::string why;
  if (!materialize(c, options.cacheRoot, dir, why)) {
    report.diagnostics.push_back(c.label + ": " + why);
    return;
  }
  auto module =
      native::NativeModule::acquire(dir / fromUtf8(c.manifest.modules.at(nativePlatform)), why);
  if (!module) {
    report.diagnostics.push_back(c.label + ": module failed to load: " + why);
    return;
  }
  std::vector<std::string> ids;
  for (const auto &d : module->descriptors())
    ids.push_back(d.id);
  std::sort(ids.begin(), ids.end());
  if (ids != c.manifest.instruments) {
    // The module stays loaded process-wide (no hot unload) but is not offered.
    report.diagnostics.push_back(c.label + ": module instrument ids differ from the manifest");
    return;
  }
  offer(c, native::makeProvider(std::move(module), key->id), report);
}
} // namespace

bool parseManifest(std::span<const std::uint8_t> bytes, Manifest &out, std::string &why) {
  if (bytes.empty() || bytes.size() > maxManifestBytes)
    return fail(why, "manifest.json empty or larger than 64 KiB");
  juce::var json;
  const auto parsed = juce::JSON::parse(
      juce::String::fromUTF8(reinterpret_cast<const char *>(bytes.data()),
                             static_cast<int>(bytes.size())),
      json);
  auto *object = json.getDynamicObject();
  if (parsed.failed() || object == nullptr)
    return fail(why, "manifest.json is not a JSON object");
  static const std::set<juce::String> knownKeys = {
      "vins", "packageId", "version", "name", "vendor", "executionKind", "instruments",
      "graph", "native", "files", "ui"};
  for (const auto &property : object->getProperties())
    if (knownKeys.count(property.name.toString()) == 0)
      return fail(why, "manifest: unknown key '" + property.name.toString().toStdString() + "'");
  const auto schema = json["vins"];
  if (!(schema.isInt() || schema.isInt64()) || static_cast<juce::int64>(schema) != manifestSchema)
    return fail(why, "manifest: unsupported vins schema (expected 1)");
  Manifest m;
  std::string kind;
  if (!readString(json["packageId"], m.packageId) || !validId(m.packageId))
    return fail(why, "manifest: invalid packageId");
  if (!readString(json["version"], m.version) || !validVersion(m.version))
    return fail(why, "manifest: version must be MAJOR.MINOR.PATCH");
  if (!readString(json["name"], m.name) || !validText(m.name) ||
      !readString(json["vendor"], m.vendor) || !validText(m.vendor))
    return fail(why, "manifest: name and vendor are required (1..128 characters)");
  if (!readString(json["executionKind"], kind) || (kind != "graph" && kind != "native"))
    return fail(why, "manifest: executionKind must be \"graph\" or \"native\"");
  m.kind = kind == "graph" ? ExecutionKind::graph : ExecutionKind::native;
  const auto *instruments = json["instruments"].getArray();
  if (instruments == nullptr || instruments->isEmpty() || instruments->size() > 64)
    return fail(why, "manifest: instruments must list 1..64 ids");
  for (const auto &id : *instruments) {
    std::string s;
    if (!readString(id, s) || !validId(s))
      return fail(why, "manifest: invalid instrument id");
    m.instruments.push_back(s);
  }
  std::sort(m.instruments.begin(), m.instruments.end());
  if (std::adjacent_find(m.instruments.begin(), m.instruments.end()) != m.instruments.end())
    return fail(why, "manifest: duplicate instrument id");
  auto *files = json["files"].getDynamicObject();
  if (files == nullptr)
    return fail(why, "manifest: files object is required");
  for (const auto &property : files->getProperties()) {
    const auto name = property.name.toString().toStdString();
    std::string hash;
    if (!validMemberName(name, ZipLimits{}.maxNameBytes) || name == manifestMember ||
        name == signatureMember || !readString(property.value, hash) || !lowerHex(hash, 64))
      return fail(why, "manifest: invalid files entry '" + name + "'");
    m.files[name] = hash;
  }
  if (m.kind == ExecutionKind::graph) {
    if (!json["native"].isVoid())
      return fail(why, "manifest: a graph package has no native section");
    if (!readString(json["graph"], m.graph) || m.files.count(m.graph) == 0)
      return fail(why, "manifest: graph must name a listed member");
  } else {
    if (!json["graph"].isVoid())
      return fail(why, "manifest: a native package has no graph member");
    const auto section = json["native"];
    auto *nativeObject = section.getDynamicObject();
    const auto abi = section["abi"];
    auto *modules = section["modules"].getDynamicObject();
    if (nativeObject == nullptr || !(abi.isInt() || abi.isInt64()) ||
        static_cast<juce::int64>(abi) != VOX_MODULE_ABI_V2 || modules == nullptr ||
        !readString(section["publisher"], m.publisher) || !lowerHex(m.publisher, 16))
      return fail(why, "manifest: native needs abi 2, modules and a publisher key id");
    for (const auto &property : nativeObject->getProperties())
      if (property.name.toString() != "abi" && property.name.toString() != "modules" &&
          property.name.toString() != "publisher")
        return fail(why, "manifest: unknown native key '" +
                             property.name.toString().toStdString() + "'");
    m.nativeAbi = VOX_MODULE_ABI_V2;
    for (const auto &property : modules->getProperties()) {
      const auto platform = property.name.toString().toStdString();
      std::string member;
      if ((platform != "win-x64" && platform != "linux-x64") ||
          !readString(property.value, member) || m.files.count(member) == 0)
        return fail(why, "manifest: invalid native module entry '" + platform + "'");
      m.modules[platform] = member;
    }
    if (m.modules.empty())
      return fail(why, "manifest: native needs at least one module");
  }
  if (!json["ui"].isVoid()) {
    auto *ui = json["ui"].getDynamicObject();
    if (ui == nullptr || ui->getProperties().isEmpty())
      return fail(why, "manifest: ui must map instrument ids to layout members");
    for (const auto &property : ui->getProperties()) {
      const auto id = property.name.toString().toStdString();
      std::string member;
      if (!std::binary_search(m.instruments.begin(), m.instruments.end(), id) ||
          !readString(property.value, member) || m.files.count(member) == 0)
        return fail(why, "manifest: invalid ui entry '" + id + "'");
      m.ui[id] = member;
    }
  }
  out = std::move(m);
  return true;
}

bool parseSoundLayout(std::span<const std::uint8_t> bytes, SoundLayout &out, std::string &why) {
  if (bytes.empty() || bytes.size() > maxSoundLayoutBytes)
    return fail(why, "sound layout empty or larger than 64 KiB");
  juce::var json;
  const auto parsed = juce::JSON::parse(
      juce::String::fromUTF8(reinterpret_cast<const char *>(bytes.data()),
                             static_cast<int>(bytes.size())),
      json);
  const auto onlyKeys = [](const juce::var &v, std::initializer_list<const char *> keys) {
    auto *object = v.getDynamicObject();
    if (object == nullptr)
      return false;
    for (const auto &property : object->getProperties())
      if (std::none_of(keys.begin(), keys.end(), [&](const char *k) {
            return property.name.toString() == k;
          }))
        return false;
    return true;
  };
  if (parsed.failed() || !onlyKeys(json, {"voxSoundLayout", "sections"}))
    return fail(why, "sound layout: not a JSON object with voxSoundLayout and sections only");
  const auto schema = json["voxSoundLayout"];
  if (!(schema.isInt() || schema.isInt64()) ||
      static_cast<juce::int64>(schema) != soundLayoutSchema)
    return fail(why, "sound layout: unsupported voxSoundLayout schema (expected 1)");
  const auto *sections = json["sections"].getArray();
  if (sections == nullptr || sections->isEmpty() ||
      static_cast<std::size_t>(sections->size()) > maxSoundSections)
    return fail(why, "sound layout: sections must list 1..16 sections");
  SoundLayout layout;
  for (const auto &s : *sections) {
    SoundSection section;
    const auto *controls = s["controls"].getArray();
    if (!onlyKeys(s, {"title", "controls"}) || !readString(s["title"], section.title) ||
        controls == nullptr ||
        static_cast<std::size_t>(controls->size()) > poolParametersPerSlot)
      return fail(why, "sound layout: a section needs a title and a controls list");
    for (const auto &c : *controls) {
      SoundControl control;
      std::string kind;
      if (!onlyKeys(c, {"parameter", "kind", "label"}) ||
          !readString(c["parameter"], control.parameter) || !readString(c["kind"], kind) ||
          (!c["label"].isVoid() && (!readString(c["label"], control.label) ||
                                    control.label.empty())))
        return fail(why, "sound layout: a control needs parameter, kind and an optional label");
      if (kind == "knob")
        control.kind = SoundControlKind::knob;
      else if (kind == "selector")
        control.kind = SoundControlKind::selector;
      else if (kind == "toggle")
        control.kind = SoundControlKind::toggle;
      else
        return fail(why, "sound layout: kind must be knob, selector or toggle");
      section.controls.push_back(std::move(control));
    }
    layout.sections.push_back(std::move(section));
  }
  out = std::move(layout);
  return true;
}

LoadReport loadFolder(const fs::path &folder, const LoadOptions &options) {
  LoadReport report;
  std::error_code ec;
  if (!folder.is_absolute() || !fs::is_directory(folder, ec))
    return report; // nothing installed
  if (isLinkOrReparsePoint(folder)) {
    report.diagnostics.push_back("instruments folder is a symlink or junction; ignored: " +
                                 utf8(folder));
    return report;
  }
  std::vector<fs::path> archives;
  std::size_t visited = 0;
  for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    if (++visited > maxFolderEntries) {
      report.diagnostics.push_back("instruments folder has more than " +
                                   std::to_string(maxFolderEntries) + " entries; scan stopped");
      break;
    }
    const auto name = utf8(it->path().filename());
    if (name.size() <= 5 || !juce::String::fromUTF8(name.c_str()).endsWithIgnoreCase(archiveSuffix))
      continue;
    if (isLinkOrReparsePoint(it->path()) || !it->is_regular_file(ec)) {
      report.diagnostics.push_back(name + ": not a regular file; ignored");
      continue;
    }
    archives.push_back(it->path());
  }
  std::sort(archives.begin(), archives.end());
  if (archives.size() > maxArchivesPerFolder) {
    report.diagnostics.push_back("more than " + std::to_string(maxArchivesPerFolder) +
                                 " .vins files; the rest are ignored");
    archives.resize(maxArchivesPerFolder);
  }

  std::vector<Candidate> candidates;
  for (const auto &archive : archives) {
    Candidate c;
    std::string why;
    const auto label = utf8(archive.filename());
    if (!verifyArchive(archive, options, c, why)) {
      report.diagnostics.push_back(label + ": rejected: " + why);
      continue;
    }
    c.label = label + " (" + c.manifest.packageId + " " + c.manifest.version + ")";
    candidates.push_back(std::move(c));
  }
  // Conflicting identities reject every package involved.
  std::map<std::string, int> packageCount, instrumentCount;
  for (const auto &c : candidates) {
    ++packageCount[c.manifest.packageId];
    for (const auto &id : c.manifest.instruments)
      ++instrumentCount[id];
  }
  for (auto &c : candidates) {
    bool conflict = packageCount[c.manifest.packageId] > 1;
    for (const auto &id : c.manifest.instruments)
      conflict = conflict || instrumentCount[id] > 1;
    if (conflict) {
      report.diagnostics.push_back(c.label +
                                   ": rejected: another package uses the same package or "
                                   "instrument id");
      continue;
    }
    if (c.manifest.kind == ExecutionKind::graph)
      loadGraph(c, report);
    else
      loadNative(c, options, report);
  }
  return report;
}

fs::path currentModulePath() {
#if defined(_WIN32)
  HMODULE self = nullptr;
  if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&currentModulePath), &self))
    return {};
  std::wstring buffer(32768, L'\0');
  const auto length = ::GetModuleFileNameW(self, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size())
    return {};
  buffer.resize(length);
  return fs::path(buffer);
#else
  Dl_info info{};
  if (::dladdr(reinterpret_cast<void *>(&currentModulePath), &info) == 0 ||
      info.dli_fname == nullptr)
    return {};
  return fs::path(info.dli_fname);
#endif
}

std::optional<fs::path> instrumentsFolderForPlugin(const fs::path &pluginBinary) {
  if (!pluginBinary.is_absolute())
    return std::nullopt;
  const auto binary = pluginBinary.lexically_normal();
  const auto ends = [](const fs::path &p, const char *suffix) {
    return juce::String::fromUTF8(utf8(p.filename()).c_str()).endsWithIgnoreCase(suffix);
  };
  const auto arch = binary.parent_path();
  const auto contents = arch.parent_path();
  const auto bundle = contents.parent_path();
  if (utf8(contents.filename()) == "Contents" && ends(bundle, ".vst3") && bundle.has_parent_path() &&
      bundle.parent_path() != bundle)
    return bundle.parent_path() / "instruments";
  if (ends(binary, ".vst3") && binary.has_parent_path())
    return binary.parent_path() / "instruments";
  return std::nullopt;
}

fs::path defaultCacheRoot() {
#if defined(_WIN32)
  const auto local = juce::File::getSpecialLocation(juce::File::windowsLocalAppData);
  if (local.getFullPathName().isEmpty())
    return {};
  return fromUtf8(local.getFullPathName().toStdString()) / "Vox Music" / "Cache";
#else
  if (const char *xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && xdg[0] == '/')
    return fs::path(xdg) / "vox-music";
  if (const char *home = std::getenv("HOME"); home != nullptr && home[0] == '/')
    return fs::path(home) / ".cache" / "vox-music";
  return {};
#endif
}

LoadReport loadInstalled() {
  const auto folder = instrumentsFolderForPlugin(currentModulePath());
  if (!folder)
    return {};
  LoadOptions options;
  options.cacheRoot = defaultCacheRoot();
  options.trusted = pinnedPublisherKeys();
  return loadFolder(*folder, options);
}

} // namespace vstengine::instrument::vins
