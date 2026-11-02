// Contract C hand-off budget under controller/audio concurrency (owner
// review 5740523896): the live-memory cap must hold even when the audio
// thread installs a pending buffer while a publish decides.
#include "instrument/native/SampleExchange.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <thread>

namespace {
using vstengine::instrument::SampleBuffer;
using vstengine::instrument::native::SampleExchange;
int checks = 0;

void check(bool ok, const std::string &message) {
  ++checks;
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

std::unique_ptr<SampleBuffer> buffer(std::size_t frames) {
  auto b = std::make_unique<SampleBuffer>();
  b->channels.push_back(std::vector<float>(frames, 0.1f));
  b->sampleRate = 48000.0;
  b->peak = 0.1f;
  return b;
}

constexpr std::uint64_t bytes(std::size_t frames) { return frames * sizeof(float); }

// One audio block with no voice holding anything.
void block(SampleExchange &ex) {
  ex.beginBlock();
  ex.endBlock(true);
}

// The interleaving of the review: audio runs a block right after publish(B)
// took the pending word. The pending A is then the publisher's: audio cannot
// install it, B supersedes it, and A + B are never both live.
void auditedInterleaving() {
  SampleExchange ex(1);
  std::string why;
  check(ex.publish(0, buffer(1000), why), "A pending");
  std::uint64_t installedDuringPublish = 1;
  ex.setAfterTakeHookForTesting([&] {
    block(ex); // the audio block lands between take and decision
    installedDuringPublish = ex.table()[0].id;
  });
  const auto limit = bytes(1000) + bytes(1000) - 1; // A + B do not fit together
  check(ex.publish(0, buffer(1000), why, limit), "B accepted: " + why);
  ex.setAfterTakeHookForTesting({});
  const auto stats = ex.stats();
  check(installedDuringPublish == 0, "the block inside the publish installs nothing");
  check(stats.released == 1 && stats.live == 1 && stats.liveBytes == bytes(1000) &&
            stats.liveBytes <= limit,
        "A is superseded and released; live bytes stay within the cap");
  block(ex);
  check(ex.table()[0].id != 0, "the next block installs B");
}

// Audio installed A before the publish: A is current, so it counts, and a
// B that does not fit beside it is refused without any change.
void installedBufferCounts() {
  SampleExchange ex(1);
  std::string why;
  check(ex.publish(0, buffer(1000), why), "A pending");
  block(ex);
  const auto a = ex.table()[0].id;
  const auto limit = bytes(1000) + bytes(1000) - 1;
  check(!ex.publish(0, buffer(1000), why, limit) && why.find("budget") != std::string::npos,
        "B is refused while A is current");
  block(ex);
  check(ex.table()[0].id == a && ex.stats().published == 1 && ex.stats().liveBytes == bytes(1000),
        "the refusal changed nothing");
}

// A refusal puts the taken pending entry back: it still installs next.
void refusalRestoresPending() {
  SampleExchange ex(1);
  std::string why;
  check(ex.publish(0, buffer(500), why), "X");
  block(ex); // X current
  check(ex.publish(0, buffer(300), why), "A pending");
  const auto limit = bytes(500) + bytes(1000) - 1; // X + B do not fit
  check(!ex.publish(0, buffer(1000), why, limit), "B refused");
  block(ex);
  check(ex.table()[0].id == 2 && ex.table()[0].frames == 300,
        "the pending A survives the refusal and installs");
  check(ex.stats().liveBytes <= bytes(500) + bytes(300), "nothing extra is live");
}

// Stress (run under ThreadSanitizer in the TSan build): an audio thread runs
// blocks while the controller publishes buffers of random sizes under a cap
// and collects. The cap holds after every publish.
void concurrentCap() {
  SampleExchange ex(2);
  std::atomic<bool> stop{false};
  std::thread audio([&] {
    while (!stop.load(std::memory_order_acquire))
      block(ex);
  });
  std::mt19937 random(7);
  const std::uint64_t limit = bytes(6000);
  bool bounded = true;
  int accepted = 0;
  for (int i = 0; i < 20000; ++i) {
    std::string why;
    const auto frames = static_cast<std::size_t>(500 + random() % 3000);
    accepted += ex.publish(static_cast<std::uint32_t>(i % 2), buffer(frames), why, limit) ? 1 : 0;
    bounded &= ex.stats().liveBytes <= limit;
    if (i % 3 == 0)
      ex.collect();
  }
  stop.store(true, std::memory_order_release);
  audio.join();
  check(bounded, "live bytes never exceed the cap under concurrency");
  check(accepted > 1000, "most publishes are accepted (" + std::to_string(accepted) + ")");
}
} // namespace

int main() {
  auditedInterleaving();
  installedBufferCounts();
  refusalRestoresPending();
  concurrentCap();
  std::cout << "sample_exchange_tests: " << checks << " checks passed\n";
  return 0;
}
