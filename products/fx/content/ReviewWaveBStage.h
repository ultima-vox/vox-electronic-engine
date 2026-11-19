#pragma once
#include "FactoryCandidates.h"

namespace vox::fx::content::waveb {
// First Wave B comparison set is staged separately. These are alternate cues
// for the same arrangement slot, not transpositions or seeded clones.
using namespace detail::completion;
using detail::F;using detail::R;using detail::M;using detail::K;using detail::E;
using detail::WaveItem;
struct Candidate {
    const char* pair;
    WaveItem sound;
    std::array<source::GrainShape,4> grain{};
    std::array<float,8> step{0,.8f,.35f,1,.22f,.72f,.41f,0};
    mod::Route motion{};
};
inline constexpr Candidate staged[]{
{"progressive-four-bar",{e("voxfx.riser.trellis-ascent","Trellis Ascent","Progressive Tech","Patient",R::riser,M::transition,true,53,4,3,131,"Keyed four-bar lift off an empty bass break"),
    layers(v(F::formant,.86,-.25,0,1,0,.31,bp,1370),v(F::granular,.63,.39,.19,.81,7,-.28,lp,2100),v(F::laser,.43,.25,.58,.36,12,-.58,hp,880),v(F::impact,.54,0,.96,.04,-12,.48,lp,760)),
    {.13,.67,.95,.71},{.34,.52,.67,.61},{.41,.56,.57,.67},fx({E::chorus,.21,.44},{E::tempoDelay,.14,.25}),{K::dorian,5}},
    {{{},{55,.28f,true}}},{.05,.24,.19,.42,.36,.68,.51,.27},{mod::Source::step,mod::Destination::cutoff,.23f,.01f}},
{"progressive-four-bar",{e("voxfx.riser.gasket-rush","Gasket Rush","Progressive Tech","Dry",R::riser,M::transition,false,60,4,3,131,"Keyless four-bar pressure ramp for a harmonically crowded break"),
    layers(v(F::riser,.93,-.2,0,1,0,-.61,bp,650,.93),v(F::riser,.81,.41,.29,.7,0,.48,nt,1520),v(F::riser,.79,-.38,.72,.28,0,.68,hp,3580)),
    {.04,.66,.94,.81},flat,{.58,.38,.55,.72},fx({E::gate,.26,.41},{E::reverb,.15,.31})},
    {},{0,.11,.31,.07,.41,.52,.18,.62},{mod::Source::step,mod::Destination::layerPan,.31f,.022f}},
{"forest-eight-bar",{e("voxfx.fall.hush-mycelium","Hush Mycelium","Dark Forest","Organic",R::fall,M::transition,true,46,8,4,154,"Eight-bar modal fall into a percussion-only verse"),
    layers(v(F::granular,.89,.35,0,1,-12,-.53,lp,1630),v(F::formant,.71,-.39,.13,.86,3,-.27,bp,825),v(F::metallic,.52,.31,0,.48,-7,.63,nt,478)),
    {.98,.83,.035,.4},{.79,.42,.24,.51},{.59,.33,.48,.54},fx({E::ring,.16,.31},{E::tempoDelay,.2,.18},{E::reverb,.2,.46}),{K::dorian,10}},
    {{{123,.63f,true}}},{.77,.8,.51,.32,.44,.18,.09,0},{mod::Source::step,mod::Destination::grainRate,.35f,.03f}},
{"forest-eight-bar",{e("voxfx.fall.canopy-ash","Canopy Ash","Dark Forest","Weathered",R::fall,M::transition,false,60,8,4,154,"Eight-bar noise decay with hollow near-field residue"),
    layers(v(F::riser,.87,.37,0,.69,0,.44,hp,1460),v(F::riser,.81,-.36,.12,.86,0,-.53,bp,642,1.17),v(F::riser,.64,.12,.53,.47,0,-.31,lp,965)),
    {.93,.62,.03,.28},flat,{.61,.39,.47,.51},fx({E::flanger,.34,.59},{E::lowpass,.31,.42},{E::reverb,.17,.41})},
    {},{.94,.51,.64,.17,.29,.08,.21,.14},{mod::Source::step,mod::Destination::resonance,.27f,.031f}},
{"uplifting-sixteen-bar",{e("voxfx.riser.zenith-thread","Zenith Thread","Uplifting","Radiant",R::riser,M::transition,true,67,16,4,140,"Sixteen-bar fifth-led rising counterline before a chorus"),
    layers(v(F::shepard,.76,-.24,0,.99,0,.41,hp,210),v(F::texture,.82,.29,.14,.86,-5,.4,lp,3300),v(F::formant,.78,-.34,.36,.64,7,.56,bp,1440),v(F::metallic,.51,.42,.79,.19,12,-.55,bp,2700)),
    {.09,.65,.95,.83},{.36,.58,.75,.77},{.47,.64,.52,.66},fx({E::chorus,.33,.53},{E::tempoDelay,.2,.22},{E::reverb,.16,.56}),{K::major,7}},
    {},{.11,.33,.51,.31,.68,.57,.83,.94},{mod::Source::step,mod::Destination::layerGain,.21f,.07f}},
{"uplifting-sixteen-bar",{e("voxfx.riser.white-veranda","White Veranda","Uplifting","Airy",R::riser,M::transition,false,60,16,4,140,"Sixteen-bar keyless air opening underneath an existing melody"),
    layers(v(F::riser,.72,.34,0,1,0,-.52,lp,1200),v(F::riser,.83,-.36,.24,.76,0,.43,bp,1880),v(F::riser,.69,.43,.52,.48,0,.56,hp,3280),v(F::riser,.55,-.29,.91,.09,0,.69,hp,6100)),
    {.055,.52,.86,.84},flat,{.46,.28,.57,.72},fx({E::phaser,.19,.65},{E::tempoDelay,.13,.17},{E::reverb,.31,.61})},
    {},{.1,.13,.23,.21,.54,.61,.63,.77},{mod::Source::step,mod::Destination::cutoff,.31f,.05f}},
};
inline offline::Capture makePreview(const Candidate& item,std::uint32_t rate){
    offline::Capture result;
    const auto& entry=item.sound.entry;
    result.program.mode=entry.mode;result.program.bars=entry.bars;
    auto& s=result.settings;s.sampleRate=rate;s.note=entry.note;s.tempo=entry.bpm;s.duration=entry.seconds;
    detail::applyWaveItem(result,item.sound);
    for(std::size_t i=0;i<item.grain.size();++i)result.program.layers[i].grains=item.grain[i];
    s.macroRoutes.event.steps=item.step;
    (void)s.macroRoutes.put(0,item.motion);
    return result;
}
} // namespace vox::fx::content::waveb
