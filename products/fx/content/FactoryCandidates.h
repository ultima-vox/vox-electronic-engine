#pragma once

#include "engine/OfflineRender.h"
#include <string_view>

namespace vox::fx::content {
// Review catalog, separate from the existing procedural Seed selector.
// Version 1: immutable stable IDs, handwritten recipes, no external assets.
// Candidate status cannot be turned into a factory release by the loader.
enum class Role { impact, riser, fall, sweep, zap, rhythm, atmosphere, stinger };
struct Entry {
    std::string_view id, name, style, mood;
    Role role;
    PlayMode mode;
    bool pitchedSource;
    std::uint8_t note, bars;
    float seconds;
    double bpm;
    std::uint32_t revision = 1;
};
inline constexpr unsigned schemaVersion = 1;
inline constexpr std::string_view provenance = "Original hand-authored VOX synth recipe; no external assets; distribution approval pending";
inline constexpr Entry reviewEntries[] {
    {"voxfx.impact.iron-drop", "Iron Drop", "Peak-Time Tech", "Heavy", Role::impact, PlayMode::oneShot, true, 38, 0, .7f, 138},
    {"voxfx.riser.first-light", "First Light", "Uplifting", "Bright", Role::riser, PlayMode::transition, true, 62, 4, 4, 138},
    {"voxfx.fall.blue-hour", "Blue Hour", "Progressive", "Melancholic", Role::fall, PlayMode::oneShot, true, 57, 0, 2.6f, 128},
    {"voxfx.sweep.salt-air", "Salt Air", "Balearic", "Airy", Role::sweep, PlayMode::loop, false, 60, 0, 1.8f, 124},
    {"voxfx.zap.acid-spore", "Acid Spore", "Goa", "Acidic", Role::zap, PlayMode::gate, true, 58, 0, .38f, 145},
    {"voxfx.rhythm.neon-fault", "Neon Fault", "Hi-Tech", "Extreme", Role::rhythm, PlayMode::sequence, true, 60, 0, .13f, 175},
    {"voxfx.atmosphere.root-system", "Root System", "Dark Forest", "Organic", Role::atmosphere, PlayMode::continuous, true, 43, 0, 5, 150},
    {"voxfx.stinger.sun-glass", "Sun Glass", "Anthem", "Euphoric", Role::stinger, PlayMode::oneShot, true, 69, 0, .9f, 136}
};

inline offline::Capture makePreview(const Entry& e, std::uint32_t rate) {
    using F=NoiseFamily;
    using E=effects::Type;
    using S=timeline::Shape;
    offline::Capture result;
    auto& p=result.program;auto& s=result.settings;
    p.mode=e.mode;p.bars=e.bars;s.duration=e.seconds;s.note=e.note;s.tempo=e.bpm;s.sampleRate=rate;
    const auto layer=[&](std::size_t i,F family,float gain,float pan,float offset,float span){
        auto& l=p.layers[i]; l.enabled=true;l.family=family;l.gain=gain;l.pan=pan;l.timing={offset,span};
    };
    const auto envelope=[](float first,float middle,float last,float center){
        timeline::Curve c;c.count=3;c.nodes[0]={0,first,S::smooth};c.nodes[1]={center,middle,S::exponential};c.nodes[2]={1,last,S::linear};return c;
    };
    switch(e.role){
        case Role::impact:
            layer(0,F::impact,1,0,0,1);layer(1,F::metallic,.38f,.2f,0,.7f);
            layer(2,F::downlifter,.26f,-.25f,.08f,.85f);p.layers[2].gainCurve=envelope(.35f,1,0,.18f);
            s.effects[0]={E::drive,.45f,.62f};s.effects[1]={E::reverb,.14f,.3f};break;
        case Role::riser:
            layer(0,F::shepard,.78f,0,0,1);layer(1,F::riser,.68f,-.25f,0,1);
            layer(2,F::laser,.46f,.34f,.81f,.18f);layer(3,F::impact,.65f,0,.972f,.028f);
            p.amplitudeCurve=envelope(.14f,.72f,1,.66f);s.effects[0]={E::chorus,.22f,.35f};break;
        case Role::fall:
            layer(0,F::downlifter,.88f,0,0,1);layer(1,F::texture,.38f,.4f,.09f,.81f);
            p.layers[1].pitchCurve=envelope(.6f,.46f,.31f,.29f);p.amplitudeCurve=envelope(.9f,.7f,0,.33f);
            s.effects[0]={E::delay,.16f,.22f};break;
        case Role::sweep:
            layer(0,F::whoosh,.87f,0,0,1);layer(1,F::riser,.35f,-.38f,.1f,.64f);
            p.panoramaCurve=envelope(.12f,.72f,.36f,.53f);p.amplitudeCurve=envelope(.1f,1,.16f,.48f);
            s.effects[0]={E::phaser,.29f,.61f};break;
        case Role::zap:
            layer(0,F::squelch,1,0,0,1);layer(1,F::laser,.48f,-.17f,.12f,.72f);
            p.layers[1].pitchCurve=envelope(.78f,.48f,.3f,.26f);
            s.effects[0]={E::drive,.62f,.49f};s.effects[1]={E::delay,.22f,.12f};break;
        case Role::rhythm: {
            layer(0,F::laser,.86f,-.2f,0,1);layer(1,F::glitch,.7f,.26f,0,.96f);
            auto& pattern=result.pattern;pattern.bars=2;pattern.swing=.3f;pattern.seed=220019u;
            constexpr unsigned positions[]{0,3,6,7,10,14,16,19,20,25,27,31};
            for(unsigned i=0;i<std::size(positions);++i)pattern.steps[positions[i]]={true,static_cast<std::uint8_t>(46u+(i*7u)%19u),static_cast<std::uint8_t>(95u+i%5u*7u),100,static_cast<std::uint8_t>(i%3u+1u)};
            s.effects[0]={E::bitcrush,.37f,.42f};break;
        }
        case Role::atmosphere:
            layer(0,F::granular,.76f,.32f,0,1);layer(1,F::formant,.56f,-.34f,.09f,.91f);
            layer(2,F::texture,.52f,0,0,1);p.cutoffCurve=envelope(.32f,.62f,.41f,.55f);
            s.effects[0]={E::lowpass,.55f,.43f};s.effects[1]={E::reverb,.38f,.72f};break;
        case Role::stinger:
            layer(0,F::metallic,.78f,0,0,1);layer(1,F::shepard,.54f,-.36f,.21f,.7f);
            p.layers[1].gainCurve=envelope(.1f,.85f,0,.35f);p.layers[1].pitchCurve=envelope(.75f,.7f,.64f,.44f);
            s.effects[0]={E::reverb,.24f,.52f};break;
    }
    return result;
}
} // namespace vox::fx::content
