#pragma once
// Contract C2: host decoding of WAV samples, off the audio thread.
//
// The decoder reads the whole file (bounded), hashes the exact bytes it then
// decodes (SHA-256), and produces an immutable float SampleBuffer at the
// file's native rate with its peak. Nothing here is ever called from the
// audio callback.
#include "instrument/InstrumentContract.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace vstengine::instrument::samples {

// PROPOSED limits (contract C: "to be measured and approved").
struct SampleLimits {
  std::uint64_t maxFileBytes{64ull * 1024 * 1024}; // one WAV file
  std::uint64_t maxFrames{30ull * 192000};         // 30 s at 192 kHz
  std::uint32_t maxChannels{2};
  double minSampleRate{8000.0};
  double maxSampleRate{384000.0};
};

enum class DecodeError {
  none,
  unreadable,      // missing, not a regular file, I/O error
  tooLarge,        // file above maxFileBytes
  hashMismatch,    // bytes differ from the expected SHA-256
  notWav,          // not a WAV the reader accepts
  badFormat,       // sample rate / channel count / length out of range
  empty,           // no frames
  nonFinite,       // NaN or infinity in the data
};

[[nodiscard]] const char *describe(DecodeError) noexcept;

struct DecodedSample {
  std::unique_ptr<SampleBuffer> buffer;
  std::string sha256;         // lowercase hex of the decoded bytes
  std::uint64_t fileBytes{};  // size of those bytes
  // Decoded size in memory (frames x channels x 4 bytes).
  [[nodiscard]] std::uint64_t memoryBytes() const noexcept;
};

[[nodiscard]] std::string sha256Hex(std::span<const std::uint8_t> bytes);

// Decodes WAV bytes. `expectedSha256` (lowercase hex) is checked first when it
// is not empty.
[[nodiscard]] DecodeError decodeWav(std::span<const std::uint8_t> bytes,
                                    std::string_view expectedSha256, const SampleLimits &,
                                    DecodedSample &out);

// Reads a regular file (no symlink) of at most maxFileBytes, then decodeWav.
[[nodiscard]] DecodeError decodeWavFile(const std::filesystem::path &file,
                                        std::string_view expectedSha256,
                                        const SampleLimits &, DecodedSample &out);

} // namespace vstengine::instrument::samples
