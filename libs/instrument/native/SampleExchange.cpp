#include "instrument/native/SampleExchange.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace vstengine::instrument::native {

namespace {
VoxSampleV2 emptyView() noexcept {
  VoxSampleV2 view{};
  view.structSize = sizeof(VoxSampleV2);
  view.version = VOX_MODULE_ABI_V2;
  return view;
}
} // namespace

SampleExchange::SampleExchange(std::uint32_t slots)
    : slots_(slots), capacity_(std::max<std::uint32_t>(1, slots * usagePerSlot)),
      pending_(std::make_unique<std::atomic<Entry *>[]>(slots)), current_(slots, nullptr),
      table_(slots, emptyView()), usageIds_(capacity_, 0) {
  for (std::uint32_t s = 0; s < slots_; ++s)
    pending_[s].store(nullptr, std::memory_order_relaxed);
  for (auto &report : reports_) {
    report.current.assign(slots_, 0);
    report.ids.assign(capacity_, 0);
  }
  usage_.structSize = sizeof(VoxSampleUsageV2);
  usage_.version = VOX_MODULE_ABI_V2;
  usage_.capacity = capacity_;
  usage_.ids = usageIds_.data();
  live_.reserve(static_cast<std::size_t>(slots_) * maxLivePerSlot + 1);
  scratch_.reserve(capacity_);
}

SampleExchange::~SampleExchange() {
  // Teardown without another block: the instance is gone, so nothing can
  // read any buffer. Take every pending word, then release all, once each.
  const std::scoped_lock lock(mutex_);
  for (std::uint32_t s = 0; s < slots_; ++s)
    (void)pending_[s].exchange(nullptr, std::memory_order_acq_rel);
  while (!live_.empty())
    releaseLocked(live_.back().get());
}

std::size_t SampleExchange::liveInSlot(std::uint32_t slot) const noexcept {
  return static_cast<std::size_t>(std::count_if(
      live_.begin(), live_.end(), [&](const auto &entry) { return entry->slot == slot; }));
}

std::uint64_t SampleExchange::bytesOf(const SampleBuffer *buffer) noexcept {
  std::uint64_t bytes = 0;
  if (buffer != nullptr)
    for (const auto &channel : buffer->channels)
      bytes += channel.size() * sizeof(float);
  return bytes;
}

std::uint64_t SampleExchange::liveBytes(const Entry *except) const noexcept {
  std::uint64_t bytes = 0;
  for (const auto &entry : live_)
    if (entry.get() != except)
      bytes += bytesOf(entry->buffer.get());
  return bytes;
}

bool SampleExchange::publish(std::uint32_t slot, std::unique_ptr<SampleBuffer> buffer,
                             std::string &diagnostic, std::uint64_t memoryLimitBytes) {
  const std::scoped_lock lock(mutex_);
  if (slot >= slots_) {
    diagnostic = "sample slot out of range";
    return false;
  }
  if (buffer) {
    const auto &channels = buffer->channels;
    const auto frames = channels.empty() ? 0 : channels.front().size();
    if (channels.empty() || channels.size() > 2 || frames == 0 ||
        std::any_of(channels.begin(), channels.end(),
                    [&](const auto &c) { return c.size() != frames; }) ||
        !std::isfinite(buffer->sampleRate) || buffer->sampleRate <= 0.0 ||
        !std::isfinite(buffer->peak) || buffer->peak < 0.0f) {
      diagnostic = "invalid sample buffer";
      return false;
    }
  }
  if (liveInSlot(slot) >= maxLivePerSlot) {
    (void)collectLocked();
    if (liveInSlot(slot) >= maxLivePerSlot) {
      diagnostic = "sample slot busy: earlier buffers are still in use";
      return false;
    }
  }
  // Take this slot's pending entry out of the pending word first. One
  // exchange decides who owns it: if it is still there, the audio thread can
  // no longer install it and this publish supersedes it (never installed, so
  // it does not count against the budget); if audio took it already, it is
  // current and counts like every other live buffer. No window remains in
  // which audio installs a buffer this check left out (owner review
  // 5740523896).
  Entry *previous = pending_[slot].exchange(nullptr, std::memory_order_acq_rel);
  if (afterTakeForTesting_)
    afterTakeForTesting_();
  if (memoryLimitBytes != 0) {
    // Collect first, then decide. A refusal changes nothing: the taken entry
    // goes back to the pending word (only this mutex holder writes non-null
    // entries there, and audio only takes them).
    const auto incoming = bytesOf(buffer.get());
    const auto over = [&] { return liveBytes(previous) + incoming > memoryLimitBytes; };
    if (over()) {
      (void)collectLocked();
      if (over()) {
        if (previous != nullptr)
          pending_[slot].store(previous, std::memory_order_release);
        diagnostic = "sample memory budget exceeded: earlier buffers are still in use";
        return false;
      }
    }
  }
  auto entry = std::make_unique<Entry>();
  entry->id = ++nextId_;
  entry->slot = slot;
  entry->view = emptyView();
  if (buffer) {
    buffer->channelTable.clear();
    for (const auto &channel : buffer->channels)
      buffer->channelTable.push_back(channel.data());
    entry->view.id = entry->id;
    entry->view.channels = buffer->channelTable.data();
    entry->view.channelCount = static_cast<std::uint32_t>(buffer->channelTable.size());
    entry->view.frames = buffer->channels.front().size();
    entry->view.sampleRate = buffer->sampleRate;
    entry->view.peak = buffer->peak;
  }
  entry->buffer = std::move(buffer);
  auto *raw = entry.get();
  live_.push_back(std::move(entry));
  ++stats_.published;
  // The entry is complete before this release store, so the audio thread's
  // acquiring exchange sees all of it. The word is empty: only this mutex
  // holder writes entries into it.
  pending_[slot].store(raw, std::memory_order_release);
  if (previous != nullptr)
    releaseLocked(previous); // superseded: audio never took it, no voice saw it
  diagnostic.clear();
  return true;
}

