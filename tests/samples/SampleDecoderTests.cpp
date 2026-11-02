// Contract C2: WAV decoding for the sample hand-off. Every case builds its
// WAV in memory with JUCE's writer, so the expected samples are known.
#include "instrument/samples/SampleDecoder.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
using namespace vstengine::instrument::samples;
int checks = 0;

void check(bool ok, const std::string &message) {
  ++checks;
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

// A WAV with `channels` channels of `frames` frames; channel c, frame i holds
// value(c, i).
template <typename Value>
std::vector<std::uint8_t> makeWav(int channels, int frames, double rate, int bits, Value value) {
  juce::MemoryBlock block;
  {
    juce::WavAudioFormat wav;
    auto *stream = new juce::MemoryOutputStream(block, false);
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(
        stream, rate, static_cast<unsigned>(channels), bits, {}, 0));
    check(writer != nullptr, "test WAV writer created");
    juce::AudioBuffer<float> audio(channels, frames);
    for (int c = 0; c < channels; ++c)
      for (int i = 0; i < frames; ++i)
        audio.setSample(c, i, value(c, i));
    if (frames > 0)
      check(writer->writeFromAudioSampleBuffer(audio, 0, frames), "test WAV written");
  }
  const auto *data = static_cast<const std::uint8_t *>(block.getData());
  return {data, data + block.getSize()};
}

void decodesFormats() {
  const SampleLimits limits;
  for (const int bits : {16, 24, 32}) {
    const auto bytes = makeWav(2, 1000, 44100.0, bits, [](int c, int i) {
      return (c == 0 ? 0.5f : -0.25f) * static_cast<float>(i % 10) / 10.0f;
    });
    DecodedSample out;
    check(decodeWav(bytes, {}, limits, out) == DecodeError::none,
          std::to_string(bits) + "-bit stereo WAV decodes");
    check(out.buffer && out.buffer->channels.size() == 2 &&
              out.buffer->channels[0].size() == 1000 && out.buffer->sampleRate == 44100.0,
          "channels, frames and native rate kept (" + std::to_string(bits) + "-bit)");
    const float tolerance = bits == 16 ? 1e-4f : 1e-6f;
    check(std::abs(out.buffer->channels[0][9] - 0.45f) < tolerance &&
              std::abs(out.buffer->channels[1][9] + 0.225f) < tolerance,
          "sample values decoded (" + std::to_string(bits) + "-bit)");
    check(std::abs(out.buffer->peak - 0.45f) < tolerance, "peak is max |sample|");
    check(out.sha256 == sha256Hex(bytes) && out.sha256.size() == 64 &&
              out.fileBytes == bytes.size(),
          "SHA-256 of the exact bytes");
    check(out.memoryBytes() == 2u * 1000u * 4u, "decoded memory size");
  }
  const auto mono = makeWav(1, 10, 48000.0, 24, [](int, int i) { return i == 3 ? -0.8f : 0.1f; });
  DecodedSample out;
  check(decodeWav(mono, {}, limits, out) == DecodeError::none &&
            out.buffer->channels.size() == 1 && std::abs(out.buffer->peak - 0.8f) < 1e-6f,
        "mono decodes with its negative peak");
}

void rejects() {
  const SampleLimits limits;
  const auto good = makeWav(1, 100, 48000.0, 16, [](int, int) { return 0.1f; });
  DecodedSample out;
  const auto hash = sha256Hex(good);
  check(decodeWav(good, hash, limits, out) == DecodeError::none, "expected hash accepted");
  auto other = hash;
  other[0] = other[0] == '0' ? '1' : '0';
  check(decodeWav(good, other, limits, out) == DecodeError::hashMismatch && !out.buffer,
        "a different hash is refused before decoding");
  const auto three = makeWav(3, 100, 48000.0, 16, [](int, int) { return 0.1f; });
  check(decodeWav(three, {}, limits, out) == DecodeError::badFormat, "3 channels refused");
  SampleLimits shortLimits;
  shortLimits.maxFrames = 99;
  check(decodeWav(good, {}, shortLimits, out) == DecodeError::badFormat, "too long refused");
  SampleLimits smallFile;
  smallFile.maxFileBytes = good.size() - 1;
  check(decodeWav(good, {}, smallFile, out) == DecodeError::tooLarge, "too large refused");
  const auto empty = makeWav(1, 0, 48000.0, 16, [](int, int) { return 0.0f; });
  check(decodeWav(empty, {}, limits, out) == DecodeError::empty, "an empty WAV is refused");
  const auto slow = makeWav(1, 10, 4000.0, 16, [](int, int) { return 0.1f; });
  check(decodeWav(slow, {}, limits, out) == DecodeError::badFormat, "4 kHz refused");
  const std::vector<std::uint8_t> junk(512, 0x5A);
  check(decodeWav(junk, {}, limits, out) == DecodeError::notWav, "not a WAV refused");
  auto truncated = good;
  truncated.resize(30);
  check(decodeWav(truncated, {}, limits, out) != DecodeError::none, "a truncated WAV refused");
  const auto nan = makeWav(1, 10, 48000.0, 32, [](int, int i) {
    return i == 5 ? std::numeric_limits<float>::quiet_NaN() : 0.1f;
  });
  check(decodeWav(nan, {}, limits, out) == DecodeError::nonFinite && !out.buffer,
        "NaN in float data refused");
}

void files() {
  const auto dir = std::filesystem::temp_directory_path() /
                   ("vox-sample-decoder-" + std::to_string(std::rand()));
  std::filesystem::create_directories(dir);
  const auto wav = dir / "kick.wav";
  const auto bytes = makeWav(1, 64, 48000.0, 24, [](int, int i) { return 0.01f * i; });
  {
    std::ofstream f(wav, std::ios::binary);
    f.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  DecodedSample out;
  const SampleLimits limits;
  check(decodeWavFile(wav, sha256Hex(bytes), limits, out) == DecodeError::none &&
            out.buffer->channels[0].size() == 64,
        "a WAV file decodes with its expected hash");
  check(decodeWavFile(dir / "missing.wav", {}, limits, out) == DecodeError::unreadable,
        "a missing file is unreadable");
  check(decodeWavFile(dir, {}, limits, out) == DecodeError::unreadable,
        "a directory is unreadable");
  std::error_code ec;
  std::filesystem::create_symlink(wav, dir / "link.wav", ec);
  if (!ec)
    check(decodeWavFile(dir / "link.wav", {}, limits, out) == DecodeError::unreadable,
          "a symlink is refused");
  SampleLimits small;
  small.maxFileBytes = 16;
  check(decodeWavFile(wav, {}, small, out) == DecodeError::tooLarge,
        "a file above the byte limit is refused before reading");
  std::filesystem::remove_all(dir, ec);
}
} // namespace

int main() {
  decodesFormats();
  rejects();
  files();
  std::cout << "sample_decoder_tests: " << checks << " checks passed\n";
  return 0;
}
