#include "engine/NoiseRiser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace {
struct Audio { std::vector<float> left, right; };
Audio render(vox::fx::NoiseProgram p) {
    constexpr int frames=24000;
    auto owner=std::make_unique<vox::fx::NoiseRiser>();auto& engine=*owner;engine.prepare(48000);engine.setProgram(p);
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
    std::printf("FX curve trajectories: %d failure(s)\n",failures);return failures?1:0;
}
