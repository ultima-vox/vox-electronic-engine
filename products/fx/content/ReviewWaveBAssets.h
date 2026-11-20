#pragma once
#include "ReviewWaveBStage.h"

namespace vox::fx::content::waveb {
// Bring-your-own-asset rehearsal fixtures. The two source waves are original
// authored modal studies, not third-party recordings or shipped sample stock.
// A portable Program carries the source identity only; its WAV travels apart.
enum class Asset { rail, ceramic };
struct AssetCue {
    Asset asset;
    const char* sourceName;
    WaveItem sound;
    std::array<source::GrainShape,4> grain;
    macro::EventShape event;
    std::array<mod::Route,2> routes;
};
inline constexpr AssetCue assetCues[]{
    {Asset::rail,"rail-modes.wav",
     {e("voxfx.sweep.vellum-coil","Vellum Coil","Dark Trance","Brushed",R::sweep,M::gate,true,55,0,1.95f,140,
        "Reverse metal inhale into an offbeat, then release the gate before the bass returns"),
      layers(v(F::granular,.9,-.38,0,.93,0,-.28,bp,1820,.84),v(F::granular,.72,.42,.25,.75,7,.15,hp,490),
             v(F::riser,.43,.13,.39,.59,0,-.44,lp,1460)),
      {.12,.94,.17,.64},{.42,.62,.47,.61},{.31,.64,.46,.69},fx({E::flanger,.22,.61},{E::tempoDelay,.16,.23}),{K::dorian,7}},
     {{{96,.55f,true},{38,.29f,false}}},{{.08,.17,.31,.73,.49,.64,.26,.09},true,.045f,.21f,.53f,.28f},
     {{{mod::Source::step,mod::Destination::grainRate,.35f,.021f},{mod::Source::adsr,mod::Destination::cutoff,.27f,.019f}}}},
    {Asset::ceramic,"ceramic-modes.wav",
     {e("voxfx.atmosphere.kiln-choir","Kiln Choir","Balearic Trance","Warm",R::atmosphere,M::continuous,true,60,0,4.8f,124,
        "Held ceramic partial bed in a breakdown; use NoteOff to leave an unmasked chord change"),
      layers(v(F::granular,.91,.25,0,1,0,.31,lp,2430),v(F::granular,.63,-.39,.09,.91,7,-.27,bp,750,.9),
             v(F::texture,.43,.12,.26,.74,-12,-.37,hp,190),v(F::formant,.38,-.19,.43,.57,4,.34,bp,1320)),
      {.09,.93,.31,.57},{.45,.54,.49,.47},{.54,.36,.6,.69},fx({E::chorus,.31,.52},{E::reverb,.24,.64}),{K::major,0}},
     {{{140,-1,false},{72,.42f,true}}},{{.16,.41,.62,.52,.37,.55,.28,.38},true,.32f,.42f,.68f,.64f},
     {{{mod::Source::adsr,mod::Destination::layerGain,.42f,.033f},{mod::Source::step,mod::Destination::layerPan,.2f,.036f}}}},
};
// Explicit modal frequencies, decay and strike timing. Mono PCM16, 48 kHz.
// Retain the exported WAV to reproduce the exact SHA on another machine.
inline std::vector<std::uint8_t> sourceWav(Asset asset){
    constexpr unsigned rate=48000;
    const bool rail=asset==Asset::rail;
    const unsigned frames=rail?36000:48000;
    std::vector<std::uint8_t> out;out.reserve(44+2*frames);
    offline::tag(out,"RIFF");offline::u32(out,36+2*frames);offline::tag(out,"WAVE");offline::tag(out,"fmt ");
    offline::u32(out,16);offline::u16(out,1);offline::u16(out,1);offline::u32(out,rate);offline::u32(out,rate*2);
    offline::u16(out,2);offline::u16(out,16);offline::tag(out,"data");offline::u32(out,2*frames);
    constexpr double tau=6.283185307179586;
    for(unsigned i=0;i<frames;++i){
        const double t=static_cast<double>(i)/rate;
        const auto mode=[&](double start,double hz,double decay,double amount,double bend){
            const double age=t-start;if(age<=0)return 0.;
            return amount*(-std::expm1(-age/.002))*std::exp(-age*decay)*std::sin(tau*(hz*age+bend*age*age));
        };
        double x=rail?(mode(0,196,7.8,.29,-45)+mode(.013,399,9.2,.23,-36)+mode(.019,1167,20,.17,-270)+
                      mode(.12,728,8.4,.16,-210)+mode(.26,1481,27,.06,-340)):
                     (mode(0,261.626,4.3,.25,0)+mode(.009,392.0,3.7,.22,0)+mode(.07,813,7.2,.14,11)+
                      mode(.19,523.252,4.9,.16,0)+mode(.29,1619,19,.08,-3));
        x*=std::clamp((static_cast<double>(frames)-1-i)/960.0,0.,1.);
        offline::u16(out,static_cast<std::uint16_t>(std::lround(32767*x)));
    }
    return out;
}
inline offline::Capture makeAssetPreview(const AssetCue& cue){
    offline::Capture result;result.useUserSample=true;
    const auto& entry=cue.sound.entry;
    result.program.mode=entry.mode;
    result.settings.note=entry.note;result.settings.tempo=entry.bpm;result.settings.duration=entry.seconds;
    detail::applyWaveItem(result,cue.sound);
    for(std::size_t i=0;i<cue.grain.size();++i)result.program.layers[i].grains=cue.grain[i];
    auto& macro=result.settings.macroRoutes;macro.event=cue.event;
    for(std::size_t i=0;i<cue.routes.size();++i)(void)macro.put(i,cue.routes[i]);
    return result;
}
} // namespace vox::fx::content::waveb
