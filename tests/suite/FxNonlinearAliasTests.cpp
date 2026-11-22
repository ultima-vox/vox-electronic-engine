// Nonlinear alias screen for the shared FX slots.
//
// A bin-centred sine is fed through one slot. Because the input period divides
// the analysis window, every harmonic of the input falls exactly on a DFT bin;
// all remaining energy is content the nonlinearity generated and folded back
// below Nyquist. Total energy comes from Parseval, harmonic energy from a
// Goertzel probe on the harmonic bins only, so the check stays cheap.
//
// Drive uses first-order antiderivative antialiasing (ADAA) and keeps the
// thresholds introduced with that change. Wavefold now uses second-order ADAA
// on its unchanged triangle-fold shape. It is measured against an independent
// point-sampled reference of the old transfer function: the assertion is on
// absolute folded power, because ADAA also attenuates the extreme probes and a
// ratio-only check would hide that.
#include "engine/Effects.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <limits>
#include <random>

struct WavefoldReference { double gain,x0,x1,x2,expected; };
#include "fixtures/FxWavefoldReference.h"

namespace {
constexpr int window = 16384;
constexpr double pi = 3.14159265358979323846;

struct AliasResult {
    double harmonic = 0;
    double aliased = 0;
    double ratio = 0;
    // Power of a unit-amplitude sine over the analysis window.
    double inputPower = .25 * window * window;
};

// Positive-side energy of a buffer split into harmonic and generated energy.
AliasResult analyse(const std::vector<float>& left, int fundamentalBin) {
    AliasResult result;
    double timeEnergy = 0;
    for (int i = 0; i < window; ++i) {
        const double x = left[i];
        timeEnergy += x * x;
    }
    const double total = .5 * window * timeEnergy;
    for (int bin = fundamentalBin; bin <= window / 2; bin += fundamentalBin) {
        double coeff = 2 * std::cos(2 * pi * bin / window);
        double s1 = 0, s2 = 0;
        for (int i = 0; i < window; ++i) {
            const double s = left[i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        result.harmonic += s1 * s1 + s2 * s2 - coeff * s1 * s2;
    }
    result.aliased = std::max(0.0, total - result.harmonic);
    result.ratio = total > 0 ? result.aliased / total : 0;
    return result;
}

double foldedDb(const AliasResult& r) {
    return 10 * std::log10(r.aliased / r.inputPower + 1e-30);
}
double ratioDb(const AliasResult& r) {
    return -10 * std::log10(r.ratio + 1e-12);
}

// Point-sampled reference: the pre-ADAA Drive transfer function, evaluated
// directly on the same input. Used only to show the measured improvement.
AliasResult measureDirect(float amount, double rate, int fundamentalBin) {
    const double f0 = fundamentalBin * rate / window;
    const double gain = 1.0 + 7.0 * amount;
    const double level = 1.0 / std::sqrt(gain);
    std::vector<float> left(window);
    for (int i = 0; i < window; ++i) {
        const double x = std::sin(2 * pi * f0 * i / rate);
        left[i] = static_cast<float>(x + amount * (std::tanh(x * gain) * level - x));
    }
    return analyse(left, fundamentalBin);
}

// Point-sampled reference for Wavefold: the literal pre-change expression,
// independent of the arithmetic triangle and ADAA helpers in the slot.
AliasResult measureDirectWavefold(float amount, double rate, int fundamentalBin) {
    const double f0 = fundamentalBin * rate / window;
    const double gain = 1.0 + 7.0 * amount;
    std::vector<float> left(window);
    for (int i = 0; i < window; ++i) {
        const double x = std::sin(2 * pi * f0 * i / rate);
        const double u = std::clamp(x * gain, -8.0, 8.0);
        left[i] = static_cast<float>(2.0 / pi * std::asin(std::sin(pi * u * .5)));
    }
    return analyse(left, fundamentalBin);
}

AliasResult measure(vox::fx::effects::Type type, float amount, double rate, int fundamentalBin) {
    vox::fx::effects::Chain chain;
    AliasResult result;
    if (!chain.prepare(rate) || !chain.set(0, {type, 1.0f, amount})) return result;
    const std::size_t settle = static_cast<std::size_t>(rate * .2);
    std::vector<float> left(settle + window), right(left.size());
    const double f0 = fundamentalBin * rate / window;
    for (std::size_t i = 0; i < left.size(); ++i)
        left[i] = static_cast<float>(std::sin(2 * pi * f0 * i / rate));
    right = left;
    chain.process(left.data(), right.data(), left.size());
    std::vector<float> settled(left.begin() + static_cast<std::ptrdiff_t>(settle), left.end());
    return analyse(settled, fundamentalBin);
}
} // namespace

int main() {
    using vox::fx::effects::Type;
    using vox::fx::effects::Settings;
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
    };

    // Drive ADAA must keep the behaviour and limits introduced with it.
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const int bin : {683, 1707, 2731}) {
            for (const float amount : {.5f, 1.0f}) {
                const auto drive = measure(Type::drive, amount, rate, bin);
                const auto direct = measureDirect(amount, rate, bin);
                check(std::isfinite(drive.ratio) && drive.ratio >= 0 && drive.ratio <= 1,
                      "drive alias measure in range");
                // Measured against the same transfer function evaluated
                // point-sampled, ADAA must remove a clear share of the folded
                // energy at every probe.
                check(drive.ratio <= .55 * direct.ratio + 1e-6,
                      "antialiased Drive folds less energy than direct tanh");
                check(drive.ratio < .06, "antialiased Drive keeps folded energy low");
            }
        }
        // Amount 0 is a linear slot; nothing may be generated.
        const auto bypassed = measure(Type::drive, 0.0f, rate, 1707);
        check(bypassed.ratio < 1e-12, "zero Drive amount is alias-free");
    }

    // Repeated near-coincident samples in a linear segment should integrate
    // to the exact average, even when divided differences nearly cancel.
    {
        using namespace vox::fx::effects::detail;
        bool stable=true; double maxError=0.0, maxOutput=0.0;
        for (double centre : { .25, 1.0, -.25 })
            for (double step : { 1e-6, 1e-7, 1e-8, 2e-9, 1e-9, 1e-10 }) {
                WavefoldMemory state;
                for (int i=0;i<60;++i) {
                    const double x=centre+(i%3-1)*step;
                    const double out=wavefoldTick(x,2.0,state);
                    if(i<2)continue;
                    const double x1=centre+((i-1)%3-1)*step;
                    const double x2=centre+((i-2)%3-1)*step;
                    // These centres and samples are away from slope corners;
                    // a linear transfer integrates exactly at the mean.
                    const double reference=wavefoldValue((x+x1+x2)/3.0,2.0);
                    maxError=std::max(maxError,std::abs(out-reference));
                    maxOutput=std::max(maxOutput,std::abs(out));
                    stable=stable&&std::isfinite(out)&&std::abs(out-reference)<1e-5&&std::abs(out)<=1.00001;
                }
            }
        std::printf("Wavefold near-coincident max error %.12g max output %.12g\n",maxError,maxOutput);
        check(stable,"Wavefold ADAA2 near-coincident samples stay bounded and follow the linear reference");
    }

    // Decimal-generated high precision references cover near-coincident,
    // alternating, slope-corner and clamp-boundary samples. The reference is
    // rate-independent mathematics; spectral tests below run at three rates.
    {
        using namespace vox::fx::effects::detail;
        bool accurate=true; double maxError=0.0, maxOutput=0.0;
        for (const auto& c : wavefoldReferences) {
            WavefoldMemory state;
            state.x1=c.x1; state.x2=c.x2;
            state.gain=c.gain==1.0?8.0:1.0; // force gain-change recomputation
            state.f2x1=wavefoldSecond(state.x1,state.gain);
            state.lastDifference=123.0; // stale value must not be reused
            const double out=wavefoldTick(c.x0,c.gain,state);
            const double error=std::abs(out-c.expected);
            maxError=std::max(maxError,error); maxOutput=std::max(maxOutput,std::abs(out));
            accurate=accurate&&std::isfinite(out)&&error<1e-5&&std::abs(out)<=1.00001;
        }
        WavefoldMemory denormal;
        const double tiny=std::numeric_limits<double>::denorm_min();
        const auto tinyOut=wavefoldTick(tiny,8.0,denormal);
        accurate=accurate&&std::isfinite(tinyOut)&&std::abs(tinyOut)<1e-300;
        const auto floatTiny=static_cast<double>(std::numeric_limits<float>::denorm_min());
        const auto floatTinyOut=wavefoldTick(floatTiny,8.0,denormal);
        accurate=accurate&&std::isfinite(floatTinyOut)&&std::abs(floatTinyOut)<1e-35;
        std::printf("Wavefold Decimal references: %zu cases max error %.12g max output %.12g\n",
                    std::size(wavefoldReferences),maxError,maxOutput);
        check(accurate,"Wavefold ADAA2 matches high-precision boundary references and handles denormals");
    }

    // Rapid per-sample gain changes and tiny/alternating inputs must remain
    // bounded. This is a math invariant, independent of host sample rate.
    {
        using namespace vox::fx::effects::detail;
        std::mt19937_64 random(918273); std::uniform_real_distribution<double> unit(-1.0,1.0), gain(1.0,8.0);
        bool bounded=true; double maxOutput=0.0;
        for (int scenario=0;scenario<4;++scenario) {
            WavefoldMemory state; double previous=0.0;
            for (int i=0;i<20000;++i) {
                const double g=scenario==3?gain(random):2.0;
                const double x=scenario==0?unit(random):scenario==1?previous+unit(random)*1e-6:
                               scenario==2?(i%2?previous:1.0-previous):unit(random);
                const double out=wavefoldTick(x,g,state);
                bounded=bounded&&std::isfinite(out)&&std::abs(out)<=1.0001;
                maxOutput=std::max(maxOutput,std::abs(out)); previous=x;
            }
        }
        std::printf("Wavefold changing-gain adversarial max output %.12g\n",maxOutput);
        check(bounded,"Wavefold ADAA2 stays finite and bounded under rapid gain changes");
    }

    // Real Chain control path: rapid parameter changes and repeated slot-type
    // changes at all supported diagnostic rates/block sizes. The held input
    // makes a gross transition discontinuity observable independently of a
    // deliberately discontinuous source waveform.
    {
        using namespace vox::fx::effects;
        bool finite=true; double maxStep=0.0, maxOutput=0.0;
        for (const double rate : {44100.0,48000.0,96000.0})
            for (const int block : {32,64,128}) {
                Chain chain; finite=finite&&chain.prepare(rate);
                finite=finite&&chain.set(0,{Type::wavefold,1.0f,.25f});
                float left=.125f,right=.125f;
                for(int warm=0;warm<static_cast<int>(rate*.05);++warm) {
                    left=right=.125f; chain.process(&left,&right,1);
                }
                double previous=left;
                for(int i=0;i<block*64;++i) {
                    const auto type=(i%509<31)?Type::bypass:Type::wavefold;
                    const float wet=(i%2)?1.f:0.f, amount=(i%2)?1.f:0.f;
                    finite=finite&&chain.set(0,{type,wet,amount});
                    left=right=.125f; chain.process(&left,&right,1);
                    finite=finite&&std::isfinite(left)&&std::isfinite(right)
                           &&std::abs(left)<=1.0001f&&std::abs(right)<=1.0001f;
                    maxStep=std::max(maxStep,std::abs(static_cast<double>(left)-previous));
                    maxOutput=std::max(maxOutput,std::abs(static_cast<double>(left))); previous=left;
                }
                // Two fresh prepares must retire previous ADAA state; a silent
                // reselected slot cannot leak the prior held signal.
                for(int reset=0;reset<2;++reset) {
                    finite=finite&&chain.prepare(rate)&&chain.set(0,{Type::wavefold,1.0f,.75f});
                    for(int i=0;i<block*2;++i) {
                        left=right=0.f; chain.process(&left,&right,1);
                        finite=finite&&left==0.f&&right==0.f;
                    }
                }
            }
        std::printf("Wavefold rapid control max step %.9g max output %.9g\n",maxStep,maxOutput);
        check(finite&&maxStep<.05,"Wavefold rapid control remains finite, bounded and without gross steps");
    }

    // Dry/wet summing is explicitly checked against independent parallel
    // Chains. This checks the mix contract; it does not approve the inherent
    // ADAA phase shift or replace listening/host phase tests.
    {
        using namespace vox::fx::effects;
        bool mixedCorrectly=true; double maxMixError=0.0;
        for(const double rate : {44100.0,48000.0,96000.0}) {
            const int settle=static_cast<int>(rate*.2), n=4096;
            std::vector<float> input(settle+n),dry,wet,mixed,right;
            for(int i=0;i<settle+n;++i)
                input[static_cast<std::size_t>(i)]=static_cast<float>(.1*std::sin(2*std::acos(-1.0)*8000*i/rate));
            auto render=[&](float wetness,std::vector<float>& out) {
                Chain chain;
                if(!chain.prepare(rate)||!chain.set(0,{Type::wavefold,wetness,.28f}))return false;
                out=input; right=input; chain.process(out.data(),right.data(),out.size());return true;
            };
            mixedCorrectly=mixedCorrectly&&render(0.f,dry)&&render(1.f,wet)&&render(.5f,mixed);
            if(dry.size()!=input.size()||wet.size()!=input.size()||mixed.size()!=input.size()) { mixedCorrectly=false;continue; }
            for(int i=settle;i<settle+n;++i) {
                const auto at=static_cast<std::size_t>(i);
                const double reference=.5*(static_cast<double>(dry[at])+static_cast<double>(wet[at]));
                const double error=std::abs(static_cast<double>(mixed[at])-reference);
                maxMixError=std::max(maxMixError,error);
                mixedCorrectly=mixedCorrectly&&std::isfinite(mixed[at])&&error<1e-4;
            }
        }
        std::printf("Wavefold parallel dry/wet max mix error %.12g\n",maxMixError);
        check(mixedCorrectly,"Wavefold 50 percent wet matches independently rendered parallel dry/wet sum");
    }

    // Wavefold: second-order ADAA against the old point-sampled shape.
    for (const double rate : {44100.0, 48000.0, 96000.0}) {
        for (const int bin : {683, 1707, 2731}) {
            for (const float amount : {.28f, .5f, 1.0f}) {
                const auto fold = measure(Type::wavefold, amount, rate, bin);
                const auto direct = measureDirectWavefold(amount, rate, bin);
                const auto drive = measure(Type::drive, amount, rate, bin);
                std::printf("%5.0f Hz f0 %7.1f amt %.2f: Drive ADAA %.4f (%5.1f dB) | "
                            "Wavefold point %.4f (%5.1f dB, %6.1f dB abs) -> "
                            "Wavefold ADAA2 %.4f (%5.1f dB, %6.1f dB abs)\n",
                            rate, bin * rate / window, amount, drive.ratio, ratioDb(drive),
                            direct.ratio, ratioDb(direct), foldedDb(direct),
                            fold.ratio, ratioDb(fold), foldedDb(fold));
                check(std::isfinite(fold.ratio) && fold.ratio >= 0 && fold.ratio <= 1,
                      "wavefold alias measure in range");
                // Absolute folded power must stay a small fraction of the old
                // point-sampled folded power and of the input power itself.
                // Measured worst case is 8.3% of the old power and -14.2 dB
                // relative to a unit sine; the limits keep a clear margin while
                // still catching a silent fall back to point sampling.
                check(fold.aliased <= .20 * direct.aliased + 1e-12,
                      "antialiased Wavefold folds far less energy than point sampling");
                check(fold.aliased <= .10 * fold.inputPower,
                      "antialiased Wavefold keeps absolute folded energy low");
            }
        }
    }

    // The ADAA must not alter the static transfer function: a constant input
    // settles on exactly the old fold value, so presets keep their low-
    // frequency character.
    {
        vox::fx::effects::Chain chain;
        check(chain.prepare(48000) && chain.set(0, {Type::wavefold, 1, .5f}),
              "wavefold slot accepts settings");
        std::vector<float> left(512, .37f), right(left.size(), -.63f);
        chain.process(left.data(), right.data(), left.size());
        const auto expected = [](double x) {
            const double u = std::clamp(x * (1.0 + 7.0 * .5), -8.0, 8.0);
            return 2.0 / pi * std::asin(std::sin(pi * u * .5));
        };
        check(std::abs(left.back() - expected(.37)) < 1e-6,
              "wavefold DC transfer matches the old static shape");
        check(std::abs(right.back() - expected(-.63)) < 1e-6,
              "wavefold DC transfer matches on the second channel");
    }

    // Quasi-static sweep across the whole fold domain (both signs) must track
    // the old expression. ADAA introduces at most a sub-sample delay, so a
    // slowly rising ramp differs only by the local slope times the delay.
    for (const float amount : {.28f, 1.0f}) {
        vox::fx::effects::Chain chain;
        check(chain.prepare(48000) && chain.set(0, {Type::wavefold, 1, amount}),
              "wavefold static sweep prepare");
        constexpr int samples = 65536;
        std::vector<float> left(samples), right(samples);
        for (int i = 0; i < samples; ++i) {
            const float x = -2.0f + 4.0f * static_cast<float>(i) / (samples - 1);
            left[i] = x;
            right[i] = -x;
        }
        auto reference = left;
        chain.process(left.data(), right.data(), left.size());
        const auto expected = [amount](double x) {
            const double u = std::clamp(x * (1.0 + 7.0 * amount), -8.0, 8.0);
            return 2.0 / pi * std::asin(std::sin(pi * u * .5));
        };
        double error = 0;
        for (int i = 4096; i < samples; ++i) { // skip crossfade and amount smoothing
            error = std::max(error, std::abs(left[i] - expected(reference[i])));
            error = std::max(error, std::abs(right[i] - expected(-reference[i])));
        }
        check(error < 2e-3, "wavefold quasi-static sweep follows the old static shape");
    }

    // Latency impact. Second-order ADAA has no integer pre-delay and no
    // buffer, but the method itself carries p/2 = one sample of group delay.
    // Measure it in the wavefold's linear region (low level keeps the fold
    // inside |u|<=1) against the input phase. Drive keeps the smaller
    // effective delay of its parallel dry/nonlinear mix.
    {
        const auto groupDelay = [](Type type, float amount, double rate, int bin) {
            constexpr int samples = 32768;
            const auto settle = static_cast<std::size_t>(rate * .25);
            const double f0 = bin * rate / samples;
            std::vector<float> input(settle + samples), left, right;
            for (std::size_t i = 0; i < input.size(); ++i)
                input[i] = static_cast<float>(.1 * std::sin(2 * pi * f0 * i / rate));
            left = input;
            right = input;
            vox::fx::effects::Chain chain;
            chain.prepare(rate);
            chain.set(0, {type, 1, amount});
            chain.process(left.data(), right.data(), left.size());
            const auto amplitude = [&](const std::vector<float>& v) {
                double re = 0, im = 0;
                for (int i = 0; i < samples; ++i) {
                    const double w = 2 * pi * f0 * i / rate;
                    re += v[settle + static_cast<std::size_t>(i)] * std::cos(w);
                    im -= v[settle + static_cast<std::size_t>(i)] * std::sin(w);
                }
                return std::pair(re, im);
            };
            const auto in = amplitude(input), out = amplitude(left);
            double phase = std::atan2(out.second, out.first)
                         - std::atan2(in.second, in.first);
            while (phase > pi) phase -= 2 * pi;
            while (phase < -pi) phase += 2 * pi;
            return -phase / (2 * pi * f0 / rate);
        };
        for (const double rate : {44100.0, 48000.0, 96000.0}) {
            const auto wavefoldDelay = groupDelay(Type::wavefold, .28f, rate, 683);
            const auto driveDelay = groupDelay(Type::drive, .28f, rate, 683);
            std::printf("%5.0f Hz group delay: Wavefold %.3f samples, Drive %.3f samples\n",
                        rate, wavefoldDelay, driveDelay);
            check(wavefoldDelay > .9 && wavefoldDelay < 1.1,
                  "Wavefold adds one sample of ADAA group delay, not more");
            check(driveDelay >= 0 && driveDelay < .5,
                  "Drive keeps its parallel-path group delay");
        }
    }

    // Silence remains exactly silent and finite.
    {
        vox::fx::effects::Chain chain;
        check(chain.prepare(44100) && chain.set(0, {Type::wavefold, 1, .8f}),
              "wavefold silence prepare");
        std::vector<float> left(512), right(512);
        chain.process(left.data(), right.data(), left.size());
        double energy = 0;
        for (float value : left) { check(std::isfinite(value), "finite silent output"); energy += std::abs(value); }
        check(energy == 0, "wavefold silence stays exactly silent");
    }

    // Preset/codec compatibility: the type id, 6-bit state packing and the
    // FXSL v1 legacy decode are unchanged by the DSP change.
    {
        const Settings wavefold { Type::wavefold, .28f, .44f };
        check(static_cast<unsigned>(Type::wavefold) == 3,
              "wavefold type id is stable for FXSL");
        check(wavefold.valid(), "wavefold settings valid");
        const auto packed = vox::fx::effects::pack(wavefold);
        const auto restored = vox::fx::effects::unpack(packed);
        check(restored.type == Type::wavefold, "packed wavefold keeps its type");
        check(std::abs(restored.wet - .28f) <= 1.0f / 126.0f,
              "packed wavefold keeps wet within the 6-bit step");
        check(std::abs(restored.amount - .44f) <= 1.0f / 126.0f,
              "packed wavefold keeps amount within the 6-bit step");
        const auto legacy = vox::fx::effects::unpackLegacy(static_cast<std::uint16_t>(
            3u | (static_cast<unsigned>(std::lround(.28f * 63)) << 3)
               | (static_cast<unsigned>(std::lround(.44f * 127)) << 9)));
        check(legacy.type == Type::wavefold, "FXSL v1 wavefold still decodes");
        check(std::abs(legacy.wet - .28f) <= 1.0f / 126.0f
                  && std::abs(legacy.amount - .44f) <= 1.0f / 252.0f,
              "FXSL v1 wavefold values still decode");
    }

    std::printf("FX nonlinear alias: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
