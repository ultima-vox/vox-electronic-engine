#pragma once
#include "ProgramCodec.h"
#include "Effects.h"
#include "PatternCodec.h"
#include "MacroRouting.h"
#include "GeneratorState.h"
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
    struct Snapshot { NoiseProgram program; std::uint64_t effects=effects::defaults(); pattern::Pattern pattern; macro::Config macros; generator::ControllerState generator; };
    bool publishState(const NoiseProgram& program, std::uint64_t effectsValue, const pattern::Pattern& patternValue,
                      const macro::Config& macrosValue, const generator::ControllerState& generatorValue) {
        std::vector<std::byte> encoded;
        if (!programcodec::encode(program,encoded)||!effects::validPacked(effectsValue)||!patternValue.valid()||!macrosValue.valid()||!generatorValue.valid())return false;
        const std::lock_guard<std::mutex> lock(mutex_);
        commit(program,encoded,effectsValue,patternValue,macrosValue,generatorValue);advanceProgramRevision();return true;
    }
    bool publishState(const NoiseProgram& program, std::uint64_t effectsValue, const pattern::Pattern& patternValue,
                      const macro::Config& macrosValue) {
        std::vector<std::byte> encoded;
        if (!programcodec::encode(program,encoded) || !effects::validPacked(effectsValue) || !patternValue.valid() || !macrosValue.valid())return false;
        const std::lock_guard<std::mutex> lock(mutex_);
        commit(program,encoded,effectsValue,patternValue,macrosValue,desiredGenerator_);advanceProgramRevision();return true;
    }
    bool publishState(const NoiseProgram& program, std::uint64_t effectsValue, const pattern::Pattern& patternValue) {
        std::vector<std::byte> encoded;
        if (!programcodec::encode(program, encoded) || !effects::validPacked(effectsValue) || !patternValue.valid()) return false;
        const std::lock_guard<std::mutex> lock(mutex_);
        commit(program, encoded, effectsValue, patternValue, desiredMacros_, desiredGenerator_);
        advanceProgramRevision();return true;
    }
    bool publishState(const NoiseProgram& program, std::uint64_t effectsValue) {
        std::vector<std::byte> encoded;
        if (!programcodec::encode(program, encoded) || !effects::validPacked(effectsValue)) return false;
        const std::lock_guard<std::mutex> lock(mutex_);
        commit(program, encoded, effectsValue, desiredPattern_, desiredMacros_, desiredGenerator_);
        advanceProgramRevision();return true;
    }
    bool publish(const NoiseProgram& program) {
        std::vector<std::byte> encoded;
        if (!programcodec::encode(program, encoded)) return false;
        const std::lock_guard<std::mutex> lock(mutex_);
        commit(program, encoded, desiredEffects_, desiredPattern_, desiredMacros_, desiredGenerator_);
        advanceProgramRevision();return true;
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
        commit(next, encoded, desiredEffects_, desiredPattern_, desiredMacros_, desiredGenerator_);
        return true;
    }
    // Compare under the same writer lock as whole-Program publication, so a
    // stale UI gesture cannot modify a newly restored Program.
    template <typename Edit>
    bool editLayerIfRevision(std::size_t index,std::uint64_t revision,Edit edit) {
        if(index>=4)return false;
        const std::lock_guard<std::mutex> lock(mutex_);
        if(programRevision_.load(std::memory_order_relaxed)!=revision)return false;
        auto next=desired_;edit(next.layers[index]);std::vector<std::byte> encoded;
        if(!programcodec::encode(next,encoded))return false;
        commit(next,encoded,desiredEffects_,desiredPattern_,desiredMacros_,desiredGenerator_);return true;
    }
    std::uint64_t programRevision() const noexcept { return programRevision_.load(std::memory_order_acquire); }
    struct VersionedProgram { NoiseProgram program;std::uint64_t revision; };
    VersionedProgram versionedProgram() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return {desired_,programRevision_.load(std::memory_order_relaxed)};
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
        commit(next, encoded, desiredEffects_, desiredPattern_, desiredMacros_, desiredGenerator_);
        advanceProgramRevision();return true;
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
        commit(desired_,encoded,next,desiredPattern_,desiredMacros_,desiredGenerator_);return true;
    }
    bool setPattern(const pattern::Pattern& value) {
        if(!value.valid())return false;
        const std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::byte> encoded;
        if(!programcodec::encode(desired_,encoded))return false;
        commit(desired_,encoded,desiredEffects_,value,desiredMacros_,desiredGenerator_);return true;
    }
    template <typename Edit>
    bool editPattern(Edit edit) {
        const std::lock_guard<std::mutex> lock(mutex_);
        auto next=desiredPattern_;edit(next);
        if(!next.valid())return false;
        std::vector<std::byte> encoded;
        if(!programcodec::encode(desired_,encoded))return false;
        commit(desired_,encoded,desiredEffects_,next,desiredMacros_,desiredGenerator_);return true;
    }
    bool setMacroConfig(const macro::Config& config) {
        if(!config.valid())return false;
        const std::lock_guard<std::mutex> lock(mutex_);std::vector<std::byte> encoded;
        if(!programcodec::encode(desired_,encoded))return false;
        commit(desired_,encoded,desiredEffects_,desiredPattern_,config,desiredGenerator_);return true;
    }
    template<typename Edit> bool editMacroConfig(Edit edit) {
        const std::lock_guard<std::mutex> lock(mutex_);auto next=desiredMacros_;
        if(!edit(next)||!next.valid())return false;
        std::vector<std::byte> encoded;if(!programcodec::encode(desired_,encoded))return false;
        commit(desired_,encoded,desiredEffects_,desiredPattern_,next,desiredGenerator_);return true;
    }
    template<typename Edit> bool editGeneratorState(Edit edit) {
        const std::lock_guard<std::mutex> lock(mutex_);
        auto next=desiredGenerator_;edit(next);if(!next.valid())return false;
        // Controller metadata is not used in audio; avoid republishing the DSP graph.
        desiredGenerator_=next;return true;
    }
    Snapshot snapshotState() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return {desired_,desiredEffects_,desiredPattern_,desiredMacros_,desiredGenerator_};
    }
    NoiseProgram snapshot() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return desired_;
    }
    bool tryRead(NoiseProgram& destination, std::uint64_t& lastSequence) const noexcept {
        std::uint64_t ignored=0; return tryRead(destination,ignored,lastSequence);
    }
    bool tryRead(NoiseProgram& destination, std::uint64_t& effectDestination, std::uint64_t& lastSequence) const noexcept {
        pattern::Pattern ignored; return tryRead(destination,effectDestination,ignored,lastSequence);
    }
    bool tryRead(NoiseProgram& destination, std::uint64_t& effectDestination, pattern::Pattern& patternDestination, std::uint64_t& lastSequence) const noexcept {
        macro::Config ignored;return tryRead(destination,effectDestination,patternDestination,ignored,lastSequence);
    }
    bool tryRead(NoiseProgram& destination, std::uint64_t& effectDestination, pattern::Pattern& patternDestination,
                 macro::Config& macroDestination, std::uint64_t& lastSequence) const noexcept {
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
        pattern::codec::Words patternWords{};
        for(std::size_t i=0;i<patternWords.size();++i)patternWords[i]=patternWords_[i].load();
        macro::codec::Words macroWords{};
        for(std::size_t i=0;i<macroWords.size();++i)macroWords[i]=macroWords_[i].load();
        const auto after=sequence_.load();
        if (before != after || (after & 1u) != 0) return false;
        NoiseProgram next;
        if (!programcodec::decode(std::span(bytes).first(count*4),next)) return false;
        pattern::Pattern nextPattern;
        macro::Config nextMacros;
        if (!effects::validPacked(effectValue) || !pattern::codec::fromWords(patternWords,nextPattern) ||
            !macro::codec::fromWords(macroWords,nextMacros)) return false;
        destination=next; effectDestination=effectValue; patternDestination=nextPattern; macroDestination=nextMacros;
        lastSequence=after; return true;
    }
