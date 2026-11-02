#include "instrument/samples/SampleLoader.h"

#include <algorithm>

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
    const auto queuedByUsers = static_cast<std::size_t>(
        std::count_if(queue_.begin(), queue_.end(), [](const Request &r) { return !r.saved; }));
    if (stop_ || queuedByUsers >= options_.queueCapacity) {
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
  // Saved references are never refused: each restore replaces the saved
  // requests still queued for its slots (they name an older instance), so
  // they stay bounded by the Rack's own limits (maxSlots x maxSampleSlots)
  // next to at most queueCapacity user requests.
  auto requests = rack_.sampleLoadRequests();
  std::size_t queued = 0;
  {
    const std::scoped_lock lock(mutex_);
    if (stop_) {
      why = "sample loader stopped";
      return 0;
    }
    std::erase_if(queue_, [&](const Request &r) { return r.saved && (!slot || r.slot == *slot); });
    for (auto &saved : requests)
      if (!slot || saved.slot == *slot) {
        Request request{saved.slot, saved.generation, std::move(saved.ref)};
        request.saved = true;
        queue_.push_back(std::move(request));
        ++queued;
      }
  }
  wake_.notify_one();
  why.clear();
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
  SampleRef saved = ref;
  saved.sha256 = decoded.sha256;
  saved.bytes = decoded.fileBytes;
  // The budget covers every buffer the slot instrument's hand-off holds
  // (pending, current and retiring ones a voice may still read); a refused
  // publish leaves the previous sample and the saved reference as they were.
  std::string why;
  if (!rack_.assignSlotSample(request.slot, request.generation, ref.sampleSlot,
                              std::move(decoded.buffer), &saved, why,
                              options_.slotBudgetBytes)) {
    fail(why + ": " + ref.location);
    return;
  }
  const std::scoped_lock lock(mutex_);
  ++stats_.loaded;
}

} // namespace vstengine::instrument::samples
