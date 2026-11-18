#pragma once
#include "ProgramCodec.h"
#include "Effects.h"
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
    struct Snapshot { NoiseProgram program; std::uint64_t effects=effects::defaults(); };
    bool publishState(const NoiseProgram& program, std::uint64_t effectsValue) {
        std::vector<std::byte> encoded;
        if (!programcodec::encode(program, encoded) || !effects::validPacked(effectsValue)) return false;
        const std::lock_guard<std::mutex> lock(mutex_);
        commit(program, encoded, effectsValue);
        return true;
    }
    bool publish(const NoiseProgram& program) {
        std::vector<std::byte> encoded;
        if (!programcodec::encode(program, encoded)) return false;
        const std::lock_guard<std::mutex> lock(mutex_);
        commit(program, encoded, desiredEffects_);
        return true;
    }
    // Serialized read-modify-write preserves edits to other layers even when
    // a UI gesture and a host-state restore arrive on different threads.
    template <typename Edit>
    bool editLayer(std::size_t index, Edit edit) {
        if (index >= 4) return false;
        const std::lock_guard<std::mutex> lock(mutex_);
        auto next = desired_;
        edit(next.layers[index]);
        std::vector<std::byte> encoded;
        if (!programcodec::encode(next, encoded)) return false;
        commit(next, encoded, desiredEffects_);
        return true;
    }
    // Serialized whole-program edit. A false return aborts without publication;
    // callers must not recursively enter this store from the editor function.
    template <typename Edit>
    bool editProgram(Edit edit) {
        const std::lock_guard<std::mutex> lock(mutex_);
        auto next = desired_;
        if (!edit(next)) return false;
        std::vector<std::byte> encoded;
        if (!programcodec::encode(next, encoded)) return false;
        commit(next, encoded, desiredEffects_);
        return true;
    }
    bool setLayerFamily(std::size_t index, NoiseFamily family) {
        if (static_cast<unsigned>(family) > static_cast<unsigned>(NoiseFamily::granular)) return false;
        return editLayer(index, [family] (NoiseLayer& layer) { layer.family = family; });
    }
    bool setEffect(std::size_t index, effects::Settings settings) {
        if(index>=4 || !settings.valid())return false;
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto shift=static_cast<unsigned>(index*16);
        const auto mask=std::uint64_t{0xffff}<<shift;
        const auto next=(desiredEffects_&~mask)|(static_cast<std::uint64_t>(effects::pack(settings))<<shift);
        std::vector<std::byte> encoded;
        if(!programcodec::encode(desired_,encoded))return false;
        commit(desired_,encoded,next);return true;
    }
    Snapshot snapshotState() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return {desired_,desiredEffects_};
    }
    NoiseProgram snapshot() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return desired_;
    }
    bool tryRead(NoiseProgram& destination, std::uint64_t& lastSequence) const noexcept {
        std::uint64_t ignored=0; return tryRead(destination,ignored,lastSequence);
    }
    bool tryRead(NoiseProgram& destination, std::uint64_t& effectDestination, std::uint64_t& lastSequence) const noexcept {
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
        const auto effectValue=effectWords_.load();
        const auto after=sequence_.load();
        if (before != after || (after & 1u) != 0) return false;
        NoiseProgram next;
        if (!programcodec::decode(std::span(bytes).first(count*4),next)) return false;
        if (!effects::validPacked(effectValue)) return false;
        destination=next; effectDestination=effectValue; lastSequence=after; return true;
    }
private:
    // Called with mutex_ held, only from producer threads.
    void commit(const NoiseProgram& program, const std::vector<std::byte>& encoded, std::uint64_t effectsValue) noexcept {
        const auto count = encoded.size() / 4;
        sequence_.fetch_add(1); // odd: write in progress
        wordCount_.store(static_cast<std::uint32_t>(count));
        for (std::size_t i=0; i<count; ++i) {
            std::uint32_t value=0;
            for (std::size_t byte=0; byte<4; ++byte)
                value |= std::to_integer<std::uint32_t>(encoded[i*4+byte]) << (8*byte);
            words_[i].store(value);
        }
        effectWords_.store(effectsValue);
        desired_ = program; desiredEffects_=effectsValue;
        sequence_.fetch_add(1); // even: complete publication
    }
    static constexpr std::size_t maxWords = (4 + 4 * (24 + 2 * (4 + 16 * 12))) / 4;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    mutable std::mutex mutex_; // only non-audio threads
    NoiseProgram desired_;
    std::uint64_t desiredEffects_=effects::defaults();
    std::atomic<std::uint64_t> effectWords_ {effects::defaults()};
    std::atomic<std::uint64_t> sequence_ {0};
    std::atomic<std::uint32_t> wordCount_ {0};
    std::array<std::atomic<std::uint32_t>, maxWords> words_ {};
};
} // namespace vox::fx
