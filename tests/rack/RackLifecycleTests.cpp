#include "rack/Rack.h"
#include "rack/RackState.h"
#include "support/ReferenceInstrument.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace vstengine;
using States = std::array<rack::PersistentSlotState, instrument::maxSlots>;
int checks {};
void require(bool condition, const char* message)
{
    ++checks;
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
class PrepareFault final : public instrument::InstrumentInstance {
public:
    PrepareFault(std::unique_ptr<instrument::InstrumentInstance> inner,
                 instrument::SlotId slot, int& fault)
        : inner_(std::move(inner)), slot_(slot), fault_(fault) {}
    bool prepare(const instrument::PrepareSpec& spec) override
    {
        if (slot_ == instrument::initialSlotId(1)) {
            if (fault_ == 1) return false;
            if (fault_ == 2) throw std::runtime_error("test prepare fault");
        }
        return inner_->prepare(spec);
    }
    void reset() noexcept override { inner_->reset(); }
    void suspend() noexcept override { inner_->suspend(); }
    void resume() noexcept override { inner_->resume(); }
    void setBypassed(bool b) noexcept override { inner_->setBypassed(b); }
    void process(const instrument::ProcessBlock& b) noexcept override { inner_->process(b); }
    bool setParameter(std::string_view id, float value) noexcept override
    { return inner_->setParameter(id, value); }
    bool loadState(std::uint32_t schema, std::span<const std::byte> payload) noexcept override
    { return inner_->loadState(schema, payload); }
    bool saveState(std::span<std::byte> data, std::uint32_t& count) const noexcept override
    { return inner_->saveState(data, count); }
    std::uint32_t latencySamples() const noexcept override { return inner_->latencySamples(); }
    std::uint32_t tailSamples() const noexcept override { return inner_->tailSamples(); }
private:
    std::unique_ptr<instrument::InstrumentInstance> inner_;
    instrument::SlotId slot_;
    int& fault_;
};
class CountingProvider final : public instrument::InstrumentProvider {
public:
    CountingProvider(int& count, int& fault) : count_(count), fault_(fault),
        inner_(tests::createReferenceInstrumentProvider()) {}
    std::span<const instrument::InstrumentDescriptor> descriptors() const noexcept override
    { return inner_->descriptors(); }
    std::unique_ptr<instrument::InstrumentInstance> create(
        std::string_view id, const instrument::CreateContext& context) override
    {
        ++count_;
        auto instance = inner_->create(id, context);
        return instance ? std::make_unique<PrepareFault>(std::move(instance), context.slotId, fault_)
                        : nullptr;
    }
private:
    int& count_;
    int& fault_;
    std::unique_ptr<instrument::InstrumentProvider> inner_;
};
VoxMidiEventV1 note(std::uint8_t status, std::uint8_t key = 60)
{ return { 0, 3, { status, key, static_cast<std::uint8_t>(status < 0x90 ? 0 : 100) } }; }
double render(rack::Rack& rack, std::span<const VoxMidiEventV1> midi = {})
{
    std::array<float, 256> left {}, right {};
    std::array<float*, 2> outputs { left.data(), right.data() };
    rack.process(outputs, 256, midi, 145.0, 0.0, true);
    require(std::all_of(left.begin(), left.end(), [](float x) { return std::isfinite(x); }),
            "rack output is finite");
    return std::inner_product(left.begin(), left.end(), left.begin(), 0.0);
}
juce::String snapshot(rack::Rack& rack)
{ return rack::state::serialize(rack.snapshotState()).toXmlString(); }
void controls(rack::Rack& rack, std::size_t index, bool mute = false, bool solo = false)
{
    auto routing = rack.state()[index].routing;
    rack.updateControls(index, routing, true, mute, solo, false, 1.0f, 0.0f);
}
} // namespace

