#pragma once

#include "engine/OfflineRender.h"
#include <string_view>

namespace vox::fx::content {
// Review catalog, separate from the existing procedural Seed selector.
// Version 2 audit manifest: immutable stable IDs, handwritten recipes, no external assets.
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
    bool secondWave = false;
    unsigned authored = 0;
    std::string_view useCase{};
};
inline constexpr unsigned schemaVersion = 2;
inline constexpr std::string_view provenance = "Original hand-authored VOX synth recipe; no external assets; distribution approval pending";
inline constexpr std::string_view useCase(const Entry& e) noexcept {
    constexpr std::string_view byRole[]{"Downbeat accent","Build into a transition","Release after a drop","Movement and flyby",
        "Short fill accent","Rhythmic fill","Breakdown bed","Section marker"};
    return e.useCase.empty()?byRole[static_cast<unsigned>(e.role)]:e.useCase;
}
inline constexpr const char* scaleName(source::Scale scale) noexcept {
    constexpr const char* names[]{"Chromatic","Major","Minor","Dorian","Phrygian"};
    return names[static_cast<unsigned>(scale)];
}
inline constexpr Entry firstReviewEntries[] {
    {"voxfx.impact.iron-drop", "Iron Drop", "Peak-Time Tech", "Heavy", Role::impact, PlayMode::oneShot, true, 38, 0, .7f, 138},
    {"voxfx.riser.first-light", "First Light", "Uplifting", "Bright", Role::riser, PlayMode::transition, true, 62, 4, 4, 138},
    {"voxfx.fall.blue-hour", "Blue Hour", "Progressive", "Melancholic", Role::fall, PlayMode::oneShot, true, 57, 0, 2.6f, 128},
    {"voxfx.sweep.salt-air", "Salt Air", "Balearic", "Airy", Role::sweep, PlayMode::loop, true, 60, 0, 1.8f, 124},
    {"voxfx.zap.acid-spore", "Acid Spore", "Goa", "Acidic", Role::zap, PlayMode::gate, true, 58, 0, .38f, 145},
    {"voxfx.rhythm.neon-fault", "Neon Fault", "Hi-Tech", "Extreme", Role::rhythm, PlayMode::sequence, true, 60, 0, .13f, 175},
    {"voxfx.atmosphere.root-system", "Root System", "Dark Forest", "Organic", Role::atmosphere, PlayMode::continuous, true, 43, 0, 5, 150},
    {"voxfx.stinger.sun-glass", "Sun Glass", "Anthem", "Euphoric", Role::stinger, PlayMode::oneShot, true, 69, 0, .9f, 136},
    {"voxfx.impact.subduction", "Subduction", "Dark Psy", "Subterranean", Role::impact, PlayMode::oneShot, true, 41, 0, 1.1f, 156, 1, true},
    {"voxfx.riser.vernal-arc", "Vernal Arc", "Daylight Trance", "Open", Role::riser, PlayMode::transition, true, 65, 2, 3, 140, 1, true},
    {"voxfx.fall.ash-tide", "Ash Tide", "Dark Trance", "Hollow", Role::fall, PlayMode::oneShot, false, 48, 0, 1.7f, 140, 1, true},
    {"voxfx.sweep.cedar-probe", "Cedar Probe", "Forest Psy", "Dry", Role::sweep, PlayMode::gate, true, 59, 0, 1.3f, 150, 1, true},
    {"voxfx.zap.pixel-wasp", "Pixel Wasp", "Hi-Tech", "Angular", Role::zap, PlayMode::oneShot, true, 73, 0, .19f, 178, 1, true},
    {"voxfx.rhythm.sand-code", "Sand Code", "Goa", "Coiled", Role::rhythm, PlayMode::sequence, true, 55, 0, .21f, 146, 1, true},
    {"voxfx.atmosphere.cloud-temple", "Cloud Temple", "Uplifting", "Weightless", Role::atmosphere, PlayMode::continuous, true, 69, 0, 4.5f, 138, 1, true},
    {"voxfx.stinger.copper-warning", "Copper Warning", "Dark Psy", "Ominous", Role::stinger, PlayMode::oneShot, true, 51, 0, .85f, 152, 1, true},
    {"voxfx.impact.thunder-veil","Thunder Veil","Daylight Trance","Expansive",Role::impact,PlayMode::oneShot,true,48,0,1.5f,138,1,false,1},
    {"voxfx.impact.oxide-bloom","Oxide Bloom","Industrial Psy","Gritty",Role::impact,PlayMode::gate,true,35,0,.58f,150,1,false,2},
    {"voxfx.riser.cobalt-choir","Cobalt Choir","Uplifting","Radiant",Role::riser,PlayMode::transition,true,67,8,4,140,1,false,3},
    {"voxfx.riser.pressure-front","Pressure Front","Progressive Tech","Tense",Role::riser,PlayMode::oneShot,false,60,0,2.8f,131,1,false,4},
    {"voxfx.fall.halcyon-wake","Halcyon Wake","Daylight Trance","Nostalgic",Role::fall,PlayMode::oneShot,true,64,0,2.4f,134,1,false,5},
    {"voxfx.fall.glass-elevator","Glass Elevator","Hi-Tech","Brittle",Role::fall,PlayMode::oneShot,true,53,0,.77f,180,1,false,6},
    {"voxfx.sweep.velvet-conduit","Velvet Conduit","Progressive Tech","Liquid",Role::sweep,PlayMode::loop,true,58,0,1.25f,132,1,false,7},
    {"voxfx.sweep.monsoon-edge","Monsoon Edge","Goa Psy","Restless",Role::sweep,PlayMode::gate,false,60,0,1.5f,146,1,false,8},
    {"voxfx.zap.mantis-antenna","Mantis Antenna","Forest Psy","Insectile",Role::zap,PlayMode::gate,true,71,0,.43f,152,1,false,9},
    {"voxfx.zap.prism-arc","Prism Arc","Anthem","Luminous",Role::zap,PlayMode::oneShot,true,81,0,.28f,139,1,false,10},
    {"voxfx.rhythm.rusted-bell-grid","Rusted Bell Grid","Dark Psy","Mechanical",Role::rhythm,PlayMode::sequence,true,50,0,.09f,158,1,false,11},
    {"voxfx.rhythm.lantern-bounce","Lantern Bounce","Daylight Trance","Playful",Role::rhythm,PlayMode::sequence,true,60,0,.27f,136,1,false,12},
    {"voxfx.atmosphere.lichen-cavern","Lichen Cavern","Forest Psy","Damp",Role::atmosphere,PlayMode::continuous,true,46,0,4.8f,150,1,false,13},
    {"voxfx.atmosphere.horizon-vapour","Horizon Vapour","Balearic Trance","Mist",Role::atmosphere,PlayMode::continuous,false,60,0,4.3f,126,1,false,14},
    {"voxfx.stinger.solar-insignia","Solar Insignia","Uplifting","Triumphant",Role::stinger,PlayMode::oneShot,true,72,0,.85f,142,1,false,15},
    {"voxfx.stinger.null-beacon","Null Beacon","Dark Trance","Uneasy",Role::stinger,PlayMode::oneShot,true,55,0,1.2f,147,1,false,16},
};

