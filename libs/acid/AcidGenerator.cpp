#if __has_include("acid/AcidGenerator.h")
#include "acid/AcidGenerator.h"
#else
#include "AcidGenerator.h"
#endif

#include <algorithm>
#include <array>

namespace vstengine::acid {

namespace {

struct XorShift32 {
    std::uint32_t state;

    explicit constexpr XorShift32(std::uint32_t seed) noexcept
        : state(seed != 0 ? seed : 0x9E3779B9u) {}

    [[nodiscard]] constexpr std::uint32_t next() noexcept {
        std::uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    [[nodiscard]] constexpr float nextFloat01() noexcept {
        return static_cast<float>(next() & 0x00FFFFFFu) /
               static_cast<float>(0x01000000u);
    }
};

constexpr std::array<std::int8_t, 7> kPhrygianScale   { 0, 1, 3, 5, 7, 8, 10 };
constexpr std::array<std::int8_t, 7> kNaturalMinor    { 0, 2, 3, 5, 7, 8, 10 };
constexpr std::array<std::int8_t, 6> kHarmonicAcid    { 0, 1, 4, 5, 7, 8 };
constexpr std::array<std::int8_t, 5> kTechnoMinorPent { 0, 3, 5, 7, 10 };

[[nodiscard]] std::int8_t pickScaleInterval(AcidProfile profile,
                                            XorShift32& rng,
                                            float variation) noexcept {
    if (rng.nextFloat01() > (0.35f + 0.55f * variation)) {
        return 0;
    }

    switch (profile) {
        case AcidProfile::psyAcid:
        case AcidProfile::forestAcid:
        case AcidProfile::hiTechAcid: {
            const std::size_t idx = rng.next() % kHarmonicAcid.size();
            return kHarmonicAcid[idx];
        }
        case AcidProfile::darkAcid: {
            const std::size_t idx = rng.next() % kPhrygianScale.size();
            return kPhrygianScale[idx];
        }
        case AcidProfile::technoDrive:
        case AcidProfile::hypnoticAcid: {
            const std::size_t idx = rng.next() % kTechnoMinorPent.size();
            return kTechnoMinorPent[idx];
        }
        case AcidProfile::classic303:
        default: {
            const std::size_t idx = rng.next() % kNaturalMinor.size();
            return kNaturalMinor[idx];
        }
    }
}

} // namespace

std::uint32_t AcidGenerator::stableSeed(
    std::uint32_t globalSeed, std::uint64_t slotId,
    std::string_view instrumentId) noexcept {
    std::uint32_t hash = 2166136261u ^ globalSeed;
    for (int i = 0; i < 8; ++i) {
        hash ^= static_cast<std::uint32_t>((slotId >> (i * 8)) & 0xFFu);
        hash *= 16777619u;
    }
    for (char c : instrumentId) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 16777619u;
    }
    return hash != 0 ? hash : 0xA341316Cu;
}

sequence::Sequence AcidGenerator::generate(
    AcidProfile profile, std::uint32_t globalSeed, std::uint64_t slotId,
    std::string_view instrumentId) noexcept {
    AcidPatternParams params {};
    params.profile = profile;

    switch (profile) {
        case AcidProfile::classic303:
            params.density = 0.70f;
            params.variation = 0.50f;
            break;
        case AcidProfile::psyAcid:
            params.density = 0.85f;
            params.variation = 0.45f;
            break;
        case AcidProfile::darkAcid:
            params.density = 0.65f;
            params.variation = 0.55f;
            break;
        case AcidProfile::forestAcid:
            params.density = 0.75f;
            params.variation = 0.65f;
            break;
        case AcidProfile::hiTechAcid:
            params.density = 0.90f;
            params.variation = 0.75f;
            break;
        case AcidProfile::hypnoticAcid:
            params.density = 0.80f;
            params.variation = 0.30f;
            break;
        case AcidProfile::technoDrive:
            params.density = 0.70f;
            params.variation = 0.50f;
            break;
    }

    return generateWithParams(params, globalSeed, slotId, instrumentId);
}

sequence::Sequence AcidGenerator::generateWithParams(
    const AcidPatternParams& params, std::uint32_t globalSeed,
    std::uint64_t slotId, std::string_view instrumentId) noexcept {
    sequence::Sequence seq {};
    seq.setLength(static_cast<int>(std::clamp<std::uint32_t>(
        params.length, 1u, static_cast<std::uint32_t>(sequence::Sequence::maxSteps))));
    seq.setTimingMode(sequence::TimingMode::sixteenth);

    const std::uint32_t seed =
        stableSeed(globalSeed, slotId, instrumentId) ^
        (static_cast<std::uint32_t>(params.profile) * 0x45D9F3Bu);
    XorShift32 rng(seed);

    const float density = std::clamp(params.density, 0.05f, 1.0f);
    const float variation = std::clamp(params.variation, 0.0f, 1.0f);

    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(seq.size()); ++i) {
        auto& step = seq[static_cast<int>(i)];
        const bool isDownbeat = (i % 4 == 0);
        const bool isOffbeat = (i % 4 == 2);

        const float gateThreshold = isDownbeat ? std::min(1.0f, density + 0.25f)
                                               : density;
        step.gate = (rng.nextFloat01() < gateThreshold);

        const std::int8_t interval =
            pickScaleInterval(params.profile, rng, variation);

        std::int8_t octaveShift = 0;
        const float octRoll = rng.nextFloat01();
        if (octRoll < 0.22f * variation) {
            octaveShift = 1;
        } else if (octRoll < 0.32f * variation && !isDownbeat) {
            octaveShift = -1;
        }
        // Octave is folded into the canonical semitone noteOffset.

        const int midiNote = std::clamp(
            static_cast<int>(params.rootNote) + static_cast<int>(interval) +
                static_cast<int>(octaveShift) * 12,
            24, 96);
        step.noteOffset = midiNote - static_cast<int>(params.rootNote);

        const float accentProb =
            (isOffbeat ? 0.35f : 0.18f) * (0.4f + 0.8f * variation);
        step.accent = step.gate && (rng.nextFloat01() < accentProb);

        const float slideProb = 0.28f * (0.3f + 0.9f * variation);
        step.slideDuration =
            (step.gate && rng.nextFloat01() < slideProb) ? 0.5f : 0.0f;
        step.gateWidth = step.slideDuration > 0.0f ? 1.0f : 0.72f;

        step.velocity = step.accent ? (122.0f / 127.0f) : (96.0f / 127.0f);
    }