int main()
{
    using namespace vstengine;
    int constructions {};
    int prepareFault {};
    instrument::InstrumentRegistry registry;
    std::string diagnostic;
    require(registry.registerProvider(std::make_unique<CountingProvider>(constructions, prepareFault),
                                      diagnostic), "reference provider registers");
    auto rack = std::make_unique<rack::Rack>(registry);
    require(rack->prepare({ 48000.0, 256, 2 }), "rack prepares");
    for (std::size_t index = 0; index < 3; ++index) {
        require(rack->loadModule(index, tests::referenceInstrumentId, nullptr, diagnostic),
                "reference instrument loads");
        auto routing = rack->state()[index].routing;
        routing.mode = rack::RouteMode::channel;
        routing.channel = static_cast<std::uint8_t>(index + 1);
        rack->updateControls(index, routing, true, false, false, false, 1.0f, 0.0f);
    }
    const auto valid = rack->snapshotState();
    struct Mutation { const char* name; std::function<void(States&)> apply; };
    const std::vector<Mutation> mutations {
        { "duplicate empty SlotId", [](auto& s) { s.back().slotId = s[0].slotId; } },
        { "duplicate active SlotId", [](auto& s) { s[1].slotId = s[0].slotId; } },
        { "zero SlotId", [](auto& s) { s.back().slotId = 0; } },
        { "wrong slot schema", [](auto& s) { s.back().schemaVersion = 0; } },
        { "invalid instrument ID", [](auto& s) { s.back().instrumentId = "Bad/Instrument"; } },
        { "invalid provider ID", [](auto& s) { s.back().resolvedProviderId = "Bad/Provider"; } },
        { "invalid macro mapping", [](auto& s) { s.back().macroAssignments[0] = "Bad/Parameter"; } },
        { "NaN macro", [](auto& s) { s.back().macros[0] = std::numeric_limits<float>::quiet_NaN(); } },
        { "infinite macro", [](auto& s) { s.back().macros[0] = std::numeric_limits<float>::infinity(); } },
        { "negative macro", [](auto& s) { s.back().macros[0] = -0.01f; } },
        { "oversized macro", [](auto& s) { s.back().macros[0] = 1.01f; } },
        { "NaN level", [](auto& s) { s.back().level = std::numeric_limits<float>::quiet_NaN(); } },
        { "infinite level", [](auto& s) { s.back().level = std::numeric_limits<float>::infinity(); } },
        { "negative level", [](auto& s) { s.back().level = -0.01f; } },
        { "oversized level", [](auto& s) { s.back().level = 2.01f; } },
        { "NaN pan", [](auto& s) { s.back().pan = std::numeric_limits<float>::quiet_NaN(); } },
        { "negative pan", [](auto& s) { s.back().pan = -1.01f; } },
        { "oversized pan", [](auto& s) { s.back().pan = 1.01f; } },
        { "unsupported output", [](auto& s) { s.back().outputDestination = 1; } },
        { "zero pattern schema", [](auto& s) { s.back().patternSchemaVersion = 0; } },
        { "oversized module payload", [](auto& s) { s.back().modulePayload.resize(rack::state::maxModulePayloadBytes + 1); } },
        { "oversized pattern payload", [](auto& s) { s.back().patternPayload.resize(rack::state::maxPatternPayloadBytes + 1); } },
        { "oversized modulation payload", [](auto& s) { s.back().modulationPayload.resize(rack::state::maxModulationPayloadBytes + 1); } },
        { "oversized missing module", [](auto& s) { s.back().instrumentId = "com.ultimavox.test-missing"; s.back().modulePayload.resize(rack::state::maxModulePayloadBytes + 1); } },
        { "oversized future pattern", [](auto& s) { s.back().patternSchemaVersion = VOX_PATTERN_SCHEMA_V1 + 1; s.back().patternPayload.resize(rack::state::maxPatternPayloadBytes + 1); } },
        { "invalid routing mode", [](auto& s) { s.back().routing.mode = static_cast<rack::RouteMode>(255); } },
        { "invalid key zone", [](auto& s) { s.back().routing.keyHigh = 128; } },
        { "invalid velocity zone", [](auto& s) { s.back().routing.velocityHigh = 128; } },
        { "invalid MIDI channel", [](auto& s) { s.back().routing.channel = 0; } },
        { "oversized sound name", [](auto& s) { s.back().soundPreset.assign(257, 'a'); } },
        { "oversized pattern name", [](auto& s) { s.back().patternPreset.assign(257, 'a'); } }
    };
    for (const auto& mutation : mutations) {
        auto bad = valid;
        mutation.apply(bad);
        rack->reset();
        require(render(*rack, std::array { note(0x90) }) > 1.0e-6, "note sounds before rejected state");
        const auto before = snapshot(*rack);
        const int beforeConstructions = constructions;
        diagnostic.clear();
        if (rack->replaceState(bad, nullptr, diagnostic)) {
            std::cerr << "Accepted invalid state: " << mutation.name << '\n';
            return EXIT_FAILURE;
        }
        require(!diagnostic.empty(), "invalid state gives diagnostic");
        require(constructions == beforeConstructions, "preflight precedes every module construction");
        require(snapshot(*rack) == before, "invalid state leaves persistent rack unchanged");
        require(render(*rack) > 1.0e-8, "invalid state leaves sounding voice unchanged");
        render(*rack, std::array { note(0x80) });
        require(render(*rack) == 0.0, "original NoteOff ownership survives rejected state");
    }
    // XML and direct callers share finite/range validation. The destination is transactional.
    const auto encoded = rack::state::serialize(valid);
    for (const auto& [name, value] : std::vector<std::pair<const char*, double>> {
            { "level", -0.01 }, { "level", 2.01 },
            { "level", std::numeric_limits<double>::quiet_NaN() },
            { "pan", -1.01 }, { "pan", 1.01 },
            { "pan", std::numeric_limits<double>::infinity() } }) {
        auto malformed = encoded.createCopy();
        malformed.getChild(15).setProperty(name, value, nullptr);
        auto decoded = valid;
        require(!rack::state::deserialize(malformed, decoded, diagnostic),
                "serialized invalid mix state is rejected");
        require(rack::state::serialize(decoded).toXmlString() == encoded.toXmlString(),
                "failed deserialization does not publish partial output");
    }

    // Wrong-channel isolation, bypass/reset and deliberate solo at real Rack boundary.
    rack->reset();
    require(render(*rack, std::array { note(0x93) }) == 0.0, "wrong MIDI channel is silent");
    require(render(*rack, std::array { note(0x90) }) > 1.0e-6, "assigned MIDI channel sounds");
    controls(*rack, 0, true);
    require(render(*rack) == 0.0, "mute silences and resets sounding voice");
    controls(*rack, 0);
    require(render(*rack) == 0.0, "unmute does not resurrect a discarded voice");
    require(render(*rack, std::array { note(0x90) }) > 1.0e-6, "fresh note plays after unmute");
    rack->reset();
    controls(*rack, 1, false, true);
    require(render(*rack, std::array { note(0x90) }) == 0.0, "solo suppresses other slots");
    require(render(*rack, std::array { note(0x91) }) > 1.0e-6, "solo destination still plays");
    controls(*rack, 1);

    // Unload/reload must not inherit a held note or stale ownership.
    rack->reset();
    render(*rack, std::array { note(0x90) });
    const auto firstId = rack->state()[0].slotId;
    require(rack->loadModule(0, {}, nullptr, diagnostic), "slot unload succeeds");
    require(render(*rack) == 0.0 && rack->state()[0].slotId == firstId,
            "unload is silent and keeps SlotId");
    require(rack->loadModule(0, tests::referenceInstrumentId, nullptr, diagnostic),
            "reload succeeds");
    require(render(*rack) == 0.0, "new instance has no inherited voice");
    require(render(*rack, std::array { note(0x80) }) == 0.0, "old release cannot revive new instance");

    // Missing and newer-schema content are legal unresolved state, not malformed data.
    for (const bool newerPattern : { false, true }) {
        auto unresolved = valid;
        if (newerPattern) unresolved[0].patternSchemaVersion = VOX_PATTERN_SCHEMA_V1 + 1;
        else unresolved[0].instrumentId = "com.ultimavox.test-missing";
        unresolved[0].modulePayload = { std::byte { 0xde }, std::byte { 0xad } };
        unresolved[0].patternPayload = { std::byte { 0xbe }, std::byte { 0xef } };
        require(rack->replaceState(unresolved, nullptr, diagnostic), "unresolved state remains loadable");
        require(rack->descriptor(0) == nullptr, "unresolved state does not substitute instrument");
        require(rack->state()[0].modulePayload == unresolved[0].modulePayload
                    && rack->state()[0].patternPayload == unresolved[0].patternPayload,
                "unresolved opaque payload remains recoverable");
        auto decoded = valid;
        const auto saved = rack::state::serialize(rack->snapshotState());
        require(rack::state::deserialize(saved, decoded, diagnostic),
                "legal unresolved state parses");
        require(rack::state::serialize(decoded).toXmlString() == saved.toXmlString(),
                "unresolved state round-trips canonically");
    }
    require(rack->replaceState(valid, nullptr, diagnostic), "valid state restores");
    rack->setMacro(0, 0, 0.7f);
    const auto saved = rack->snapshotState();
    auto restored = std::make_unique<rack::Rack>(registry);
    require(restored->prepare({ 48000.0, 256, 2 })
                && restored->replaceState(saved, nullptr, diagnostic), "fresh rack restores");
    require(snapshot(*restored) == snapshot(*rack), "snapshot preserves IDs/routing/macros");
    require(render(*restored) == 0.0, "project state does not serialize transient voices");
    const auto withNote = render(*restored, std::array { note(0x90) });
    require(withNote > 1.0e-6, "restored rack responds to fresh MIDI");
    require(restored->prepare({ 44100.0, 256, 2 }), "sample-rate reprepare succeeds");
    require(render(*restored) == 0.0, "reprepare clears transient voices");
    require(render(*restored, std::array { note(0x90) }) > 1.0e-6,
            "reprepared rack responds normally");

    const auto beforeBadCall = snapshot(*restored);
    require(!restored->loadModule(0, "Bad/Instrument", nullptr, diagnostic),
            "module load cannot publish an invalid InstrumentId");
    require(!restored->updatePatternState(0, std::string(257, 'a'), 1, {}),
            "pattern update cannot publish an oversized preset name");
    require(snapshot(*restored) == beforeBadCall, "rejected direct updates preserve state");

    for (const double invalidRate : { std::numeric_limits<double>::quiet_NaN(),
                                      std::numeric_limits<double>::infinity() }) {
        require(!restored->prepare({ invalidRate, 256, 2 }),
                "nonfinite sample rate is rejected before changing the live spec");
        require(render(*restored, std::array { note(0x90) }) > 1.0e-6,
                "invalid prepare input preserves the working rack");
    }
    for (const int fault : { 1, 2 }) {
        prepareFault = fault;
        require(!restored->prepare({ 96000.0, 1024, 2 }),
                "provider prepare failure/exception is reported");
        require(render(*restored, std::array { note(0x90) }) == 0.0,
                "partially prepared rack stays fail-closed");
        std::vector<float> left(1024, 0.25f), right(1024, 0.5f);
        std::array<float*, 2> outputs { left.data(), right.data() };
        const std::array thirdSlot { note(0x92) };
        restored->process(outputs, 1024, thirdSlot, 145.0, 0.0, true);
        require(std::all_of(left.begin(), left.end(), [](float x) { return x == 0.25f; })
                    && std::all_of(right.begin(), right.end(), [](float x) { return x == 0.5f; }),
                "failed reprepare cannot touch stale buffers or erase caller-owned mix");
        prepareFault = 0;
        require(restored->prepare({ 48000.0, 256, 2 }), "valid retry prepares every slot");
        require(render(*restored, thirdSlot) > 1.0e-6, "third slot recovers after failed second slot");
        require(snapshot(*restored) == beforeBadCall, "prepare failures preserve persistent state");
    }
    require(restored->prepare({ 48000.0, 256, 1 }), "mono prepare succeeds");
    require(render(*restored, std::array { note(0x90) }) == 0.0,
            "more output channels than prepared are rejected safely");
    std::array<float, 256> mono {};
    std::array<float*, 1> monoOutput { mono.data() };
    restored->process(monoOutput, 256, std::array { note(0x90) }, 145.0, 0.0, true);
    require(std::inner_product(mono.begin(), mono.end(), mono.begin(), 0.0) > 1.0e-6,
            "matching mono output shape plays correctly");

    // Prepare may overlap the audio callback. Controls stay immutable here;
    // this exercises the spec/buffer gate, not unrelated live macro mutation.
    require(restored->prepare({ 48000.0, 256, 2 }), "concurrent test starts prepared");
    std::array<rack::ProcessSlotControls, instrument::maxSlots> frozenControls {};
    for (std::size_t i = 0; i < frozenControls.size(); ++i)
        frozenControls[i] = rack::processControls(restored->state()[i]);
    std::atomic<bool> start { false }, finite { true };
    std::thread audio([&] {
        std::array<float, 256> left {}, right {};
        std::array<float*, 2> outputs { left.data(), right.data() };
        const std::array midi { note(0x90) };
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (int block = 0; block < 1200; ++block) {
            left.fill(0.0f); right.fill(0.0f);
            restored->process(outputs, 256, midi, 145.0, 0.0, true,
                              {}, instrument::invalidSlotId, frozenControls);
            for (std::size_t sample = 0; sample < left.size(); ++sample)
                if (!std::isfinite(left[sample]) || !std::isfinite(right[sample]))
                    finite.store(false, std::memory_order_relaxed);
        }
    });
    start.store(true, std::memory_order_release);
    bool prepared = true;
    for (int iteration = 0; iteration < 32; ++iteration)
        prepared = restored->prepare({ iteration % 2 ? 44100.0 : 48000.0,
            iteration % 2 ? 64u : 512u, iteration % 2 ? 1u : 2u }) && prepared;
    audio.join();
    require(prepared && finite.load(), "concurrent prepare/process completes with finite output");
    require(restored->prepare({ 48000.0, 256, 2 }), "rack prepares after concurrent churn");
    require(render(*restored, std::array { note(0x90) }) > 1.0e-6,
            "process counter/gate remains usable after early returns and churn");

    std::cout << "Rack lifecycle tests passed (" << checks << ")\n";
    return EXIT_SUCCESS;
}