// Hand-specified combinations. Each row has independently authored source,
// event staging, envelope, filter and effects choices (no seed expansion).
struct AuthoredLayer {
    NoiseFamily family=NoiseFamily::riser;
    float gain=0,pan=0,start=0,span=1,transpose=0;
    source::Filter filter{};
};
struct AuthoredRecipe {
    std::array<AuthoredLayer,3> layers;
    std::array<float,4> envelope; // start, crest, end, crest position
    std::array<effects::Settings,3> fx;
    source::Tonality key{};
    std::array<float,2> pitch{.5f,.5f};
};
namespace detail {
using F=NoiseFamily;using E=effects::Type;using T=source::FilterType;using K=source::Scale;
inline constexpr AuthoredRecipe authored[]{
    {{{{F::impact,.98f,0,0,.65f,0,{T::lowpass,2600,.7f,2}},{F::whoosh,.52f,.28f,.06f,.81f,7,{T::highpass,170,.707f,2}},{F::texture,.32f,-.46f,.24f,.76f,12,{T::lowpass,5300,.62f,2}}}},
      {.91f,.83f,0,.19f},{{{E::chorus,.24f,.37f},{E::reverb,.24f,.52f},{}}},{K::major,0},{.58f,.43f}},
    {{{{F::metallic,.86f,-.04f,0,1,-5,{T::bandpass,970,.94f,2}},{F::squelch,.47f,.41f,.13f,.71f,0,{T::lowpass,4600,1.2f,2}},{F::impact,.75f,0,0,.67f,-12,{}}}},
      {.89f,.74f,.21f,.12f},{{{E::drive,.53f,.51f},{E::bitcrush,.17f,.44f},{E::delay,.11f,.17f}}},{K::phrygian,11},{.56f,.31f}},
    {{{{F::formant,.72f,-.3f,0,1,0,{T::highpass,93,.68f,2}},{F::texture,.65f,.35f,.12f,.88f,7,{T::lowpass,4200,.72f,2}},{F::shepard,.48f,0,.31f,.69f,12,{T::lowpass,6500,.66f,2}}}},
      {.04f,.54f,1,.61f},{{{E::chorus,.22f,.29f},{E::reverb,.17f,.57f},{}}},{K::major,7},{.45f,.64f}},
    {{{{F::riser,.77f,-.31f,0,1,0,{T::bandpass,650,.81f,2}},{F::riser,.64f,.24f,.26f,.74f,0,{T::highpass,1700,.67f,4}},{F::riser,.51f,0,.55f,.45f,0,{T::notch,4100,.8f,2}}}},
      {.1f,.73f,1,.71f},{{{E::phaser,.22f,.48f},{E::delay,.15f,.25f},{}}},{K::chromatic,0}},
    {{{{F::downlifter,.78f,0,0,1,0,{T::lowpass,2700,.7f,2}},{F::texture,.48f,-.34f,.12f,.88f,-5,{T::highpass,144,.7f,2}},{F::whoosh,.42f,.49f,.32f,.65f,7,{T::lowpass,1700,.69f,2}}}},
      {1,.68f,.02f,.39f},{{{E::delay,.19f,.32f},{E::reverb,.23f,.37f},{}}},{K::dorian,4},{.61f,.31f}},
    {{{{F::glitch,.88f,.29f,0,.76f,0,{T::highpass,220,.74f,2}},{F::laser,.7f,-.37f,.18f,.82f,-12,{T::lowpass,2100,.85f,2}},{F::metallic,.45f,0,.25f,.55f,7,{T::notch,810,.87f,2}}}},
      {.89f,.66f,0,.28f},{{{E::bitcrush,.24f,.62f},{E::flanger,.15f,.35f},{E::delay,.14f,.15f}}},{K::minor,5},{.74f,.26f}},
    {{{{F::texture,.67f,-.21f,0,1,0,{T::bandpass,480,.7f,2}},{F::whoosh,.59f,.37f,.04f,.94f,-5,{T::highpass,185,.74f,2}},{F::formant,.38f,.02f,.22f,.71f,12,{T::notch,1650,.87f,2}}}},
      {.15f,.8f,.12f,.47f},{{{E::phaser,.38f,.43f},{E::chorus,.2f,.26f},{}}},{K::minor,10},{.43f,.59f}},
    {{{{F::riser,.88f,-.25f,0,1,0,{T::bandpass,1400,1.1f,2}},{F::riser,.57f,.43f,.05f,.89f,0,{T::lowpass,480,.71f,4}},{F::riser,.39f,-.03f,.41f,.59f,0,{T::highpass,5000,.68f,2}}}},
      {.17f,.87f,.13f,.53f},{{{E::flanger,.18f,.5f},{E::ring,.18f,.29f},{}}},{K::chromatic,0}},
    {{{{F::squelch,.9f,-.04f,0,1,0,{T::bandpass,1650,1.15f,2}},{F::formant,.59f,.44f,.1f,.9f,-12,{T::highpass,290,.68f,2}},{F::glitch,.35f,-.21f,.21f,.65f,0,{T::lowpass,5000,.74f,2}}}},
      {.92f,.68f,.24f,.22f},{{{E::ring,.23f,.28f},{E::delay,.13f,.2f},{}}},{K::phrygian,11},{.48f,.76f}},
    {{{{F::laser,.82f,-.19f,0,.9f,0,{T::highpass,630,.68f,2}},{F::metallic,.54f,.29f,.04f,.96f,7,{T::lowpass,6300,.73f,2}},{F::shepard,.51f,.02f,.32f,.68f,-12,{T::lowpass,3700,.72f,2}}}},
      {.9f,.75f,0,.19f},{{{E::chorus,.23f,.34f},{E::reverb,.2f,.43f},{}}},{K::major,9},{.68f,.43f}},
    {{{{F::metallic,.81f,0,0,.93f,0,{T::lowpass,2950,.73f,2}},{F::impact,.61f,-.15f,0,1,-12,{T::lowpass,1600,.68f,2}},{F::glitch,.47f,.43f,.15f,.84f,5,{T::highpass,690,.72f,2}}}},
      {.94f,.7f,.02f,.36f},{{{E::drive,.38f,.48f},{E::gate,.17f,.39f},{E::delay,.12f,.24f}}},{K::phrygian,2},{.57f,.44f}},
    {{{{F::texture,.78f,-.16f,0,1,0,{T::highpass,100,.7f,2}},{F::laser,.44f,.28f,.14f,.76f,12,{T::lowpass,3500,.72f,4}},{F::impact,.49f,0,0,.48f,0,{T::lowpass,900,.72f,2}}}},
      {.54f,.88f,.03f,.32f},{{{E::chorus,.17f,.25f},{E::delay,.16f,.22f},{}}},{K::major,0},{.51f,.47f}},
    {{{{F::granular,.74f,-.32f,0,1,-12,{T::bandpass,575,.88f,2}},{F::formant,.57f,.24f,.14f,.81f,0,{T::notch,900,.72f,2}},{F::squelch,.36f,.1f,.46f,.5f,7,{T::lowpass,1900,.71f,2}}}},
      {.09f,.75f,.23f,.58f},{{{E::flanger,.21f,.41f},{E::reverb,.31f,.62f},{}}},{K::phrygian,10},{.45f,.55f}},
    {{{{F::riser,.62f,.19f,0,1,0,{T::lowpass,730,.65f,2}},{F::riser,.79f,-.22f,.19f,.81f,0,{T::bandpass,1830,.72f,2}},{F::riser,.46f,.41f,.45f,.55f,0,{T::highpass,4600,.69f,2}}}},
      {.08f,.71f,.12f,.65f},{{{E::chorus,.31f,.31f},{E::reverb,.31f,.71f},{}}},{K::chromatic,0}},
    {{{{F::formant,.62f,-.29f,0,.87f,0,{T::highpass,170,.72f,2}},{F::shepard,.5f,.25f,.07f,.93f,7,{T::lowpass,5100,.66f,2}},{F::impact,.77f,0,.48f,.48f,-12,{T::lowpass,7800,.71f,2}}}},
      {.51f,.91f,.03f,.52f},{{{E::chorus,.24f,.28f},{E::reverb,.21f,.48f},{}}},{K::major,0},{.49f,.59f}},
    {{{{F::downlifter,.68f,-.23f,0,.91f,0,{T::bandpass,860,.88f,2}},{F::formant,.52f,.33f,.07f,.81f,-5,{T::notch,470,.7f,2}},{F::glitch,.62f,.04f,.27f,.52f,0,{T::highpass,620,.72f,2}}}},
      {.82f,.68f,0,.33f},{{{E::ring,.27f,.4f},{E::delay,.17f,.28f},{E::reverb,.13f,.42f}}},{K::minor,7},{.59f,.29f}},
};
#include "ReviewWaveARecipes.h"
#include "ReviewWaveACompletion.h"
inline void applyWaveItem(offline::Capture& result,const WaveItem& item){
    auto& p=result.program;
    for(std::size_t i=0;i<item.layers.size();++i){
        const auto& r=item.layers[i];if(r.gain==0)continue;
        auto& l=p.layers[i];l.enabled=true;l.family=r.family;l.gain=r.gain;l.pan=r.pan;l.timing={r.start,r.span};
        l.transpose=r.transpose;l.filters[0]=r.filter;l.character=r.character;
    }
    const auto curve=[](Contour c){
        using S=timeline::Shape;
        timeline::Curve result;result.count=3;result.nodes[0]={0,c.start,S::smooth};
        result.nodes[1]={c.pivot,c.middle,S::exponential};result.nodes[2]={1,c.end,S::linear};return result;
    };
    p.amplitudeCurve=curve(item.amp);p.pitchCurve=curve(item.pitch);p.panoramaCurve=curve(item.pan);
    p.tonality=item.key;result.settings.effects=item.fx;
    if(item.entry.mode==PlayMode::sequence){
        auto& pattern=result.pattern;
        pattern.bars=item.beat.bars;pattern.grid=item.beat.grid;pattern.swing=item.beat.swing;
        for(const auto& h:item.beat.hits)if(h.velocity!=0)pattern.steps[h.step]={true,h.note,h.velocity,h.chance,h.ratchet};
    }
}
} // namespace detail

