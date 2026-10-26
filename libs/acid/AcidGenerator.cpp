#include "acid/AcidGenerator.h"
#include "instrument/InstrumentContract.h"
#include <algorithm>
#include <array>

namespace vstengine::acid {
namespace {
class Random final {
public:
    explicit Random(std::uint32_t seed) noexcept : state_(seed ? seed : 1u) {}
    std::uint32_t next() noexcept
    {
        auto value = state_;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        state_ = value;
        return value;
    }
    float unit() noexcept { return static_cast<float>(next() >> 8)
        * (1.0f / 16777216.0f); }
    int choose(int count) noexcept
    {
        return count > 0 ? static_cast<int>(next() % static_cast<std::uint32_t>(count)) : 0;
    }
private:
    std::uint32_t state_;
};

constexpr std::array<int, 8> minorNotes { 0, 3, 5, 7, 10, 12, -2, -5 };
constexpr std::array<int, 8> darkNotes { 0, -2, -5, -7, 3, 5, 10, 12 };

void setDefaults(sequence::Step& step) noexcept
{
    step = {};
    step.velocity = 0.74f;
    step.probability = 1.0f;
    step.ratchetCount = 1;
    step.gateWidth = 0.72f;
}
} // namespace

std::uint32_t AcidGenerator::stableSeed(std::uint32_t globalSeed,
                                        std::uint64_t slotId,
                                        std::string_view instrumentId) noexcept
{
    return instrument::generationSubSeed(globalSeed, slotId, instrumentId);
}

sequence::Sequence AcidGenerator::generate(AcidProfile profile,
                                            std::uint32_t globalSeed,
                                            std::uint64_t slotId,
                                            std::string_view instrumentId) noexcept
{
    const int length = profile == AcidProfile::hiTechAcid ? 32 : 16;
    sequence::Sequence result(length);
    result.setTimingMode(profile == AcidProfile::hypnoticAcid
        ? sequence::TimingMode::triplet
        : profile == AcidProfile::hiTechAcid
            ? sequence::TimingMode::thirtySecond
            : sequence::TimingMode::sixteenth);
    Random random(stableSeed(globalSeed, slotId, instrumentId)
                  ^ (0x9e3779b9u * (1u + static_cast<std::uint32_t>(profile))));

    int previousNote = 0;
    bool previousGate = false;
    for (int index = 0; index < length; ++index) {
        auto& step = result.getStep(index);
        setDefaults(step);
        const int beat = index % 4;
        switch (profile) {
            case AcidProfile::classic303:
                step.gate = beat != 3 || random.unit() > 0.42f;
                step.noteOffset = minorNotes[static_cast<std::size_t>(random.choose(6))];
                step.accent = step.gate && (beat == 0 || random.unit() < 0.18f);
                step.gateWidth = 0.62f;
                break;
            case AcidProfile::psyAcid:
                step.gate = beat != 0 || random.unit() > 0.58f;
                step.noteOffset = minorNotes[static_cast<std::size_t>(random.choose(7))];
                step.accent = step.gate && (beat == 1 || random.unit() < 0.14f);
                step.gateWidth = 0.58f;
                break;
            case AcidProfile::darkAcid:
                step.gate = random.unit() > (beat == 0 ? 0.48f : 0.18f);
                step.noteOffset = darkNotes[static_cast<std::size_t>(random.choose(6))];
                step.accent = step.gate && (index % 8 == 3 || random.unit() < 0.12f);
                step.velocity = 0.68f;
                step.gateWidth = 0.78f;
                break;
            case AcidProfile::forestAcid:
                step.gate = random.unit() > 0.24f;
                step.noteOffset = darkNotes[static_cast<std::size_t>(random.choose(8))];
                step.accent = step.gate && random.unit() < 0.27f;
                step.probability = step.gate && random.unit() < 0.2f ? 0.72f : 1.0f;
                step.gateWidth = 0.48f + random.unit() * 0.38f;
                break;
            case AcidProfile::hiTechAcid:
                step.gate = random.unit() > 0.16f;
                step.noteOffset = minorNotes[static_cast<std::size_t>(random.choose(8))];
                step.accent = step.gate && (index % 6 == 1 || random.unit() < 0.19f);
                step.ratchetCount = step.gate && random.unit() < 0.16f ? 2 : 1;
                step.gateWidth = 0.42f;
                break;
            case AcidProfile::hypnoticAcid:
                step.gate = (index % 3) != 2 || random.unit() > 0.64f;
                step.noteOffset = (index % 6 < 3) ? 0 : (random.unit() < 0.5f ? 3 : 7);
                step.accent = step.gate && index % 6 == 0;
                step.gateWidth = 0.88f;
                break;
        }
        step.velocity = std::clamp(step.velocity
            + (step.accent ? 0.22f : random.unit() * 0.1f - 0.05f), 0.35f, 1.0f);

        // Slide only joins adjacent audible, different-pitch notes. This keeps
        // Accent/Slide musical and matches scheduler glide eligibility.
        if (step.gate && previousGate && step.noteOffset != previousNote) {
            const float chance = profile == AcidProfile::hypnoticAcid ? 0.48f
                : profile == AcidProfile::classic303 ? 0.28f
                : profile == AcidProfile::hiTechAcid ? 0.18f : 0.34f;
            if (random.unit() < chance) {
                step.slideDuration = profile == AcidProfile::hypnoticAcid
                    ? 1.5f : 1.0f;
                step.gateWidth = 1.0f;
            }
        }
        previousGate = step.gate;
        if (step.gate) previousNote = step.noteOffset;
    }
    return result;
}

} // namespace vstengine::acid
