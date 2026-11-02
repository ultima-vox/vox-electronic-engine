#include "instrument/samples/SampleLoader.h"

namespace vstengine::instrument::samples {

SampleLoader::SampleLoader(rack::Rack &rack, Options options, FactoryResolver factory)
    : rack_(rack), options_(options), factory_(std::move(factory)),
      worker_([this] { run(); }) {}

SampleLoader::~SampleLoader() {
  {
    const std::scoped_lock lock(mutex_);
    stop_ = true;
    queue_.clear();
  }
  wake_.notify_all();
  if (worker_.joinable())
    worker_.join();
}

bool SampleLoader::enqueue(Request request, std::string &why) {
  {
    const std::scoped_lock lock(mutex_);
    if (stop_ || queue_.size() >= options_.queueCapacity) {
      ++stats_.refused;
      why = "sample loader queue is full";
      return false;
    }
    queue_.push_back(std::move(request));
  }
  wake_.notify_one();
  why.clear();
  return true;
}

std::size_t SampleLoader::enqueueSaved(std::string &why, std::optional<std::size_t> slot) {
  std::size_t queued = 0;
  for (auto &saved : rack_.sampleLoadRequests())
    if ((!slot || saved.slot == *slot) && enqueue({saved.slot, saved.generation, std::move(saved.ref)}, why))
      ++queued;
  return queued;
}

void SampleLoader::waitIdle() {
  std::unique_lock lock(mutex_);
  idle_.wait(lock, [this] { return stop_ || (queue_.empty() && !busy_); });
}

SampleLoader::Stats SampleLoader::stats() const {
  const std::scoped_lock lock(mutex_);
  return stats_;
}

void SampleLoader::run() {
  for (;;) {
    Request request;
    {
      std::unique_lock lock(mutex_);
      busy_ = false;
      idle_.notify_all();
      wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
      if (stop_)
        return;
      request = std::move(queue_.front());
      queue_.pop_front();
      busy_ = true;
    }
    load(request);
  }
}

void SampleLoader::load(const Request &request) {
  const auto fail = [&](std::string error) {
    rack_.setSampleError(request.slot, request.generation, std::move(error));
    const std::scoped_lock lock(mutex_);
    ++stats_.failed;
  };
  const auto &ref = request.ref;
  if (request.clear) {
    std::string why;
    if (!rack_.assignSlotSample(request.slot, request.generation, ref.sampleSlot, nullptr,
                                nullptr, why)) {
      fail(why);
      return;
    }
    const std::scoped_lock lock(mutex_);
    auto &use = use_[request.slot];
    if (use.generation == request.generation)
      use.bytes.erase(ref.sampleSlot);
    return;
  }
  std::filesystem::path file;
  std::string expected = ref.sha256;
  if (ref.source == SampleRef::Source::factory) {
    const auto found = factory_ ? factory_(request.slot, ref.location) : std::nullopt;
    if (!found) {
      fail("factory sample not found: " + ref.location);
      return;
    }
    // The package hash is authoritative; a saved reference must also agree.
    if (!expected.empty() && expected != found->sha256) {
      fail("factory sample changed: " + ref.location);
      return;
    }
    file = found->path;
    expected = found->sha256;
  } else {
    file = std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t *>(ref.location.c_str())));
    if (!file.is_absolute()) {
      fail("sample path is not absolute: " + ref.location);
      return;
    }
  }
  DecodedSample decoded;
  const auto error = decodeWavFile(file, expected, options_.limits, decoded);
  if (error != DecodeError::none) {
    fail(std::string(describe(error)) + ": " + ref.location);
    return;
  }
  if (ref.bytes != 0 && ref.bytes != decoded.fileBytes) {
    fail(std::string(describe(DecodeError::hashMismatch)) + ": " + ref.location);
    return;
  }
  {
    const std::scoped_lock lock(mutex_);
    auto &use = use_[request.slot];
    if (use.generation != request.generation) {
      use.generation = request.generation;
      use.bytes.clear();
    }
    std::uint64_t others = 0;
    for (const auto &[sampleSlot, bytes] : use.bytes)
      if (sampleSlot != ref.sampleSlot)
        others += bytes;
    if (others + decoded.memoryBytes() > options_.slotBudgetBytes) {
      ++stats_.failed;
      rack_.setSampleError(request.slot, request.generation,
                           "sample memory budget exceeded: " + ref.location);
      return;
    }
  }
  SampleRef saved = ref;
  saved.sha256 = decoded.sha256;
  saved.bytes = decoded.fileBytes;
  const auto memory = decoded.memoryBytes();
  std::string why;
  if (!rack_.assignSlotSample(request.slot, request.generation, ref.sampleSlot,
                              std::move(decoded.buffer), &saved, why)) {
    fail(why + ": " + ref.location);
    return;
  }
  const std::scoped_lock lock(mutex_);
  use_[request.slot].bytes[ref.sampleSlot] = memory;
  ++stats_.loaded;
}

} // namespace vstengine::instrument::samples