inline constexpr auto reviewEntries=[] {
    std::array<Entry,std::size(firstReviewEntries)+std::size(detail::waveA)+std::size(detail::completion::items)> result{};
    std::copy(std::begin(firstReviewEntries),std::end(firstReviewEntries),result.begin());
    for(std::size_t i=0;i<std::size(detail::waveA);++i){
        auto e=detail::waveA[i].entry;
        e.authored=static_cast<unsigned>(std::size(detail::authored)+1+i);
        result[std::size(firstReviewEntries)+i]=e;
    }
    for(std::size_t i=0;i<std::size(detail::completion::items);++i){
        auto e=detail::completion::items[i].entry;
        e.authored=static_cast<unsigned>(std::size(detail::authored)+std::size(detail::waveA)+1+i);
        result[std::size(firstReviewEntries)+std::size(detail::waveA)+i]=e;
    }
    return result;
}();


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
    if(e.authored>=1&&e.authored<=std::size(detail::authored)){
        const auto& recipe=detail::authored[e.authored-1];
        for(unsigned i=0;i<recipe.layers.size();++i){const auto& r=recipe.layers[i];
            layer(i,r.family,r.gain,r.pan,r.start,r.span);p.layers[i].transpose=r.transpose;p.layers[i].filters[0]=r.filter;}
        constexpr float signature[]{.26f,.34f,.42f,-.35f,.30f,.38f,-.30f,.24f,.43f,-.25f,.33f,.20f,-.24f,-.40f,.33f,.37f};
        static_assert(std::size(signature)==std::size(detail::authored));
        p.layers[0].character=signature[e.authored-1];
        const auto& a=recipe.envelope;p.amplitudeCurve=envelope(a[0],a[1],a[2],a[3]);
        p.pitchCurve.nodes[0].value=recipe.pitch[0];p.pitchCurve.nodes[1].value=recipe.pitch[1];
        p.tonality=recipe.key;std::copy(recipe.fx.begin(),recipe.fx.end(),s.effects.begin());
        if(e.authored==11){
            auto& beat=result.pattern;beat.bars=2;beat.grid=pattern::Grid::thirtysecond;beat.swing=.23f;beat.seed=700021;
            constexpr unsigned positions[]{0,3,8,11,17,21,24,29,32,38,40,45,48,55,60,63};
            constexpr std::uint8_t notes[]{50,57,50,51,50,60,53,50,50,53,50,51,62,50,53,57};
            for(unsigned i=0;i<std::size(positions);++i)beat.steps[positions[i]]={true,notes[i],static_cast<std::uint8_t>(i%4==0?118:92),static_cast<std::uint8_t>(i%4==0?100:88),static_cast<std::uint8_t>(i%5==1?3:1)};
        }else if(e.authored==12){
            auto& beat=result.pattern;beat.bars=1;beat.grid=pattern::Grid::eighthTriplet;beat.swing=.10f;beat.seed=81207;
            beat.steps[0]={true,60,120,100,1};beat.steps[2]={true,64,88,80,2};beat.steps[4]={true,67,101,100,1};
            beat.steps[6]={true,60,117,100,1};beat.steps[7]={true,72,83,78,1};beat.steps[10]={true,69,106,100,2};
        }
        return result;
    }
    if(e.authored>std::size(detail::authored)){
        const auto index=e.authored-std::size(detail::authored)-1;
        if(index<std::size(detail::waveA)+std::size(detail::completion::items)){
            const auto& item=index<std::size(detail::waveA)?detail::waveA[index]:detail::completion::items[index-std::size(detail::waveA)];
            detail::applyWaveItem(result,item);
            return result;
        }
    }
    if(e.secondWave){
        using T=source::FilterType;
        switch(e.role){
            case Role::impact:
                layer(0,F::impact,1,0,0,.55f);layer(1,F::squelch,.56f,-.23f,.1f,.63f);
                layer(2,F::granular,.42f,.36f,.21f,.79f);p.layers[0].transpose=-12;
                p.layers[2].filters[0]={T::lowpass,1100,.85f,4};p.layers[2].gainCurve=envelope(0,.65f,0,.31f);
                s.effects[0]={E::drive,.42f,.58f};s.effects[1]={E::delay,.09f,.16f};break;
            case Role::riser:
                layer(0,F::texture,.75f,-.26f,0,1);layer(1,F::formant,.62f,.32f,.13f,.87f);
                layer(2,F::shepard,.48f,0,.28f,.72f);p.tonality={source::Scale::major,5};
                p.layers[0].pitchCurve=envelope(.29f,.48f,.74f,.57f);p.layers[1].transpose=7;
                p.amplitudeCurve=envelope(.06f,.43f,1,.69f);s.effects[0]={E::reverb,.24f,.62f};break;
            case Role::fall:
                layer(0,F::riser,.88f,-.1f,0,1);layer(1,F::riser,.52f,.3f,.15f,.85f);
                p.layers[0].cutoffCurve=envelope(.95f,.46f,.03f,.21f);
                p.layers[1].filters[0]={T::notch,2100,.65f,2};p.layers[0].gainCurve=envelope(.9f,.53f,0,.28f);
                p.layers[1].gainCurve=envelope(0,.74f,0,.39f);s.effects[0]={E::flanger,.19f,.36f};break;
            case Role::sweep:
                layer(0,F::whoosh,.67f,.14f,0,1);layer(1,F::formant,.68f,-.29f,.04f,.9f);
                p.layers[1].filters[0]={T::bandpass,950,.86f,2};p.layers[1].transpose=-5;
                p.panoramaCurve=envelope(.5f,.24f,.67f,.4f);s.effects[0]={E::phaser,.24f,.58f};break;
            case Role::zap:
                layer(0,F::glitch,.85f,-.2f,0,1);layer(1,F::laser,.72f,.43f,.15f,.76f);
                p.layers[0].transpose=12;p.layers[1].filters[0]={T::highpass,890,.72f,2};
                p.layers[1].pitchCurve=envelope(.85f,.59f,.32f,.16f);s.effects[0]={E::bitcrush,.25f,.63f};break;
            case Role::rhythm: {
                layer(0,F::squelch,.87f,-.19f,0,1);layer(1,F::metallic,.54f,.32f,.19f,.68f);
                p.tonality={source::Scale::phrygian,7};p.layers[1].transpose=7;
                auto& beat=result.pattern;beat.bars=2;beat.grid=pattern::Grid::sixteenthTriplet;beat.swing=.16f;beat.seed=43013u;
                constexpr unsigned positions[]{0,4,8,13,16,20,23,24,31,34,36,40,44,46};
                constexpr std::uint8_t notes[]{55,55,56,62,58,55,53,55,58,56,62,60,56,55};
                for(unsigned i=0;i<std::size(positions);++i)beat.steps[positions[i]]={true,notes[i],static_cast<std::uint8_t>(i%4==0?120:93),static_cast<std::uint8_t>(i%4==0?100:86),static_cast<std::uint8_t>(i%5==1?3:1)};
                s.effects[0]={E::drive,.24f,.42f};s.effects[1]={E::delay,.16f,.23f};break;
            }
            case Role::atmosphere:
                layer(0,F::texture,.64f,-.24f,0,1);layer(1,F::texture,.53f,.34f,.21f,.79f);
                layer(2,F::shepard,.43f,0,.42f,.58f);p.tonality={source::Scale::dorian,9};
                p.layers[1].transpose=7;p.layers[2].filters[0]={T::lowpass,2100,.67f,2};
                p.amplitudeCurve=envelope(0,.75f,.08f,.54f);s.effects[0]={E::chorus,.22f,.39f};s.effects[1]={E::reverb,.29f,.7f};break;
            case Role::stinger:
                layer(0,F::formant,.75f,0,0,.72f);layer(1,F::impact,.61f,0,.44f,.56f);
                layer(2,F::metallic,.66f,-.32f,.45f,.55f);p.layers[2].transpose=-5;
                p.layers[0].pitchCurve=envelope(.48f,.57f,.34f,.32f);p.layers[2].filters[0]={T::bandpass,850,1.1f,2};
                s.effects[0]={E::ring,.11f,.4f};s.effects[1]={E::reverb,.22f,.42f};break;
        }
        return result;
    }
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
