#pragma once
// .vins instrument packages (#73): the one package format of the Rack.
//
// A .vins is one immutable ZIP archive file (see VinsZip.h) installed next to
// the Rack VST3:
//   <product>/Vox Electronic Engine.vst3
//   <product>/instruments/<any name>.vins
// It holds manifest.json (schema in docs/architecture/VINS_PACKAGES.md), the
// member files it lists with their SHA-256, and for native packages
// manifest.sig, a publisher signature over the exact manifest bytes.
// - executionKind "graph": a DSP patch graph (no executable code) compiled
//   by the shared DSP Core.
// - executionKind "native": a V2 module for this platform, loaded only when
//   the manifest is signed by a publisher key pinned into this build. The
//   module is copied from the verified archive into a per-user,
//   content-addressed cache and loaded from there (never from the ZIP).
// Scanning, verification, extraction and compilation happen on the
// controller thread only; nothing here is touched by the audio callback.
#include "instrument/native/NativeModule.h"
#include "instrument/vins/VinsTrust.h"
#include "instrument/vins/VinsZip.h"

#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vstengine::instrument::vins {

inline constexpr int manifestSchema = 1;
inline constexpr const char *archiveSuffix = ".vins";
inline constexpr const char *manifestMember = "manifest.json";
inline constexpr const char *signatureMember = "manifest.sig";
inline constexpr std::size_t maxManifestBytes = 64 * 1024;
inline constexpr std::size_t maxArchivesPerFolder = 64;
inline constexpr std::size_t maxFolderEntries = 1024;

// Native module platform key for this build; nullptr: no native packages.
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__)) && !defined(_M_ARM64EC)
inline constexpr const char *nativePlatform = "win-x64";
#elif defined(__linux__) && defined(__x86_64__)
inline constexpr const char *nativePlatform = "linux-x64";
#else
inline constexpr const char *nativePlatform = nullptr;
#endif

enum class ExecutionKind { graph, native };

struct Manifest {
  std::string packageId, version, name, vendor;
  ExecutionKind kind{ExecutionKind::graph};
  std::vector<std::string> instruments;     // sorted, unique
  std::string graph;                        // graph: member holding the patch
  int nativeAbi{};                          // native: VOX_MODULE_ABI_V2
  std::map<std::string, std::string> modules; // native: platform -> member
  std::string publisher;                    // native: publisher key id
  std::map<std::string, std::string> files; // member -> lowercase SHA-256 hex
};

// Strict schema check of manifest.json bytes. False with a reason.
[[nodiscard]] bool parseManifest(std::span<const std::uint8_t> bytes, Manifest &out,
                                 std::string &why);

struct LoadOptions {
  std::filesystem::path cacheRoot;              // absolute; native extraction
  std::span<const PublisherKey> trusted;        // pinned publisher keys
  ZipLimits limits;
};

// Controller thread. Loads the .vins files of one instruments folder:
// non-recursive, bounded, regular files only (no symlinks/junctions), in name
// order. Packages that share a packageId or an instrument id are all
// rejected. Every rejection is a diagnostic; nothing throws.
[[nodiscard]] native::LoadReport loadFolder(const std::filesystem::path &folder,
                                            const LoadOptions &options);

// Path of the binary that contains this code (the Rack VST3 inside a DAW).
[[nodiscard]] std::filesystem::path currentModulePath();

// <product>/instruments for a Rack binary at
// <product>/<Name>.vst3/Contents/<arch>/<Name>.vst3 (bundle) or
// <product>/<Name>.vst3 (single file); nullopt otherwise. Pure computation.
[[nodiscard]] std::optional<std::filesystem::path>
instrumentsFolderForPlugin(const std::filesystem::path &pluginBinary);

// Per-user cache: %LOCALAPPDATA%\Vox Music\Cache on Windows,
// $XDG_CACHE_HOME/vox-music (or ~/.cache/vox-music) elsewhere.
[[nodiscard]] std::filesystem::path defaultCacheRoot();

// The Rack's only discovery path: the instruments folder next to the running
// VST3, the default cache and the pinned publisher keys. Outside a VST3
// bundle (e.g. a test executable) it finds nothing.
[[nodiscard]] native::LoadReport loadInstalled();

} // namespace vstengine::instrument::vins