std::size_t SampleExchange::collect() {
  const std::scoped_lock lock(mutex_);
  return collectLocked();
}

bool SampleExchange::acceptable(const Report &report) {
  if (!report.valid || report.overflow || report.count > capacity_)
    return false;
  // Every id non-zero, unique and one this instance holds and has not
  // released; anything else is a malformed report that frees nothing.
  auto &ids = scratch_;
  ids.assign(report.ids.begin(), report.ids.begin() + report.count);
  std::sort(ids.begin(), ids.end());
  for (std::size_t i = 0; i < ids.size(); ++i) {
    if (ids[i] == 0 || (i > 0 && ids[i] == ids[i - 1]))
      return false;
    const bool known = std::any_of(live_.begin(), live_.end(), [&](const auto &entry) {
      return entry->id == ids[i] && entry->buffer != nullptr;
    });
    if (!known)
      return false;
  }
  return true;
}

std::size_t SampleExchange::collectLocked() {
  if ((triple_.load(std::memory_order_acquire) & fresh) == 0)
    return 0;
  read_ = triple_.exchange(read_, std::memory_order_acq_rel) & 3u;
  const auto &report = reports_[read_];
  if (!acceptable(report)) {
    ++stats_.reportsRejected;
    return 0;
  }
  ++stats_.reportsUsed;
  // An entry older than the slot's installed one was taken by audio and has
  // been replaced as of this report (a newer pending entry is never older).
  for (const auto &entry : live_)
    if (entry->replacedAt == 0 && entry->id < report.current[entry->slot])
      entry->replacedAt = report.epoch;
  const auto ids = std::span(report.ids.data(), report.count);
  std::size_t released = 0;
  for (std::size_t i = 0; i < live_.size();) {
    const auto &entry = *live_[i];
    const bool freeable = entry.replacedAt != 0 && report.epoch > entry.replacedAt &&
                          report.current[entry.slot] != entry.id &&
                          std::find(ids.begin(), ids.end(), entry.id) == ids.end();
    if (freeable) {
      releaseLocked(&entry);
      ++released;
    } else {
      ++i;
    }
  }
  return released;
}

void SampleExchange::quiesce() {
  const std::scoped_lock lock(mutex_);
  for (std::size_t i = 0; i < live_.size();) {
    const auto &entry = *live_[i];
    const auto *installed = current_[entry.slot];
    if (installed != nullptr && entry.id < installed->id)
      releaseLocked(&entry); // installed and replaced; no voice survives prepare
    else
      ++i;
  }
}

void SampleExchange::releaseLocked(const Entry *entry) {
  const auto found = std::find_if(live_.begin(), live_.end(),
                                  [&](const auto &e) { return e.get() == entry; });
  if (found == live_.end())
    return;
  auto owned = std::move(*found);
  live_.erase(found);
  ++stats_.released;
  if (sink_ && owned->buffer)
    sink_(std::move(owned->buffer));
}

SampleStats SampleExchange::stats() const {
  const std::scoped_lock lock(mutex_);
  auto copy = stats_;
  copy.live = live_.size();
  copy.liveBytes = liveBytes();
  return copy;
}

void SampleExchange::setAfterTakeHookForTesting(std::function<void()> hook) {
  const std::scoped_lock lock(mutex_);
  afterTakeForTesting_ = std::move(hook);
}

void SampleExchange::setReleaseSinkForTesting(ReleaseSink sink) {
  const std::scoped_lock lock(mutex_);
  sink_ = std::move(sink);
}

void SampleExchange::beginBlock() noexcept {
  for (std::uint32_t s = 0; s < slots_; ++s)
    if (auto *taken = pending_[s].exchange(nullptr, std::memory_order_acq_rel)) {
      current_[s] = taken;
      table_[s] = taken->view;
    }
  usage_.count = 0;
  usage_.overflow = 0;
}

void SampleExchange::endBlock(bool processed) noexcept {
  auto &report = reports_[write_];
  report.epoch = ++epoch_;
  report.valid = processed && usage_.count <= capacity_;
  report.overflow = usage_.overflow != 0;
  report.count = report.valid ? usage_.count : 0;
  std::copy_n(usageIds_.begin(), report.count, report.ids.begin());
  for (std::uint32_t s = 0; s < slots_; ++s)
    report.current[s] = current_[s] != nullptr ? current_[s]->id : 0;
  write_ = triple_.exchange(write_ | fresh, std::memory_order_acq_rel) & 3u;
}

} // namespace vstengine::instrument::native
