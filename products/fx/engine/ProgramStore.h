#pragma once
#include "ProgramCodec.h"
#include <array>
#include <atomic>
#include <mutex>

namespace vox::fx {

// Multi-producer/controller state and bounded, lock-free audio snapshot reader.
// Writers serialize/validate and may lock or allocate; never call publish or
// snapshot from the callback. Audio attempts once per block, retaining its
// old Program if a write overlaps. Every payload word and sequence operation
// is sequentially consistent: a reader can never accept a mixed write even
// when its two sequence reads see the same value. No graph lifetime crosses
// threads, no queues overflow, and no dynamic allocation occurs on read.
class ProgramStore {
public:
    ProgramStore() { (void) publish(NoiseProgram{}); }
    bool publish(const NoiseProgram& program) {
        std::vector<std::byte> encoded;
        if (!programcodec::encode(program, encoded)) return false;
        const std::lock_guard<std::mutex> lock(mutex_);
        commit(program, encoded);
        return true;
    }
    // Serialized read-modify-write for the structural family selector. This
    // prevents a concurrent preset restore from losing another layer edit.
    bool setLayerFamily(std::size_t index, NoiseFamily family) {
        if (index >= 4 || static_cast<unsigned>(family) > static_cast<unsigned>(NoiseFamily::formant)) return false;
        const std::lock_guard<std::mutex> lock(mutex_);
        auto next = desired_;
        next.layers[index].family = family;
        std::vector<std::byte> encoded;
        if (!programcodec::encode(next, encoded)) return false;
        commit(next, encoded);
        return true;
    }
    NoiseProgram snapshot() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return desired_;
    }
    bool tryRead(NoiseProgram& destination, std::uint64_t& lastSequence) const noexcept {
        const auto before=sequence_.load();
        if ((before & 1u) != 0 || before == lastSequence) return false;
        const auto count=wordCount_.load();
        if (count > maxWords) return false;
        std::array<std::byte,maxWords*4> bytes {};
        for (std::size_t i=0; i<count; ++i) {
            const auto value=words_[i].load();
            for (std::size_t byte=0; byte<4; ++byte)
                bytes[i*4+byte]=static_cast<std::byte>((value >> (8*byte)) & 0xffu);
        }
        const auto after=sequence_.load();
        if (before != after || (after & 1u) != 0) return false;
        NoiseProgram next;
        if (!programcodec::decode(std::span(bytes).first(count*4),next)) return false;
        destination=next; lastSequence=after; return true;
    }
private:
    // Called with mutex_ held, only from producer threads.
    void commit(const NoiseProgram& program, const std::vector<std::byte>& encoded) noexcept {
        const auto count = encoded.size() / 4;
        sequence_.fetch_add(1); // odd: write in progress
        wordCount_.store(static_cast<std::uint32_t>(count));
        for (std::size_t i=0; i<count; ++i) {
            std::uint32_t value=0;
            for (std::size_t byte=0; byte<4; ++byte)
                value |= std::to_integer<std::uint32_t>(encoded[i*4+byte]) << (8*byte);
            words_[i].store(value);
        }
        desired_ = program;
        sequence_.fetch_add(1); // even: complete publication
    }
    static constexpr std::size_t maxWords = (4 + 4 * (24 + 2 * (4 + 16 * 12))) / 4;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    mutable std::mutex mutex_; // only non-audio threads
    NoiseProgram desired_;
    std::atomic<std::uint64_t> sequence_ {0};
    std::atomic<std::uint32_t> wordCount_ {0};
    std::array<std::atomic<std::uint32_t>, maxWords> words_ {};
};
} // namespace vox::fx
