#include "engine/Timeline.h"
#include <cmath>
#include <cstdio>
#include <limits>

using namespace vox::fx::timeline;
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++failures; } };
    Curve curve = Curve::constant(0.5f);
    check(curve.valid() && curve.evaluate(0.6f) == 0.5f, "constant curve");
    curve.nodes[0] = {0, 0, Shape::linear}; curve.nodes[1] = {1, 1, Shape::linear};
    check(curve.valid() && std::abs(curve.evaluate(0.25f) - 0.25f) < 1e-6f, "linear");
    curve.nodes[0].shape = Shape::smooth;
    check(std::abs(curve.evaluate(0.25f) - 0.15625f) < 1e-6f, "smoothstep");
    curve.nodes[0].shape = Shape::exponential;
    check(std::abs(curve.evaluate(0.25f) - 0.0625f) < 1e-6f, "exponential ease");
    curve.nodes[0].shape = Shape::hold;
    check(curve.evaluate(0.99f) == 0 && curve.evaluate(1) == 1, "hold then endpoint");
    curve.count = 3; curve.nodes[0] = {0,0,Shape::hold}; curve.nodes[1] = {0.5f,0.7f,Shape::linear}; curve.nodes[2] = {1,1,Shape::linear};
    check(curve.valid() && curve.evaluate(0.5f) == 0.7f, "right-continuity at node");
    curve.nodes[1].time = 0; check(!curve.valid(), "reject duplicate times");
    curve.nodes[1].time = 0.5f; curve.nodes[1].value = std::numeric_limits<float>::quiet_NaN();
    check(!curve.valid(), "reject NaN");
    curve = Curve::constant(0); curve.count = 17; check(!curve.valid(), "reject oversized curve");
    curve = Curve::constant(0); curve.nodes[0].shape = static_cast<Shape>(99); check(!curve.valid(), "reject unknown shape");
    LayerTime layer{0.25f, 0.5f}; float p = -1;
    check(layer.valid() && !layer.phaseAt(0.249, p) && layer.phaseAt(0.25, p) && p == 0, "layer start");
    check(layer.phaseAt(0.5, p) && p == 0.5f && !layer.phaseAt(0.75, p), "layer range");
    check(!LayerTime{0.7f,0.5f}.valid() && !LayerTime{0,0}.valid(), "invalid layer ranges");
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        for (double duration : {0.05, 4.0, 32.0}) {
            SecondsClock clock; check(clock.start(duration, rate), "seconds start");
            double phase = 0; std::uint64_t count = 0;
            while (clock.next(phase)) { check(phase >= 0 && phase < 1, "seconds phase range"); ++count; }
            check(count == static_cast<std::uint64_t>(std::ceil(duration * rate)), "exact seconds sample count");
        }
    }
    SecondsClock seconds; check(!seconds.start(0.049,48000) && !seconds.start(33,48000), "seconds bounds");
    BarsClock bars; check(bars.start(1,4,4,48000), "bars start");
    double phase = 0;
    for (int i = 0; i < 24000; ++i) check(bars.next(120, phase), "first half-second");
    check(std::abs(phase - (23999.0 / 96000.0)) < 1e-9, "bar phase before tempo change");
    check(bars.next(60, phase) && std::abs(phase - 0.25) < 1e-9, "tempo change phase continuity");
    check(!bars.next(std::numeric_limits<double>::quiet_NaN(), phase), "invalid tempo rejected");
    check(!bars.start(1,4,3,48000), "invalid time signature");
    std::printf("FX timeline: %d failure(s)\n", failures); return failures ? 1 : 0;
}
