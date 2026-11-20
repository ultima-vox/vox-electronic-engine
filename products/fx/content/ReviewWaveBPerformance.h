#pragma once
#include "ReviewWaveBStage.h"

namespace vox::fx::content::waveb {
// Second staging tranche: two cues each for the four roles not represented
// by the transition and asset sets. These are not in the Wave A browser.
using detail::G;
struct PerformanceCue {
    WaveItem sound;
    macro::EventShape event;
    std::array<mod::Route,2> routes;
    std::array<source::GrainShape,4> grain{};
};
inline constexpr PerformanceCue performance[]{
  {{e("voxfx.impact.marble-fuse","Marble Fuse","Progressive Tech","Punchy",R::impact,M::oneShot,true,45,0,.73f,130,
       "Land after a stopped kick; a short center hit with a late, bright rim on the sides"),
    layers(v(F::impact,.95,0,0,.57,-12,.49,lp,730),v(F::metallic,.62,.34,.039,.68,7,-.31,bp,2440,.94),
           v(F::riser,.43,-.41,.16,.84,0,.61,hp,3100)),
    {.95,.76,0,.28},{.68,.45,.28,.39},{.5,.57,.41,.42},fx({E::drive,.31,.48},{E::reverb,.17,.26}),{K::minor,9}},
   {{.81,.72,.35,.27,.43,.19,.07,0}},
   {{{mod::Source::step,mod::Destination::cutoff,.32f,.025f},{mod::Source::velocity,mod::Destination::layerGain,.13f,.006f}}}},
  {{e("voxfx.impact.anchor-reed","Anchor Reed","Dark Trance","Dry",R::impact,M::oneShot,false,60,0,.42f,140,
       "Kick-safe keyless accent on a filled downbeat; keep the punch above the bass and the decay dry"),
    layers(v(F::riser,.88,0,0,.72,0,.38,hp,535),v(F::riser,.58,-.31,.019,.54,0,-.53,bp,1310,1.31),
           v(F::riser,.36,.37,.115,.885,0,.66,hp,4900)),
    {1,.61,.01,.22},flat,{.52,.45,.55,.3},fx({E::wavefold,.25,.31},{E::gate,.22,.29})},
   {{.92,.41,.65,.29,.2,.34,.15,.04}},
   {{{mod::Source::step,mod::Destination::resonance,.24f,.013f},{mod::Source::phase,mod::Destination::layerPan,.16f,.017f}}}},
  {{e("voxfx.zap.mercury-stitch","Mercury Stitch","Goa Psy","Acidic",R::zap,M::gate,true,55,0,.65f,146,
       "Hold for an eighth-note pickup and let the acid chirp release across the gap before the lead"),
    layers(v(F::squelch,.93,.12,0,1,0,.62,bp,1240,1.39),v(F::laser,.59,-.39,.137,.78,12,-.52,hp,1600),
           v(F::formant,.34,.27,.33,.67,-12,.49,bp,780)),
    {.29,.96,.13,.34},{.36,.76,.45,.56},{.57,.39,.61,.43},fx({E::drive,.34,.53},{E::tempoDelay,.13,.15}),{K::phrygian,7}},
   {{.18,.48,.81,.57,.49,.27,.34,.15},true,.028f,.11f,.58f,.16f},
   {{{mod::Source::adsr,mod::Destination::cutoff,.38f,.012f},{mod::Source::step,mod::Destination::pitch,.22f,.009f}}}},
  {{e("voxfx.zap.orbit-short","Orbit Short","Hi-Tech","Angular",R::zap,M::oneShot,true,74,0,.37f,181,
       "Fast reverse grain skim answers a broken laser transient at the end of a two-beat fill"),
    layers(v(F::glitch,.88,-.29,0,.54,0,.69,bp,2720),v(F::laser,.79,.36,.09,.74,-7,.41,hp,1380),
           v(F::granular,.58,-.19,.48,.52,7,-.62,lp,2760)),
    {.88,.73,0,.43},{.59,.69,.26,.31},{.41,.63,.35,.56},fx({E::bitcrush,.23,.56},{E::ring,.14,.27})},
   {{.08,.83,.35,.55,.26,.74,.42,0}},
   {{{mod::Source::step,mod::Destination::grainRate,.42f,.007f},{mod::Source::phase,mod::Destination::cutoff,-.33f,.009f}}},
   {{{},{},{42,.49f,true}}}},
  {{e("voxfx.rhythm.tidal-semaphore","Tidal Semaphore","Balearic Trance","Open",R::rhythm,M::sequence,true,60,0,.265f,124,
       "Sparse triplet bell answer over straight hats; resolve the second bar with a soft fifth"),
    layers(v(F::metallic,.82,.29,0,.88,0,-.37,bp,1930),v(F::formant,.59,-.32,.12,.83,7,.42,lp,2610),
           v(F::riser,.25,.14,.02,.43,0,-.32,hp,4740)),
    {.44,.96,.02,.38},{.57,.47,.44,.4},{.54,.37,.56,.45},fx({E::chorus,.24,.54},{E::tempoDelay,.15,.18}),{K::major,0},
    {G::eighthTriplet,2,.09f,{{{0,60,112},{3,67,91},{5,64,86},{8,72,100},{11,67,90},{12,60,119},
                                   {16,69,90},{19,64,84},{21,72,103,100,2},{23,67,92}}}}},
   {{.04,.35,.66,.58,.46,.33,.57,.12}},
   {{{mod::Source::step,mod::Destination::layerPan,.31f,.011f},{mod::Source::velocity,mod::Destination::cutoff,.21f,.008f}}}},
  {{e("voxfx.rhythm.carbon-shuttle","Carbon Shuttle","Dark Forest","Restless",R::rhythm,M::sequence,true,46,0,.13f,154,
       "Four bars of wooden ghost accents against a straight bass roll; reserve ratchets for turnarounds"),
    layers(v(F::impact,.79,0,0,.71,-12,.32,lp,880),v(F::granular,.63,.36,.16,.84,0,-.51,bp,1380),
           v(F::squelch,.53,-.34,.07,.72,-5,-.59,lp,1970)),
    {.91,.71,.02,.28},{.56,.48,.43,.44},{.51,.61,.39,.48},fx({E::drive,.29,.47},{E::flanger,.17,.36}),{K::dorian,10},
    {G::sixteenth,4,.21f,{{{0,46,120},{3,53,83},{9,46,99},{15,49,91,100,2},{16,46,114},{22,58,82,94},
                                 {27,53,101},{30,48,87,90,3},{32,46,118},{37,53,94},{41,46,90},{47,56,92,91,2},
                                 {48,46,116},{53,49,86},{58,53,100},{62,48,96,100,4}}}}},
   {{.72,.56,.11,.47,.24,.56,.14,.06}},
   {{{mod::Source::step,mod::Destination::grainRate,.29f,.012f},{mod::Source::velocity,mod::Destination::layerGain,.18f,.009f}}},
   {{{},{56,-1,false}}}},
  {{e("voxfx.stinger.citrine-lintel","Citrine Lintel","Uplifting","Bright",R::stinger,M::transition,true,62,1,.85f,140,
       "One-bar harmonic pickup: center the short tonic, then introduce the third and fifth before the chorus"),
    layers(v(F::formant,.78,-.16,0,.99,0,.31,lp,2900),v(F::texture,.67,.32,.36,.64,4,.41,hp,330),
           v(F::metallic,.55,-.41,.67,.33,7,-.52,bp,2150),v(F::impact,.43,.03,.968,.032,-12,.3,lp,970)),
    {.13,.61,.88,.74},{.42,.51,.59,.66},{.44,.61,.51,.68},fx({E::chorus,.27,.45},{E::reverb,.26,.56}),{K::major,2}},
   {{.14,.34,.27,.41,.62,.53,.76,.88}},
   {{{mod::Source::step,mod::Destination::layerGain,.25f,.023f},{mod::Source::phase,mod::Destination::cutoff,.29f,.025f}}}},
  {{e("voxfx.stinger.violet-checkpoint","Violet Checkpoint","Dark Psy","Hollow",R::stinger,M::oneShot,true,49,0,.95f,156,
       "One-shot cut marker on a silent beat; let the detuned metal ring answer from the opposite side"),
    layers(v(F::downlifter,.71,-.18,0,.81,-12,.47,lp,910),v(F::metallic,.86,.34,.17,.75,6,.57,bp,1530),
           v(F::squelch,.52,-.35,.49,.51,-5,-.61,nt,1090)),
    {.86,.69,.01,.43},{.61,.51,.25,.59},{.36,.71,.48,.53},fx({E::ring,.29,.31},{E::tempoDelay,.16,.19}),{K::phrygian,1}},
   {{.9,.76,.49,.65,.18,.48,.26,.04}},
   {{{mod::Source::step,mod::Destination::layerPan,.37f,.028f},{mod::Source::phase,mod::Destination::resonance,.28f,.02f}}}},
};
static_assert(std::size(performance)==8);

inline offline::Capture makePreview(const PerformanceCue& cue,std::uint32_t rate){
    offline::Capture result;
    const auto& entry=cue.sound.entry;
    result.program.mode=entry.mode;result.program.bars=entry.bars;
    auto& s=result.settings;s.sampleRate=rate;s.note=entry.note;s.tempo=entry.bpm;s.duration=entry.seconds;
    detail::applyWaveItem(result,cue.sound);
    for(std::size_t i=0;i<cue.grain.size();++i)result.program.layers[i].grains=cue.grain[i];
    s.macroRoutes.event=cue.event;
    for(std::size_t i=0;i<cue.routes.size();++i)(void)s.macroRoutes.put(i,cue.routes[i]);
    return result;
}
} // namespace vox::fx::content::waveb
