#include "engine/NoiseRiser.h"
#include "engine/ProgramCodec.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* message) { if (!ok) { std::printf("FAIL: %s\n", message); ++failures; } };
    constexpr int block = 128;
    std::array<float, block> l{}, r{};
    for (int index = 0; index < 4; ++index) {
        auto engineOwner=std::make_unique<vox::fx::NoiseRiser>(); auto& engine=*engineOwner; engine.prepare(48000);
        vox::fx::NoiseProgram program;
        for (auto& layer : program.layers) layer.enabled = false;
        auto& layer = program.layers[static_cast<std::size_t>(index)];
        layer.enabled = true; layer.pan = index % 2 == 0 ? -1.0f : 1.0f;
        check(engine.setProgram(program) && engine.noteOn(1, 60, 1), "program and note accepted");
        engine.render(l.data(), r.data(), block);
        const auto& active = index % 2 == 0 ? l : r;
        const auto& silent = index % 2 == 0 ? r : l;
        check(std::any_of(active.begin(), active.end(), [](float x) { return x != 0; }), "each layer renders");
        check(std::all_of(silent.begin(), silent.end(), [](float x) { return x == 0; }), "layer pan");
    }
    {
        auto engineOwner=std::make_unique<vox::fx::NoiseRiser>(); auto& engine=*engineOwner; engine.prepare(48000);
        vox::fx::NoiseProgram program;
        for (int i = 0; i < 4; ++i) {
            auto& layer = program.layers[static_cast<std::size_t>(i)];
            layer.enabled = true; layer.timing = { i * 0.25f, 0.25f };
            layer.pan = i % 2 == 0 ? -1.0f : 1.0f;
        }
        check(engine.setProgram(program) && engine.noteOn(1, 60, 1, 0.4), "staggered program");
        for (int quarter = 0; quarter < 4; ++quarter) {
            // Each quarter is 4800 samples. Entire non-target channel must be silent.
            double activeEnergy = 0, otherEnergy = 0;
            for (int sample = 0; sample < 4800; ++sample) {
                float left = 0, right = 0; engine.render(&left, &right, 1);
                activeEnergy += quarter % 2 == 0 ? left * left : right * right;
                otherEnergy += quarter % 2 == 0 ? right * right : left * left;
            }
            check(activeEnergy > 1e-8 && otherEnergy == 0, "independent layer offsets and duration");
        }
    }
    {
        auto engineOwner=std::make_unique<vox::fx::NoiseRiser>(); auto controlOwner=std::make_unique<vox::fx::NoiseRiser>(); auto& engine=*engineOwner; auto& control=*controlOwner; engine.prepare(48000); control.prepare(48000);
        check(engine.noteOn(1, 60, 1) && control.noteOn(1, 60, 1), "snapshot test trigger");
        vox::fx::NoiseProgram silent; silent.layers[0].gain = 0;
        check(engine.setProgram(silent), "install subsequent program");
        std::array<float, block> l2{}, r2{};
        engine.render(l.data(), r.data(), block); control.render(l2.data(), r2.data(), block);
        check(l == l2 && r == r2, "active event retains snapshot");
        engine.prepare(48000); engine.noteOn(1, 60, 1); engine.render(l.data(), r.data(), block);
        check(std::all_of(l.begin(), l.end(), [](float x) { return x == 0; }), "subsequent event uses new program");
        silent.layers[0].gain = 2;
        check(!engine.setProgram(silent), "invalid program rejected");
    }
    {
        auto engineOwner=std::make_unique<vox::fx::NoiseRiser>(); auto& engine=*engineOwner; engine.prepare(48000);
        vox::fx::NoiseProgram program;
        program.layers[0].gainCurve = vox::fx::timeline::Curve::constant(0);
        check(engine.setProgram(program) && engine.noteOn(1,60,1), "zero gain curve program");
        engine.render(l.data(), r.data(), block);
        check(std::all_of(l.begin(), l.end(), [](float x) { return x == 0; }), "gain curve controls output");
    }
    {
        auto engine=std::make_unique<vox::fx::NoiseRiser>();engine->prepare(48000);
        vox::fx::NoiseProgram program;
        auto& curve=program.layers[0].panCurve;
        curve.nodes[0]={0,0,vox::fx::timeline::Shape::linear};curve.nodes[1]={1,1,vox::fx::timeline::Shape::linear};
        std::vector<std::byte> state;vox::fx::NoiseProgram decoded;
        check(vox::fx::programcodec::encode(program,state)&&vox::fx::programcodec::decode(state,decoded)&&
              decoded.layers[0].panCurve.evaluate(.25f)==.25f,"pan curve survives program codec");
        check(engine->setProgram(decoded)&&engine->noteOn(1,60,1,.4),"pan curve sweep event");
        double earlyL=0,earlyR=0,lateL=0,lateR=0;
        for(int sample=0;sample<19200;++sample){float left=0,right=0;engine->render(&left,&right,1);
            if(sample<2400){earlyL+=left*left;earlyR+=right*right;}
            if(sample>=16800){lateL+=left*left;lateR+=right*right;}}
        check(earlyL>earlyR*3&&lateR>lateL*3,"pan envelope moves audible energy left to right");
    }
    std::printf("FX layers: %d failure(s)\n", failures); return failures ? 1 : 0;
}
