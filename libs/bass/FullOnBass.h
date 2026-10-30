#pragma once
#include "bass/PsyBassVoice.h"
#include "bass/FullOnBassParameters.h"
#include "instrument/InstrumentContract.h"

namespace vstengine::bass {

// Catalog registration is intentionally absent until the generic
// direct host automation/state path is implemented. This is real DSP, not a UI
// stand-in. All live methods require one audio-thread owner.
class FullOnBassInstance final : public instrument::InstrumentInstance {
public:
    FullOnBassInstance() noexcept;
    static instrument::InstrumentDescriptor descriptor();
    bool prepare(const instrument::PrepareSpec&) override;
    void reset() noexcept override;
    void releaseAllNotes() noexcept;
    void suspend() noexcept override;
    void resume() noexcept override;
    void setBypassed(bool) noexcept override;
    void process(const instrument::ProcessBlock&) noexcept override;
    bool setParameter(std::string_view, float) noexcept override;
    bool setParameterByIndex(std::size_t, float) noexcept;
    bool loadState(std::uint32_t, std::span<const std::byte>) noexcept override;
    bool saveState(std::span<std::byte>, std::uint32_t&) const noexcept override;
    std::uint32_t latencySamples() const noexcept override { return 0; }
    std::uint32_t tailSamples() const noexcept override;
private:
    void updateVoice() noexcept;
    void resumeMostRecentNote() noexcept;
    void forgetHeldNote(std::size_t index) noexcept;
    static constexpr std::uint16_t noHeldNote = 16 * 128;
    struct HeldNote {
        float velocity {};
        std::uint16_t previous { noHeldNote }, next { noHeldNote };
        bool active {}, pressed {}, sustained {};
    };
    std::array<float, fullOnParameters.size()> values {};
    PsyBassVoice voice;
    instrument::PrepareSpec spec;
    int currentNote { -1 }, currentChannel { -1 };
    std::array<bool, 16> sustainDown {};
    // One bounded entry per MIDI channel/key; head is the last held note.
    // Intrusive links make retrigger and fallback constant-time without allocation.
    std::array<HeldNote, 16 * 128> heldNotes {};
    std::uint16_t mostRecentNote { noHeldNote };
    bool keyHeld {};
    std::uint32_t dirtyParameters { (1u << fullOnParameters.size()) - 1u };
    bool bypassed {}, suspended {};
};
} // namespace vstengine::bass
