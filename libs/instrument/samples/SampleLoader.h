#pragma once
// Contract C2: the bounded sample loader worker. One background thread decodes
// requested WAV files and assigns them through the Rack, which publishes them
// with the C1 hand-off. The queue is bounded; a full queue refuses the
// request. Nothing here runs on the audio thread.
#include "instrument/samples/SampleDecoder.h"
#include "rack/Rack.h"

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace vstengine::instrument::samples {

class SampleLoader {
public:
  using SampleRef = rack::PersistentSlotState::SampleRef;
  // Finds the file of a factory sample: the instrument's verified package
  // member for `name`, and its expected SHA-256. Nullopt: unknown sample.
  struct FactoryFile {
    std::filesystem::path path;
    std::string sha256;
  };
  using FactoryResolver =
      std::function<std::optional<FactoryFile>(std::size_t slot, const std::string &name)>;

  struct Options {
    SampleLimits limits;
    std::size_t queueCapacity{64};
    // PROPOSED decoded-memory budget per Rack slot (all its sample slots).
    std::uint64_t slotBudgetBytes{256ull * 1024 * 1024};
  };

  SampleLoader(rack::Rack &rack, Options options, FactoryResolver factory = {});
  // Stops the worker after the job in progress; pending requests are dropped.
  ~SampleLoader();
  SampleLoader(const SampleLoader &) = delete;
  SampleLoader &operator=(const SampleLoader &) = delete;

  struct Request {
    std::size_t slot{};
    std::uint64_t generation{};
    // For a new assignment sha256 is empty and bytes 0: the decoder fills
    // them in. For a saved reference they must match the file.
    SampleRef ref;
    // Clears ref.sampleSlot instead (queued, so requests keep their order).
    bool clear{};
  };
  // Any controller thread. False (and why) when the queue is full.
  bool enqueue(Request request, std::string &why);
  // Queues the saved references of the Rack (after a restore or a rebuild),
  // of one slot only when `slot` is given.
  std::size_t enqueueSaved(std::string &why, std::optional<std::size_t> slot = std::nullopt);
  // Tests and shutdown: blocks until the queue is empty and the worker idle.
  void waitIdle();

  struct Stats {
    std::uint64_t loaded{}, failed{}, refused{};
  };
  [[nodiscard]] Stats stats() const;

private:
  void run();
  void load(const Request &request);

  rack::Rack &rack_;
  const Options options_;
  FactoryResolver factory_;
  mutable std::mutex mutex_;
  std::condition_variable wake_, idle_;
  std::deque<Request> queue_;
  bool busy_{};
  bool stop_{};
  Stats stats_{};
  // Decoded bytes per Rack slot and sample slot, for the budget; reset when
  // the slot's instance generation changes.
  struct SlotUse {
    std::uint64_t generation{};
    std::map<std::uint32_t, std::uint64_t> bytes;
  };
  std::map<std::size_t, SlotUse> use_;
  std::thread worker_;
};

} // namespace vstengine::instrument::samples
