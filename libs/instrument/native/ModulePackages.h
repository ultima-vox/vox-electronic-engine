#pragma once
// Versioned instrument package resolution (DEVELOPMENT/TEST ONLY, PROPOSED).
//
// Layout under an explicitly configured absolute root:
//   <root>/<packId>.voxinst/<MAJOR.MINOR.PATCH>/manifest.json
//                                              /module/<arch>/<file>
//                                              /ui/  /content/
//
// For each <packId>.voxinst folder the highest semantic version whose manifest
// validates and whose module file matches the manifest SHA-256 is selected.
// The SHA-256 is a corruption check only: it does not establish authenticity.
// There is no production scanning and no signature policy here; production
// discovery waits for the reviewed package and signing policy.
#include "instrument/native/NativeModule.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vstengine::instrument::native {

// Architecture folder name for this build (compile-time). nullptr means the
// platform is unsupported and no package resolves.
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__)) && !defined(_M_ARM64EC)
inline constexpr const char *packageArch = "x64";
#elif defined(__linux__) && defined(__x86_64__)
inline constexpr const char *packageArch = "linux-x64";
#else
inline constexpr const char *packageArch = nullptr;
#endif

inline constexpr int packageManifestVersion = 1;
inline constexpr const char *packageSuffix = ".voxinst";

struct PackageVersion {
  std::uint32_t major{}, minor{}, patch{};
  friend auto operator<=>(const PackageVersion &, const PackageVersion &) = default;
};

// Strict "MAJOR.MINOR.PATCH": decimal, no sign, no leading zeros (except "0").
[[nodiscard]] bool parsePackageVersion(std::string_view text, PackageVersion &out) noexcept;

struct ResolvedPackage {
  std::string packId;
  std::string packVersion;
  std::filesystem::path module; // absolute, hash-verified at resolution time
  std::vector<std::string> instrumentIds; // sorted, unique
};

struct PackageSelection {
  std::vector<std::filesystem::path> modules; // one per selected package
  std::vector<std::string> diagnostics;
  std::vector<ResolvedPackage> packages; // parallel to `modules`
};

// Controller thread only. `root` must be an absolute directory. Deterministic:
// packs are visited in packId order, versions from highest to lowest; versions
// above the selected one that fail validation are reported and skipped.
[[nodiscard]] PackageSelection resolvePackages(const std::filesystem::path &root);

// Controller thread only. Resolves, then loads each selected module and drops
// any provider whose instrument ids differ from the manifest's instrumentIds.
[[nodiscard]] LoadReport loadPackages(const std::filesystem::path &root);

} // namespace vstengine::instrument::native
