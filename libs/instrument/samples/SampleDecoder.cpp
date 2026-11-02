#include "instrument/samples/SampleDecoder.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <system_error>
#include <vector>

namespace vstengine::instrument::samples {

const char *describe(DecodeError error) noexcept {
  switch (error) {
  case DecodeError::none: return "ok";
  case DecodeError::unreadable: return "sample file cannot be read";
  case DecodeError::tooLarge: return "sample file is too large";
  case DecodeError::hashMismatch: return "sample file changed (SHA-256 mismatch)";
  case DecodeError::notWav: return "sample file is not a readable WAV";
  case DecodeError::badFormat: return "sample format out of range (rate, channels or length)";
  case DecodeError::tooLong: return "sample is longer than the allowed duration (30 s)";
  case DecodeError::empty: return "sample file holds no audio";
  case DecodeError::nonFinite: return "sample data holds NaN or infinity";
  }
  return "unknown sample error";
}

std::uint64_t DecodedSample::memoryBytes() const noexcept {
  if (!buffer || buffer->channels.empty())
    return 0;
  return static_cast<std::uint64_t>(buffer->channels.size()) *
         buffer->channels.front().size() * sizeof(float);
}

std::string sha256Hex(std::span<const std::uint8_t> bytes) {
  return juce::SHA256(bytes.data(), bytes.size()).toHexString().toStdString();
}

DecodeError decodeWav(std::span<const std::uint8_t> bytes, std::string_view expectedSha256,
                      const SampleLimits &limits, DecodedSample &out) {
  out = {};
  if (bytes.size() > limits.maxFileBytes)
    return DecodeError::tooLarge;
  auto hash = sha256Hex(bytes);
  if (!expectedSha256.empty() && hash != expectedSha256)
    return DecodeError::hashMismatch;
  juce::WavAudioFormat wav;
  auto stream = std::make_unique<juce::MemoryInputStream>(bytes.data(), bytes.size(), false);
  std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stream.release(), true));
  if (reader == nullptr)
    return DecodeError::notWav;
  const auto frames = reader->lengthInSamples;
  const auto channels = reader->numChannels;
  const auto rate = reader->sampleRate;
  if (!std::isfinite(rate) || rate < limits.minSampleRate || rate > limits.maxSampleRate ||
      channels == 0 || channels > limits.maxChannels || frames < 0)
    return DecodeError::badFormat;
  if (frames == 0)
    return DecodeError::empty;
  // Duration at the file's own rate, then the independent absolute frame
  // guard; both before any PCM is allocated and both reported as too long
  // (review 5742726611). Exactly maxSeconds is accepted.
  if (static_cast<double>(frames) > limits.maxSeconds * rate ||
      static_cast<std::uint64_t>(frames) > limits.maxFrames)
    return DecodeError::tooLong;
  juce::AudioBuffer<float> audio(static_cast<int>(channels), static_cast<int>(frames));
  if (!reader->read(&audio, 0, static_cast<int>(frames), 0, true, channels > 1))
    return DecodeError::notWav;
  auto buffer = std::make_unique<SampleBuffer>();
  buffer->sampleRate = rate;
  float peak = 0.0f;
  for (int c = 0; c < static_cast<int>(channels); ++c) {
    const auto *data = audio.getReadPointer(c);
    std::vector<float> channel(data, data + frames);
    for (const float v : channel) {
      if (!std::isfinite(v))
        return DecodeError::nonFinite;
      peak = std::max(peak, std::abs(v));
    }
    buffer->channels.push_back(std::move(channel));
  }
  buffer->peak = peak;
  out.buffer = std::move(buffer);
  out.sha256 = std::move(hash);
  out.fileBytes = bytes.size();
  return DecodeError::none;
}

DecodeError decodeWavFile(const std::filesystem::path &file, std::string_view expectedSha256,
                          const SampleLimits &limits, DecodedSample &out) {
  out = {};
  std::error_code ec;
  const auto status = std::filesystem::symlink_status(file, ec);
  if (ec || !std::filesystem::is_regular_file(status))
    return DecodeError::unreadable;
  const auto size = std::filesystem::file_size(file, ec);
  if (ec)
    return DecodeError::unreadable;
  if (size > limits.maxFileBytes)
    return DecodeError::tooLarge;
  std::ifstream in(file, std::ios::binary);
  if (!in)
    return DecodeError::unreadable;
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  if (size > 0 &&
      !in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size)))
    return DecodeError::unreadable;
  // The hash covers exactly these bytes, which are the ones decoded: a file
  // changed after the check cannot slip in.
  return decodeWav(bytes, expectedSha256, limits, out);
}

} // namespace vstengine::instrument::samples
