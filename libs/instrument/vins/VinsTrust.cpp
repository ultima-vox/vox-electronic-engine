#include "instrument/vins/VinsTrust.h"

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

#include "VoxPublisherKeys.h" // generated: vstengine::instrument::vins::generated::keys

#include <algorithm>

namespace vstengine::instrument::vins {
namespace {
// DER DigestInfo prefix for SHA-256 (RFC 8017, section 9.2, note 1).
constexpr std::uint8_t sha256DigestInfo[] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60,
                                             0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02,
                                             0x01, 0x05, 0x00, 0x04, 0x20};

// juce::BigInteger memory blocks are little-endian.
juce::BigInteger fromBigEndian(std::span<const std::uint8_t> bytes) {
  juce::MemoryBlock block(bytes.size());
  auto *p = static_cast<std::uint8_t *>(block.getData());
  for (std::size_t i = 0; i < bytes.size(); ++i)
    p[i] = bytes[bytes.size() - 1 - i];
  juce::BigInteger value;
  value.loadFromMemoryBlock(block);
  return value;
}

std::vector<std::uint8_t> toBigEndian(const juce::BigInteger &value, std::size_t length) {
  const auto block = value.toMemoryBlock();
  std::vector<std::uint8_t> out(length, 0);
  const auto *p = static_cast<const std::uint8_t *>(block.getData());
  for (std::size_t i = 0; i < block.getSize() && i < length; ++i)
    out[length - 1 - i] = p[i];
  return out;
}

int hexDigit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}
} // namespace

bool parseModulusHex(const std::string &text, std::vector<std::uint8_t> &out) {
  std::string hex = text;
  if (const auto eq = hex.find('='); eq != std::string::npos)
    hex = hex.substr(eq + 1);
  hex.erase(std::remove_if(hex.begin(), hex.end(),
                           [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }),
            hex.end());
  if (hex.empty() || hex.size() % 2 != 0)
    return false;
  std::vector<std::uint8_t> bytes;
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    const int hi = hexDigit(hex[i]), lo = hexDigit(hex[i + 1]);
    if (hi < 0 || lo < 0)
      return false;
    bytes.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
  }
  const auto first = std::find_if(bytes.begin(), bytes.end(), [](std::uint8_t b) { return b != 0; });
  bytes.erase(bytes.begin(), first);
  if (bytes.empty())
    return false;
  out = std::move(bytes);
  return true;
}

std::string publisherKeyId(std::span<const std::uint8_t> modulus) {
  return juce::SHA256(modulus.data(), modulus.size()).toHexString().substring(0, 16).toStdString();
}

bool verifySignature(const PublisherKey &key, std::span<const std::uint8_t> message,
                     std::span<const std::uint8_t> signature) {
  const std::size_t k = key.modulus.size();
  if (k * 8 < minPublisherKeyBits || key.modulus.front() == 0 || key.exponent < 3 ||
      (key.exponent & 1u) == 0 || signature.size() != k)
    return false;
  const auto n = fromBigEndian(key.modulus);
  auto s = fromBigEndian(signature);
  if (s.compare(n) >= 0)
    return false;
  s.exponentModulo(juce::BigInteger(static_cast<juce::int64>(key.exponent)), n);
  const auto encoded = toBigEndian(s, k);

  // EM = 0x00 || 0x01 || PS (0xff...) || 0x00 || DigestInfo || SHA-256(message)
  const juce::SHA256 digest(message.data(), message.size());
  const auto hash = digest.getRawData();
  const std::size_t tLen = sizeof(sha256DigestInfo) + 32;
  if (k < tLen + 11)
    return false;
  std::vector<std::uint8_t> expected(k, 0xFF);
  expected[0] = 0x00;
  expected[1] = 0x01;
  expected[k - tLen - 1] = 0x00;
  std::copy(std::begin(sha256DigestInfo), std::end(sha256DigestInfo),
            expected.begin() + static_cast<std::ptrdiff_t>(k - tLen));
  std::copy(static_cast<const std::uint8_t *>(hash.getData()),
            static_cast<const std::uint8_t *>(hash.getData()) + 32,
            expected.begin() + static_cast<std::ptrdiff_t>(k - 32));
  std::uint8_t diff = 0;
  for (std::size_t i = 0; i < k; ++i)
    diff |= static_cast<std::uint8_t>(expected[i] ^ encoded[i]);
  return diff == 0;
}

const std::vector<PublisherKey> &pinnedPublisherKeys() {
  static const std::vector<PublisherKey> keys = [] {
    std::vector<PublisherKey> out;
    for (const auto &entry : generated::keys) {
      PublisherKey key;
      key.label = entry.label;
      if (!parseModulusHex(entry.modulusHex, key.modulus))
        continue;
      key.exponent = entry.exponent;
      key.id = publisherKeyId(key.modulus);
      out.push_back(std::move(key));
    }
    return out;
  }();
  return keys;
}

} // namespace vstengine::instrument::vins
