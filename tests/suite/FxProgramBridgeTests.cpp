#include "engine/ProgramBridge.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++failures; } };
    vox::fx::ProgramBridge bridge;
    vox::fx::NoiseProgram p, output;
    std::uint64_t sequence = 0;
    check(!bridge.consumeLatest(output, sequence), "initially empty");
    p.layers[0].gain = -1; check(!bridge.publish(p), "reject invalid program");
    p.layers[0].gain = 0.1f; check(bridge.publish(p), "publish first");
    p.layers[0].gain = 0.2f; check(bridge.publish(p), "publish second");
    p.layers[0].gain = 0.3f; check(bridge.publish(p), "publish third");
    check(!bridge.publish(p), "bounded queue reports full");
    check(bridge.consumeLatest(output, sequence) && sequence == 3 && output.layers[0].gain == 0.3f,
          "newest complete snapshot wins");
    check(!bridge.consumeLatest(output, sequence), "older snapshots retired");
    check(bridge.publish(p), "retired slots reusable");
    check(bridge.consumeLatest(output, sequence) && sequence == 4, "monotonic sequence");

    {
        vox::fx::NoiseRiser engine; engine.prepare(48000);
        p.layers[0].gain = 0;
        check(bridge.publish(p) && bridge.consumeLatest(output, sequence) && engine.setProgram(output),
              "publish prepared settings to engine");
        check(engine.noteOn(1, 60, 1), "trigger updated program");
        float left[128] {}, right[128] {};
        engine.render(left, right, 128);
        bool silent = true;
        for (float sample : left) silent = silent && sample == 0;
        check(silent, "published program affects DSP");
    }
    constexpr int iterations = 50000;
    vox::fx::ProgramBridge concurrent;
    std::atomic<bool> done { false };
    std::thread producer([&] {
        vox::fx::NoiseProgram next;
        for (int i = 1; i <= iterations; ++i) {
            next.layers[0].gain = static_cast<float>(i) / iterations;
            next.layers[1].gain = next.layers[0].gain;
            while (!concurrent.publish(next)) std::this_thread::yield();
        }
        done.store(true, std::memory_order_release);
    });
    std::uint64_t last = 0;
    do {
        if (concurrent.consumeLatest(output, sequence)) {
            check(sequence > last, "sequences strictly increase");
            check(output.layers[0].gain == output.layers[1].gain, "snapshot is not torn");
            last = sequence;
        } else std::this_thread::yield();
    } while (!done.load(std::memory_order_acquire) || last < iterations);
    producer.join();
    check(last == iterations && output.layers[0].gain == 1.0f, "final edit delivered");
    std::printf("FX Program bridge: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
