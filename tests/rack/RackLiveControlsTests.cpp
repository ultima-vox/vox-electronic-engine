#include "rack/Rack.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>

namespace {
using namespace vstengine;
constexpr std::string_view testId = "com.ultimavox.test.plain-controls";
int checks {};
void require(bool ok, const char* message)
{
    ++checks;
    if (!ok) { std::cerr << "FAILED: " << message << '\n'; std::exit(EXIT_FAILURE); }
}
struct Probe {
    std::atomic<int> calls {}, blocks {};
    std::atomic<float> plain {};
    std::atomic<bool> inProcess {}, overlap {};
};
class TestInstrument final : public instrument::InstrumentInstance {
public:
    explicit TestInstrument(Probe& probe) : probe_(probe) {}
    bool prepare(const instrument::PrepareSpec&) override { return true; }
    void reset() noexcept override {}
    void suspend() noexcept override {}
    void resume() noexcept override {}
    void setBypassed(bool value) noexcept override { bypassed_ = value; }
    void process(const instrument::ProcessBlock& block) noexcept override
    {
        probe_.inProcess.store(true);
        // Widen the overlap window; test-only provider, never linked to plugin.
        std::this_thread::yield();
        for (auto* channel : block.outputs)
            std::fill_n(channel, block.sampleCount, bypassed_ ? 0.0f : value_ / 100.0f);
        probe_.blocks.fetch_add(1);
        probe_.inProcess.store(false);
    }
    bool setParameter(std::string_view id, float value) noexcept override
    {
        if (probe_.inProcess.load()) probe_.overlap.store(true);
        probe_.calls.fetch_add(1);
        probe_.plain.store(value);
        if (id != "gain" || !std::isfinite(value) || value < -20.0f || value > 20.0f)
            return false;
        value_ = value;
        return true;
    }
    bool loadState(std::uint32_t schema, std::span<const std::byte> payload) noexcept override
    { return schema == instrument::stateSchemaVersion && payload.empty(); }
    bool saveState(std::span<std::byte>, std::uint32_t& bytes) const noexcept override
    { bytes = 0; return true; }
    std::uint32_t latencySamples() const noexcept override { return 0; }
    std::uint32_t tailSamples() const noexcept override { return 0; }
private:
    Probe& probe_;
    float value_ {};
    bool bypassed_ {};
};
class TestProvider final : public instrument::InstrumentProvider {
public:
    explicit TestProvider(Probe& probe) : probe_(probe)
    {
        descriptor_.id = testId;
        descriptor_.providerId = "com.ultimavox.test-provider";
        descriptor_.name = "Plain control test";
        descriptor_.vendor = "Ultima Vox";
        descriptor_.tailPolicy = instrument::TailPolicy::none;
        descriptor_.budget = { 1, 2048, 64, 0, 0, 0, 0, 0, 4096 };
        descriptor_.parameters = {
            { "gain", "Gain", "dB", -20.0f, 20.0f, 0.0f, 5.0f,
              instrument::ParameterType::floating, "Output", {}, true, false, 0 }
        };
    }
    std::span<const instrument::InstrumentDescriptor> descriptors() const noexcept override
    { return { &descriptor_, 1 }; }
    std::unique_ptr<instrument::InstrumentInstance> create(
        std::string_view id, const instrument::CreateContext&) override
    { return id == testId ? std::make_unique<TestInstrument>(probe_) : nullptr; }
private:
    Probe& probe_;
    instrument::InstrumentDescriptor descriptor_;
};
float render(rack::Rack& rack,
             std::span<const rack::ProcessSlotControls> controls = {})
{
    std::array<float, 64> left {}, right {};
    std::array<float*, 2> outputs { left.data(), right.data() };
    rack.process(outputs, 64, {}, 145.0, 0.0, true, {}, instrument::invalidSlotId, controls);
    return left[0];
}
} // namespace

