#include "engine/NoiseRiser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
struct Audio { std::vector<float> left, right; };
Audio render(vox::fx::NoiseProgram p) {
    constexpr int frames=24000;
    vox::fx::NoiseRiser engine;engine.prepare(48000);engine.setProgram(p);
    if(!engine.noteOn(1,69,0.8f,0.5))return {};
    Audio a{std::vector<float>(frames),std::vector<float>(frames)};
    engine.render(a.left.data(),a.right.data(),frames);return a;
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
    double delta=0;for(std::size_t i=0;i<dry.left.size();++i)delta+=std::abs(dry.left[i]-moved.left[i]);
    check(delta>1.0,"layer pitch/cutoff trajectories alter generated samples");
    check(rms(moved.right)>rms(moved.left)*8,"global panorama trajectory moves output to the right");
    std::printf("FX curve trajectories: %d failure(s)\n",failures);return failures?1:0;
}