    return seq;
}

sequence::Sequence AcidGenerator::mutate(
    const sequence::Sequence& current, const AcidPatternParams& params,
    std::uint32_t mutationCounter, std::uint32_t globalSeed,
    std::uint64_t slotId) noexcept {
    sequence::Sequence nextSeq = current;
    nextSeq.setLength(static_cast<int>(std::clamp<std::uint32_t>(
        params.length, 1u, static_cast<std::uint32_t>(sequence::Sequence::maxSteps))));

    const std::uint32_t seed =
        stableSeed(globalSeed ^ (mutationCounter * 0x27D4EB2Du), slotId,
                   "com.ultimavox.acid.mutate");
    XorShift32 rng(seed);

    const float variation = std::clamp(params.variation, 0.1f, 1.0f);

    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(nextSeq.size()); ++i) {
        if (rng.nextFloat01() > (0.18f + 0.25f * variation)) {
            continue;
        }

        auto& step = nextSeq[static_cast<int>(i)];
        const std::uint32_t action = rng.next() % 4u;

        switch (action) {
            case 0: {
                const std::int8_t interval =
                    pickScaleInterval(params.profile, rng, variation);
                const int oldOctave = step.noteOffset / 12;
                const int midiNote = std::clamp(
                    static_cast<int>(params.rootNote) + static_cast<int>(interval)
                        + oldOctave * 12, 24, 96);
                step.noteOffset = midiNote - static_cast<int>(params.rootNote);
                break;
            }
            case 1: {
                step.accent = !step.accent && step.gate;
                step.velocity = step.accent ? (122.0f / 127.0f) : (96.0f / 127.0f);
                break;
            }
            case 2: {
                step.slideDuration =
                    (step.slideDuration <= 0.0f && step.gate) ? 0.5f : 0.0f;
                step.gateWidth = step.slideDuration > 0.0f ? 1.0f : 0.72f;
                break;
            }
            case 3: {
                if (i % 4 != 0 && rng.nextFloat01() < 0.5f) {
                    step.gate = (rng.nextFloat01() < params.density);
                } else {
                    constexpr std::array<std::int8_t, 3> kOctaves { -1, 0, 1 };
                    const int semitones =
                        static_cast<int>(kOctaves[rng.next() % kOctaves.size()]) * 12;
                    step.noteOffset = std::clamp(
                        step.noteOffset + semitones, -48, 48);
                }
                break;
            }
        }
    }

    return nextSeq;
}

} // namespace vstengine::acid