int main()
{
    using namespace vstengine;
    Probe probe;
    instrument::InstrumentRegistry registry;
    std::string diagnostic;
    require(registry.registerProvider(std::make_unique<TestProvider>(probe), diagnostic),
            "nonunit stepped parameter provider registers");
    auto rack = std::make_unique<rack::Rack>(registry);
    require(rack->prepare({ 48000.0, 64, 2 })
                && rack->loadModule(0, testId, nullptr, diagnostic), "rack loads test instrument");
    const int before = probe.calls.load();
    rack->setMacro(0, 0, 0.81f);
    require(probe.calls.load() == before, "setMacro must not call DSP on control thread");
    require(rack->snapshotState()[0].macros[0] == 0.81f, "normalized value persists immediately");
    require(std::abs(render(*rack) - 0.1f) < 1.0e-6f && probe.plain.load() == 10.0f,
            "next default block applies normalized macro as stepped plain value");
    rack->setMacro(0, 0, -1.0f);
    require(render(*rack) == -0.2f && probe.plain.load() == -20.0f, "lower macro bound clamps");
    rack->setMacro(0, 0, 2.0f);
    require(render(*rack) == 0.2f && probe.plain.load() == 20.0f, "upper macro bound clamps");
    rack->setMacro(0, 0, std::numeric_limits<float>::quiet_NaN());
    rack->setMacro(0, 0, std::numeric_limits<float>::infinity());
    rack->setMacro(instrument::maxSlots, 0, 0.0f);
    rack->setMacro(0, instrument::macrosPerSlot, 0.0f);
    require(rack->snapshotState()[0].macros[0] == 1.0f, "invalid macro calls preserve state");

    std::array<rack::ProcessSlotControls, instrument::maxSlots> explicitControls;
    const auto states = rack->snapshotState();
    for (std::size_t i = 0; i < states.size(); ++i)
        explicitControls[i] = rack::processControls(states[i]);
    explicitControls[0].macros[0] = 0.0f;
    require(render(*rack, explicitControls) == -0.2f, "host block snapshot overrides direct default");
    require(render(*rack) == 0.2f, "explicit host controls do not overwrite persistent defaults");
    rack->updateControls(0, {}, true, true, false, false, 1.0f, 0.0f);
    require(render(*rack) == 0.0f, "direct mute reaches next default block");
    rack->updateControls(0, {}, true, false, false, false, 0.5f, 1.0f);
    require(render(*rack) == 0.0f, "direct pan reaches left output");
    rack->updateControls(0, {}, true, false, false, false, 0.5f, 0.0f);
    require(render(*rack) == 0.1f, "direct level reaches audio output");

    // One audio consumer; control methods are serialized internally. No raw
    // borrowed state/descriptor reads occur concurrently in this workload.
    std::atomic<bool> start {}, stop {}, finite { true };
    std::atomic<int> attempts {};
    std::thread audio([&] {
        while (!start.load()) std::this_thread::yield();
        while (!stop.load()) {
            if (!std::isfinite(render(*rack))) finite.store(false);
            attempts.fetch_add(1);
        }
    });
    start.store(true);
    while (probe.blocks.load() < 32) std::this_thread::yield();
    for (int i = 0; i < 2000; ++i) {
        rack->setMacro(0, 0, static_cast<float>(i % 101) / 100.0f);
        rack->updateControls(0, {}, true, i % 3 == 0, false, false, 1.0f, 0.0f);
    }
    stop.store(true);
    audio.join();
    require(attempts.load() > 0 && finite.load() && !probe.overlap.load(),
            "direct control churn does not enter live DSP or produce nonfinite output");

    // UI runtime metadata can be polled while modules are replaced. This does
    // not certify borrowed state()/descriptor() for concurrent mutation.
    stop.store(false);
    std::atomic<bool> validMetadata { true };
    std::atomic<int> polls {};
    std::thread observer([&] {
        while (!stop.load()) {
            const auto state = rack->runtimeState()[0];
            if (state.resolution != instrument::ResolutionStatus::resolved
                && state.resolution != instrument::ResolutionStatus::missingModule)
                validMetadata.store(false);
            if (state.ownedNotes != 0) validMetadata.store(false);
            polls.fetch_add(1);
        }
    });
    while (polls.load() == 0) std::this_thread::yield();
    bool loads = true;
    for (int i = 0; i < 100; ++i)
        loads = rack->loadModule(0, i % 2 ? testId : "", nullptr, diagnostic) && loads;
    stop.store(true);
    observer.join();
    require(loads && polls.load() > 0 && validMetadata.load(), "runtime snapshots survive module churn");
    rack->setMacro(0, 0, 0.75f);
    rack->updateControls(0, {}, true, false, false, false, 1.0f, 0.0f);
    require(render(*rack) == 0.1f, "rack remains usable after churn");
    std::cout << "Rack live controls tests passed (" << checks << ")\n";
}
