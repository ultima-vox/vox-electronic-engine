#include "engine/NoiseRiser.h"
#include "engine/CurveEdit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace {
struct Audio { std::vector<float> left, right; };
Audio render(vox::fx::NoiseProgram p,const vox::fx::macro::Config& config={},double bpm=120,bool clock=false) {
    constexpr int frames=24000;
    auto owner=std::make_unique<vox::fx::NoiseRiser>();auto& engine=*owner;engine.prepare(48000);engine.setProgram(p);
    if(!engine.setVoiceMotion(config)||!engine.setTempo(bpm))return {};
    engine.setMusicalClockAvailable(clock,bpm);
    if(!engine.noteOn(1,69,0.8f,0.5))return {};
    Audio a{std::vector<float>(frames),std::vector<float>(frames)};
    engine.render(a.left.data(),a.right.data(),frames);return a;
}
double distance(const Audio& a,const Audio& b) {
    if(a.left.empty() || a.left.size()!=b.left.size()) return 0;
    double delta=0;for(std::size_t i=0;i<a.left.size();++i)delta+=std::abs(a.left[i]-b.left[i]);return delta;
}
double rms(const std::vector<float>& a) { double sum=0;for(float x:a)sum+=x*x;return std::sqrt(sum/a.size()); }
}
int main() {
    int failures=0;auto check=[&](bool ok,const char* why){if(!ok){std::printf("FAIL: %s\n",why);++failures;}};
    vox::fx::NoiseProgram base;base.layers[0].enabled=true;base.layers[0].family=vox::fx::NoiseFamily::laser;
    const auto dry=render(base);
    {
        using namespace vox::fx;
        macro::Config motion;
        motion.lfos[0]={1,true};
        check(motion.put(0,{mod::Source::lfo1,mod::Destination::layerPan,.44f,.01f}),"voice tempo test route");
        const auto stopped=render(base,motion,84,false), playing=render(base,motion,84,true);
        check(!stopped.left.empty()&&stopped.left==playing.left&&stopped.right==playing.right,
              "seconds voice follows BPM without PPQ or a valid bar clock");
        check(distance(stopped,render(base,motion,140,false))>.01,"tempo-synced voice LFO follows supplied BPM");
        motion.lfos[0].sync=false;
        check(distance(render(base,motion,84),render(base,motion,140))==0,"free-running LFO ignores host BPM");
    }
    auto shaped=base;
    shaped.layers[0].pitchCurve=vox::fx::timeline::Curve::constant(0.9f);
    shaped.layers[0].cutoffCurve=vox::fx::timeline::Curve::constant(0.8f);
    shaped.amplitudeCurve.nodes[0].value=0.8f;shaped.amplitudeCurve.nodes[1].value=0.1f;
    shaped.panoramaCurve=vox::fx::timeline::Curve::constant(1.0f);
    const auto moved=render(shaped);
    check(!dry.left.empty()&&!moved.left.empty(),"render both baseline and shaped trajectory programs");
    check(rms(moved.left)<rms(dry.left)*0.8,"global amplitude trajectory scales layer output");
    auto local=base;
    local.layers[0].pitchCurve=shaped.layers[0].pitchCurve;
    check(distance(dry,render(local))>1.0,"layer pitch trajectory alone alters samples");
    auto global=base;
    global.pitchCurve=vox::fx::timeline::Curve::constant(.9f);
    check(distance(dry,render(global))>1.0,"Program pitch trajectory alone alters pitched samples");
    vox::fx::NoiseProgram riser;
    const auto defaultRiser=render(riser);
    riser.cutoffCurve=vox::fx::timeline::Curve::constant(.15f);
    check(distance(defaultRiser,render(riser))>1.0,"Program cutoff trajectory alone alters the noise sweep");
    riser.cutoffCurve=vox::fx::timeline::Curve::constant(.5f);
    riser.layers[0].cutoffCurve=vox::fx::timeline::Curve::constant(.15f);
    check(distance(defaultRiser,render(riser))>1.0,"layer cutoff trajectory alone alters the noise sweep");
    check(rms(moved.right)>rms(moved.left)*8,"global panorama trajectory moves output to the right");
    {
        using namespace vox::fx;
        timeline::Curve original;
        original.count=5;
        original.nodes={{{0,.2f},{.25f,.2f},{.5f,.3f},{.75f,.5f},{1,.8f}}};
        timeline::Curve edited;
        const auto selection=curveedit::bit(0)|curveedit::bit(1)|curveedit::bit(2);
        check(curveedit::move(original,selection,.9f,.1f,edited)&&edited.valid(),"group motion clamps at unselected neighbor");
        check(edited.nodes[0].time==0&&edited.nodes[3].time==.75f&&edited.nodes[2].time<.75f&&
              std::abs(edited.nodes[2].time-edited.nodes[1].time-.25f)<1e-6f&&edited.nodes[0].value>.2f,
              "group preserves spacing and pinned endpoints");
        check(curveedit::erase(edited,selection,edited)&&edited.count==3&&edited.nodes[0].time==0&&edited.nodes[2].time==1,
              "delete selected interior nodes without erasing endpoints");
    }
    std::printf("FX curve trajectories: %d failure(s)\n",failures);return failures?1:0;
}
