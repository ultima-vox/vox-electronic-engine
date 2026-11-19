#pragma once
#include "ProgramStore.h"

namespace vox::fx::generator {
// Product-role rules, separate from the curated catalog. This path is always
// explicit in UI; existing layer-only Generate/Mutate keep their old contract.
struct RolePlan {
    std::array<NoiseFamily,4> families;
    PlayMode mode;
    std::uint8_t bars;
    std::array<effects::Type,4> slots;
    std::array<float,3> amplitude;
};
inline constexpr RolePlan rolePlans[]{
    {{NoiseFamily::impact,NoiseFamily::metallic,NoiseFamily::riser,NoiseFamily::downlifter},PlayMode::oneShot,0,{effects::Type::drive,effects::Type::lowpass,effects::Type::bypass,effects::Type::reverb},{1,.57f,0}},
    {{NoiseFamily::shepard,NoiseFamily::riser,NoiseFamily::texture,NoiseFamily::laser},PlayMode::transition,4,{effects::Type::chorus,effects::Type::lowpass,effects::Type::bypass,effects::Type::reverb},{.06f,.55f,1}},
    {{NoiseFamily::downlifter,NoiseFamily::texture,NoiseFamily::riser,NoiseFamily::whoosh},PlayMode::oneShot,0,{effects::Type::lowpass,effects::Type::bypass,effects::Type::delay,effects::Type::reverb},{.87f,.61f,.02f}},
    {{NoiseFamily::whoosh,NoiseFamily::formant,NoiseFamily::riser,NoiseFamily::texture},PlayMode::loop,0,{effects::Type::phaser,effects::Type::chorus,effects::Type::bypass,effects::Type::bypass},{.17f,1,.17f}},
    {{NoiseFamily::squelch,NoiseFamily::laser,NoiseFamily::formant,NoiseFamily::glitch},PlayMode::gate,0,{effects::Type::drive,effects::Type::bitcrush,effects::Type::delay,effects::Type::bypass},{.74f,.95f,.24f}},
    {{NoiseFamily::glitch,NoiseFamily::squelch,NoiseFamily::laser,NoiseFamily::metallic},PlayMode::sequence,0,{effects::Type::bitcrush,effects::Type::gate,effects::Type::delay,effects::Type::bypass},{.83f,.62f,.09f}},
    {{NoiseFamily::granular,NoiseFamily::texture,NoiseFamily::formant,NoiseFamily::shepard},PlayMode::continuous,0,{effects::Type::lowpass,effects::Type::chorus,effects::Type::delay,effects::Type::reverb},{.04f,.79f,.57f}},
    {{NoiseFamily::metallic,NoiseFamily::formant,NoiseFamily::impact,NoiseFamily::shepard},PlayMode::oneShot,0,{effects::Type::ring,effects::Type::bypass,effects::Type::chorus,effects::Type::reverb},{.91f,.74f,.05f}},
};
inline timeline::Curve arch(float a,float b,float c,float center=.5f) noexcept {
    using S=timeline::Shape;timeline::Curve curve;curve.count=3;
    curve.nodes[0]={0,a,S::smooth};curve.nodes[1]={center,b,S::exponential};curve.nodes[2]={1,c,S::linear};return curve;
}
inline bool makeSound(const ProgramStore::Snapshot& previous,bool mutation,ProgramStore::Snapshot& output,bool& useBuiltin){
    const auto& controller=previous.generator;
    if(!controller.valid()||!previous.program.valid()||!previous.pattern.valid()||!previous.macros.valid()||!effects::validPacked(previous.effects))return false;
    const auto locked=[&](GraphField f){return (controller.graphLocks&f)!=0;};
    auto next=previous;
    const auto& plan=rolePlans[controller.role];auto settings=controller.settings(static_cast<int>(plan.families[0]));
    Random rand(settings.seed^0x50F3A77Du^(controller.role*0x9e3779b9u));
    Result shape;
    if(mutation){if(!mutate({previous.program,4},controller.locks,settings.seed^0x14726a7du,shape))return false;}
    else if(!generate(settings,{previous.program,4},controller.locks,shape))return false;
    next.program=shape.program;
    for(std::size_t i=0;i<4;++i){auto& l=next.program.layers[i];const auto& before=previous.program.layers[i];const auto locks=controller.locks.layers[i];
        if(!mutation){
            auto familyIndex=i;
            if(i>0&&settings.style>=3)familyIndex=1+(i-1+static_cast<unsigned>(settings.style-2))%3;
            l.family=plan.families[familyIndex];
            const float offset=i==0?0.f:(controller.role==1?static_cast<float>(i)*.20f:static_cast<float>(i)*.06f);
            l.timing={offset,std::min(1.f-offset,.55f+rand.unit()*.45f)};
            l.gain*=1.f-static_cast<float>(i)*.14f;
            l.character=(rand.unit()-.5f)*settings.intensity;
            l.transpose=i==0?0.f:i==1?(settings.style<3?7.f:-12.f):i==2?12.f:-5.f;
            l.filters[0]={source::FilterType::lowpass,950.f+settings.energy*4300.f+rand.unit()*1300.f,.70f,static_cast<std::uint8_t>(i==0?2:4)};
            l.filters[1]={source::FilterType::highpass,i==0?24.f:90.f+rand.unit()*210.f,.707f,2};
        }else{
            // Bounded local perturbations preserve topology on a mutation.
            l.character=std::clamp(l.character+(rand.unit()-.5f)*.28f,-1.f,1.f);
            l.transpose=std::clamp(l.transpose+(rand.next()%2?1.f:-1.f),-24.f,24.f);
            for(auto& f:l.filters)if(f.type!=source::FilterType::off)f.hz=std::clamp(f.hz*(.85f+.30f*rand.unit()),20.f,20000.f);
        }
        applyLocks(l,before,locks);
    }
    const auto curve=[&](GraphField bit,timeline::Curve& dst,timeline::Curve fresh){
        if(locked(bit))return;
        if(!mutation){dst=fresh;return;}
        for(std::size_t i=0;i<dst.count;++i)dst.nodes[i].value=std::clamp(dst.nodes[i].value+(rand.unit()-.5f)*.10f,0.f,1.f);
    };
    auto& p=next.program;
    curve(globalAmp,p.amplitudeCurve,arch(plan.amplitude[0],plan.amplitude[1],plan.amplitude[2],.37f+settings.energy*.24f));
    curve(globalPitch,p.pitchCurve,controller.role==1?arch(.34f,.51f,.67f):controller.role==2?arch(.64f,.49f,.37f):arch(.52f,.57f,.47f));
    curve(globalCutoff,p.cutoffCurve,controller.role==2?arch(.76f,.51f,.25f):arch(.32f,.55f,.62f));
    curve(globalPan,p.panoramaCurve,arch(.5f,.5f+settings.stereo*.21f,.5f-settings.stereo*.14f));
    if(!locked(tonality)){
        if(!mutation)p.tonality={settings.style<2?source::Scale::major:settings.style<4?source::Scale::minor:source::Scale::phrygian,
            static_cast<std::uint8_t>(rand.next()%12)};
        else if(p.tonality.scale!=source::Scale::chromatic)p.tonality.root=static_cast<std::uint8_t>((p.tonality.root+(rand.next()%2?5:7))%12);
    }
    if(!mutation&&!locked(playback)){p.mode=plan.mode;p.triggerPolicy=controller.role==4?TriggerPolicy::replace:TriggerPolicy::overlap;}
    if(!mutation&&!locked(barLength))p.bars=plan.bars;
    if(!mutation&&!locked(fxChain)){
        next.effects=0;
        for(unsigned i=0;i<4;++i)next.effects|=static_cast<std::uint64_t>(effects::pack({plan.slots[i],1,.5f}))<<(16*i);
    }
    if(!locked(routes)){
        using S=mod::Source;using D=mod::Destination;
        if(!mutation){auto& m=next.macros;m={};m.randomSeed=settings.seed;m.lfos[0]={.25f,true};m.lfos[1]={1.2f,false};
            (void)m.put(0,{S::macro1,D::cutoff,.45f,.023f});(void)m.put(1,{S::macro2,D::width,.36f,.021f});
            (void)m.put(2,{S::lfo1,D::layerPan,settings.stereo*.28f,.01f});
            (void)m.put(3,{S::macro3,controller.role==6?D::grainRate:D::amount1,.38f,.04f});
        }else for(unsigned i=0;i<next.macros.count;++i)next.macros.routes[i].depth=std::clamp(next.macros.routes[i].depth+(rand.unit()-.5f)*.14f,-1.f,1.f);
    }
    if(!locked(triggerPattern)){
        auto& beat=next.pattern;
        if(!mutation){beat={};beat.seed=settings.seed;
            if(controller.role==5){beat.bars=2;beat.grid=settings.style>=4?pattern::Grid::sixteenthTriplet:pattern::Grid::sixteenth;
                beat.swing=.08f+.24f*settings.energy;
                constexpr unsigned accents[]{0,3,6,8,11,14};constexpr unsigned notes[]{0,0,7,0,3,12};
                auto perBar=pattern::stepsPerBar(beat.grid);
                for(unsigned b=0;b<beat.bars;++b)for(unsigned i=0;i<std::size(accents);++i){
                    const auto idx=b*perBar+accents[i]*perBar/16+b*(i%2);
                    beat.steps[idx]={true,static_cast<std::uint8_t>(48+p.tonality.root+notes[i]),
                        static_cast<std::uint8_t>(i%2==0?113:89),static_cast<std::uint8_t>(i==0?100:80+rand.next()%21),static_cast<std::uint8_t>(i%3==2?3:1)};
                }
            }
        }else if(std::any_of(beat.steps.begin(),beat.steps.end(),[](const auto& s){return s.enabled;})){
            beat.seed^=rand.next();beat.swing=std::clamp(beat.swing+(rand.unit()-.5f)*.18f,0.f,1.f);
        }
    }
    useBuiltin=!locked(sampleRef)&&!mutation;
    if(!p.valid()||!next.pattern.valid()||!next.macros.valid()||!effects::validPacked(next.effects))return false;
    output=std::move(next);return true;
}
} // namespace vox::fx::generator
