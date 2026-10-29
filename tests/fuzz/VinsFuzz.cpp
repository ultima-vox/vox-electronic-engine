// Deterministic mutation fuzzer for the .vins parsers that see untrusted
// bytes: readZip, parseManifest, verifySignature and the graph compiler.
// Seeds are the real packages from the build. Every mutated input must be
// handled without a crash, and an accepted archive must satisfy the reader's
// own invariants. CTest runs a short pass; build with -fsanitize=address,
// undefined and pass a large iteration count for a longer campaign.
//   vins_fuzz <test-vins dir> <instruments dir> <iterations> <seed>
#include "dspcore/GraphInstrument.h"
#include "instrument/vins/VinsPackage.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
namespace vins = vstengine::instrument::vins;
using Bytes = std::vector<std::uint8_t>;

Bytes readFile(const fs::path &p) {
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}

[[noreturn]] void fail(const std::string &what, std::uint64_t iteration) {
  std::cerr << "FAIL at iteration " << iteration << ": " << what << '\n';
  std::exit(1);
}

Bytes mutate(Bytes b, std::mt19937_64 &rng) {
  const auto pick = [&](std::size_t n) { return n == 0 ? 0 : static_cast<std::size_t>(rng() % n); };
  static const std::uint8_t interesting[] = {0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF, 0x50, 0x4B};
  const int edits = 1 + static_cast<int>(rng() % 8);
  for (int e = 0; e < edits && !b.empty(); ++e) {
    switch (rng() % 7) {
    case 0: b[pick(b.size())] ^= static_cast<std::uint8_t>(1u << (rng() % 8)); break;
    case 1: b[pick(b.size())] = interesting[rng() % sizeof(interesting)]; break;
    case 2: { // 16/32-bit little-endian field to an extreme
      const auto at = pick(b.size());
      const std::uint32_t v = (rng() % 2) ? 0xFFFFFFFFu : static_cast<std::uint32_t>(rng());
      for (std::size_t k = 0; k < 4 && at + k < b.size(); ++k)
        b[at + k] = static_cast<std::uint8_t>(v >> (8 * k));
      break;
    }
    case 3: b.resize(pick(b.size() + 1)); break;
    case 4: { // duplicate a range
      const auto from = pick(b.size()), len = pick(std::min<std::size_t>(64, b.size() - from) + 1);
      b.insert(b.begin() + static_cast<std::ptrdiff_t>(pick(b.size())), b.begin() + static_cast<std::ptrdiff_t>(from),
               b.begin() + static_cast<std::ptrdiff_t>(from + len));
      break;
    }
    case 5: b.insert(b.begin() + static_cast<std::ptrdiff_t>(pick(b.size() + 1)), pick(16) + 1,
                     static_cast<std::uint8_t>(rng()));
      break;
    default: { // erase a range
      const auto from = pick(b.size());
      b.erase(b.begin() + static_cast<std::ptrdiff_t>(from),
              b.begin() + static_cast<std::ptrdiff_t>(std::min(b.size(), from + pick(32) + 1)));
      break;
    }
    }
  }
  return b;
}

void checkAccepted(const std::vector<vins::ZipMember> &members, const vins::ZipLimits &limits,
                   std::uint64_t i) {
  std::set<std::string> folded;
  std::size_t total = 0;
  for (const auto &m : members) {
    if (!vins::validMemberName(m.name, limits.maxNameBytes))
      fail("accepted an invalid member name: " + m.name, i);
    std::string lower = m.name;
    for (auto &c : lower)
      if (c >= 'A' && c <= 'Z')
        c = static_cast<char>(c - 'A' + 'a');
    if (!folded.insert(lower).second)
      fail("accepted a duplicate member: " + m.name, i);
    if (m.data.size() > limits.maxEntryBytes)
      fail("accepted an oversized member", i);
    total += m.data.size();
  }
  if (members.size() > limits.maxEntries || total > limits.maxTotalBytes)
    fail("accepted an archive over its limits", i);
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 5) {
    std::cerr << "usage: vins_fuzz <test-vins dir> <instruments dir> <iterations> <seed>\n";
    return 2;
  }
  std::vector<Bytes> seeds;
  for (const fs::path dir : {fs::path(argv[1]), fs::path(argv[2])})
    for (const auto &e : fs::directory_iterator(dir))
      if (e.path().extension() == ".vins")
        seeds.push_back(readFile(e.path()));
  if (seeds.empty()) {
    std::cerr << "no seed packages\n";
    return 2;
  }
  // Also seed with the members themselves (manifests, graphs, signatures).
  std::vector<Bytes> manifests, graphs, signatures;
  for (const auto &s : seeds) {
    std::vector<vins::ZipMember> m;
    std::string why;
    if (!vins::readZip(s, {}, m, why)) {
      std::cerr << "seed does not parse: " << why << '\n';
      return 1;
    }
    for (auto &x : m) {
      if (x.name == "manifest.json")
        manifests.push_back(x.data);
      else if (x.name == "manifest.sig")
        signatures.push_back(x.data);
      else if (x.name.rfind("graph/", 0) == 0)
        graphs.push_back(x.data);
    }
  }
  const auto iterations = std::strtoull(argv[3], nullptr, 10);
  std::mt19937_64 rng(std::strtoull(argv[4], nullptr, 10));
  const vins::ZipLimits normal{};
  const vins::ZipLimits tight = [] {
    vins::ZipLimits l;
    l.maxEntries = 4;
    l.maxEntryBytes = 4096;
    l.maxTotalBytes = 8192;
    return l;
  }();
  std::uint64_t zipAccepted = 0, manifestAccepted = 0, graphAccepted = 0;
  const auto &keys = vins::pinnedPublisherKeys();
  for (std::uint64_t i = 0; i < iterations; ++i) {
    const auto archive = mutate(seeds[rng() % seeds.size()], rng);
    for (const vins::ZipLimits *limits : {&normal, &tight}) {
      std::vector<vins::ZipMember> out;
      std::string why;
      if (vins::readZip(archive, *limits, out, why)) {
        checkAccepted(out, *limits, i);
        ++zipAccepted;
      } else if (!out.empty() || why.empty()) {
        fail("a rejected archive left members or no reason", i);
      }
    }
    vins::Manifest manifest;
    std::string why;
    const auto m = mutate(manifests[rng() % manifests.size()], rng);
    if (vins::parseManifest(m, manifest, why))
      ++manifestAccepted;
    if (!keys.empty() && !signatures.empty()) {
      const auto sig = mutate(signatures[rng() % signatures.size()], rng);
      if (vins::verifySignature(keys[0], manifests[0], sig) && sig != signatures[0])
        fail("a mutated signature verified", i);
    }
    if (!graphs.empty() && i % 4 == 0) {
      const auto g = mutate(graphs[rng() % graphs.size()], rng);
      if (vstengine::dspcore::loadPatch(std::string(g.begin(), g.end()), why) != nullptr)
        ++graphAccepted;
    }
  }
  std::cout << "vins_fuzz: " << iterations << " iterations, " << zipAccepted
            << " archives accepted, " << manifestAccepted << " manifests accepted, "
            << graphAccepted << " graphs accepted, no crash or invariant violation\n";
  return 0;
}
