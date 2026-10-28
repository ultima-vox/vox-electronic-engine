#pragma once
// Host side of the native instrument module ABI V2 (PROPOSED, NOT FROZEN).
// A native module is a shared library exporting voxModuleEntryV2. The host
// loads it on the controller thread from a validated absolute path, validates
// and deep-copies its descriptors into the existing InstrumentDescriptor
// model, and adapts its function table to InstrumentProvider/Instance. The
// Rack does not know whether an instrument is built in or native.
#include "instrument/InstrumentContract.h"
#include "instrument/abi/VoxModuleAbiV2.h"
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vstengine::instrument::native {

inline constexpr const char *providerId = "vox.native";

// Host limits applied while importing untrusted descriptors.
inline constexpr std::size_t maxDescriptorString = 256;
inline constexpr std::uint32_t maxInstrumentsPerModule = 64;
inline constexpr std::uint32_t maxParametersPerInstrument = 1024;
inline constexpr std::uint32_t maxRecordStride = 1024;
inline constexpr std::uint32_t maxChoices = 128;

[[nodiscard]] bool validModuleTable(const VoxModuleV2 *table) noexcept;

// Validates one V2 descriptor and converts it into the host contract. Nothing
// in `out` refers to module memory afterwards. Every parameter must be host
// exposed with a pool index inside this build's pool.
[[nodiscard]] bool importDescriptor(const VoxInstrumentDescriptorV2 *descriptor,
                                    InstrumentDescriptor &out, std::string &diagnostic);

// One loaded module. Process-wide: the same library is loaded and entered at
// most once per process and stays loaded until the process exits (no hot
// unload). Instances and providers hold a shared_ptr lease.
class NativeModule final {
public:
  // Controller/message thread only. `path` must be absolute.
  [[nodiscard]] static std::shared_ptr<NativeModule>
  acquire(const std::filesystem::path &path, std::string &diagnostic);

  [[nodiscard]] const VoxModuleV2 &table() const noexcept { return *table_; }
  [[nodiscard]] std::span<const InstrumentDescriptor> descriptors() const noexcept {
    return descriptors_;
  }
  [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }

  NativeModule(const NativeModule &) = delete;
  NativeModule &operator=(const NativeModule &) = delete;
  ~NativeModule();

private:
  NativeModule() = default;
  void *library_{};
  const VoxModuleV2 *table_{};
  std::filesystem::path path_;
  std::vector<InstrumentDescriptor> descriptors_;
};

// Adapts a loaded module to the Rack's provider contract.
[[nodiscard]] std::unique_ptr<InstrumentProvider>
makeProvider(std::shared_ptr<NativeModule> module);

// Process-wide count of blocks in which routed MIDI exceeded a native
// instance's prepared budget (the adapter then ends every voice of that
// instance at the block end). Readable from any thread.
[[nodiscard]] std::uint64_t totalMidiOverflows() noexcept;

// Process-wide count of failed native process/reset calls. A failed call
// latches that instance silent until the controller reprepares it; other
// instances are unaffected. Readable from any thread.
[[nodiscard]] std::uint64_t totalModuleFaults() noexcept;

// Development/test loading: absolute module paths separated by ';'. Each
// entry is loaded independently; failures are reported, never thrown.
struct LoadReport {
  std::vector<std::unique_ptr<InstrumentProvider>> providers;
  std::vector<std::string> diagnostics;
};
[[nodiscard]] LoadReport loadConfiguredModules(std::string_view pathList);

} // namespace vstengine::instrument::native