private:
    void advanceProgramRevision() noexcept { programRevision_.fetch_add(1,std::memory_order_release); }
    // Called with mutex_ held, only from producer threads.
    void commit(const NoiseProgram& program, const std::vector<std::byte>& encoded, std::uint64_t effectsValue,
                const pattern::Pattern& patternValue,const macro::Config& macrosValue,const generator::ControllerState& generatorValue) noexcept {
        macro::codec::Words macroWords{};(void)macro::codec::toWords(macrosValue,macroWords);
        pattern::codec::Words patternWords{};
        (void) pattern::codec::toWords(patternValue,patternWords); // all controller entry points validate
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
        for(std::size_t i=0;i<patternWords.size();++i)patternWords_[i].store(patternWords[i]);
        for(std::size_t i=0;i<macroWords.size();++i)macroWords_[i].store(macroWords[i]);
        desired_ = program; desiredEffects_=effectsValue; desiredPattern_=patternValue; desiredMacros_=macrosValue; desiredGenerator_=generatorValue;
        sequence_.fetch_add(1); // even: complete publication
    }
    // v4 carries four trajectories on every layer plus four Program-wide,
    // followed by bars/mode/trigger-policy words.
    static constexpr std::size_t maxWords = (16 + 4 * (24 + 4 * (4 + 16 * 12)) + 4 * (4 + 16 * 12)) / 4;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    mutable std::mutex mutex_; // only non-audio threads
    NoiseProgram desired_;
    std::uint64_t desiredEffects_=effects::defaults();
    pattern::Pattern desiredPattern_{};
    macro::Config desiredMacros_{};
    generator::ControllerState desiredGenerator_{};
    std::array<std::atomic<std::uint32_t>,macro::codec::wordCount> macroWords_{};
    std::array<std::atomic<std::uint32_t>,pattern::codec::wordCount> patternWords_{};
    std::atomic<std::uint64_t> effectWords_ {effects::defaults()};
    std::atomic<std::uint64_t> sequence_ {0};
    std::atomic<std::uint64_t> programRevision_ {0};
    std::atomic<std::uint32_t> wordCount_ {0};
    std::array<std::atomic<std::uint32_t>, maxWords> words_ {};
};
} // namespace vox::fx
