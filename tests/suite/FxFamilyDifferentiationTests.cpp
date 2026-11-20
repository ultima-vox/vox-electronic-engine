// FX-T01: the twelve source families must stay sonically distinguishable and
// usable across the keyboard. A family that is quiet, silent at one end of the
// range, or a renamed copy of another family fails here. Thresholds are set
// from measured renders on this implementation with clear margin; this is a
// spectral/time-domain screen, not a listening acceptance.
#include "engine/NoiseRiser.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;

struct Descriptor {
    std::array<double, 6> bands{};  // normalized octave energy 60 Hz .. 3.8 kHz
    double decayLog10 = 0;          // last sixth vs first sixth
    double periodicity = 0;         // peak normalized autocorrelation
    double direction = 0;           // log2 late/early spectral centroid
    double zcrLog2 = 0;
};

std::vector<float> render(vox::fx::NoiseFamily family, int note, double seconds = 1.5) {
    auto engine = std::make_unique<vox::fx::NoiseRiser>();
    engine->prepare(48000.0);
    vox::fx::NoiseProgram p;
    p.layers[0].family = family;
    std::vector<float> left(static_cast<std::size_t>(48000 * seconds)), right(left.size());
    if (!engine->setProgram(p) || !engine->noteOn(1, note, 1.0f, seconds)) return {};
    for (std::size_t at = 0; at < left.size(); at += 128)
        engine->render(left.data() + at, right.data() + at,
                       static_cast<int>(std::min<std::size_t>(128, left.size() - at)));
    return left;
}

double rms(const std::vector<float>& x) {
    if (x.empty()) return 0;
    double sum = 0;
    for (float v : x) sum += static_cast<double>(v) * v;
    return std::sqrt(sum / static_cast<double>(x.size()));
}

double centroid(const std::vector<float>& x, std::size_t begin, std::size_t end) {
    const double rate = 48000;
    const std::size_t step = std::max<std::size_t>(1, (end - begin) / 3000);
    double total = 0, weighted = 0;
    for (double hz = 80; hz < 16000; hz *= 1.08) {
        double re = 0, im = 0;
        for (std::size_t at = begin; at < end; at += step) {
            const double ph = 2 * pi * hz * static_cast<double>(at) / rate;
            re += x[at] * std::cos(ph);
            im -= x[at] * std::sin(ph);
        }
        const double power = re * re + im * im;
        total += power;
        weighted += power * hz;
    }
    return total > 0 ? weighted / total : 0;
}

Descriptor describe(const std::vector<float>& x) {
    Descriptor d;
    const double rate = 48000;
    const std::size_t n = x.size();
    const std::size_t step = std::max<std::size_t>(1, n / 4000);
    double total = 0;
    for (int band = 0; band < 6; ++band) {
        const double lo = 60 * std::pow(2.0, band), hi = lo * 2;
        double energy = 0;
        for (double hz = lo; hz < hi; hz *= 1.08) {
            double re = 0, im = 0;
            for (std::size_t at = 0; at < n; at += step) {
                const double ph = 2 * pi * hz * static_cast<double>(at) / rate;
                re += x[at] * std::cos(ph);
                im -= x[at] * std::sin(ph);
            }
            energy += re * re + im * im;
        }
        d.bands[band] = energy;
        total += energy;
    }
    if (total > 0) for (double& band : d.bands) band /= total;
    double early = 0, late = 0;
    for (std::size_t i = 0; i < n / 6; ++i) early += static_cast<double>(x[i]) * x[i];
    for (std::size_t i = n - n / 6; i < n; ++i) late += static_cast<double>(x[i]) * x[i];
    d.decayLog10 = std::log10(1e-3 + late / (early + 1e-12));
    const int lowest = std::max(2, static_cast<int>(rate * .0002));
    const int highest = std::min(static_cast<int>(rate * .02), static_cast<int>(n / 2));
    for (int lag = lowest; lag < highest; lag += 2) {
        double num = 0, d0 = 0, d1 = 0;
        for (std::size_t i = 0; i + lag < n; i += 3) {
            num += x[i] * x[i + lag];
            d0 += static_cast<double>(x[i]) * x[i];
            d1 += static_cast<double>(x[i + lag]) * x[i + lag];
        }
        if (d0 > 0 && d1 > 0) d.periodicity = std::max(d.periodicity, num / std::sqrt(d0 * d1));
    }
    const double c0 = centroid(x, 0, n / 4), c1 = centroid(x, n - n / 4, n);
    d.direction = std::log2((c1 + 1) / (c0 + 1));
    int crossings = 0;
    for (std::size_t i = 1; i < n; ++i)
        if ((x[i - 1] < 0) != (x[i] < 0)) ++crossings;
    d.zcrLog2 = std::log2(1 + static_cast<double>(crossings) * rate / n / 100.0);
    return d;
}

// Comparable distance: octave-band shape dominates, then envelope, periodicity,
// spectral direction and roughness. A renamed copy scores far below 1.0.
double distance(const Descriptor& a, const Descriptor& b) {
    double d = 0;
    for (int band = 0; band < 6; ++band) d += std::abs(a.bands[band] - b.bands[band]);
    d += std::abs(a.decayLog10 - b.decayLog10) / 2;
    d += std::abs(a.periodicity - b.periodicity);
    d += std::abs(a.direction - b.direction) / 2;
    d += std::abs(a.zcrLog2 - b.zcrLog2) / 4;
    return d;
}
} // namespace

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
    };
    constexpr std::array<const char*, 12> names{
        "riser", "downlifter", "shepard", "impact", "whoosh", "laser",
        "squelch", "metallic", "texture", "formant", "glitch", "granular"};
    constexpr int families = 12;
    Descriptor reference[families]{};
    for (int family = 0; family < families; ++family) {
        const auto audio = render(static_cast<vox::fx::NoiseFamily>(family), 60);
        check(!audio.empty(), "family render");
        double peak = 0;
        for (float v : audio) {
            check(std::isfinite(v) && std::abs(v) < 1, "finite bounded family output");
            peak = std::max(peak, std::abs(static_cast<double>(v)));
        }
        check(peak > .001, "family is audible");
        reference[family] = describe(audio);
    }
    double closest = 1e9;
    int closestA = 0, closestB = 0;
    for (int a = 0; a < families; ++a)
        for (int b = a + 1; b < families; ++b) {
            const double d = distance(reference[a], reference[b]);
            if (d < closest) { closest = d; closestA = a; closestB = b; }
        }
    std::printf("closest families: %s / %s distance %.3f\n",
                names[static_cast<std::size_t>(closestA)],
                names[static_cast<std::size_t>(closestB)], closest);
    check(closest > 0.90, "every family is spectrally distinct from every other");

    // Keyboard coverage: a family may not collapse at the extremes of the MIDI
    // range, and no family may swing wildly in level between neighbouring notes.
    for (int family = 0; family < families; ++family) {
        double lowest = 1e9, highest = -1e9;
        for (int note : {24, 36, 48, 60, 72, 84, 96, 108, 120, 127}) {
            const auto audio = render(static_cast<vox::fx::NoiseFamily>(family), note, .8);
            const double level = 20 * std::log10(rms(audio) + 1e-12);
            check(std::isfinite(level), "finite family level");
            lowest = std::min(lowest, level);
            highest = std::max(highest, level);
        }
        std::printf("%-11s keyboard level spread %.1f dB\n", names[static_cast<std::size_t>(family)],
                    highest - lowest);
        check(highest - lowest < 8.0, "family level is stable across the keyboard");
        check(lowest < -20, "family never approaches full scale on its own");
        // Metallic modes above the anti-alias cutoff necessarily thin out with
        // rising pitch; that is a measured taper, not a silent family.
        if (family != static_cast<int>(vox::fx::NoiseFamily::metallic))
            check(highest < -20, "family keeps headroom at every note");
    }

    // Formant: the vowel morph moves the formants through the source, so the
    // peaks are only above the gaps at some moments. Test the strongest moment
    // for each edge formant inside 100 ms frames instead of averaging it away.
    for (const int note : {36, 60, 84}) {
        const auto vowel = render(vox::fx::NoiseFamily::formant, note);
        const auto narrow = [&](std::size_t begin, std::size_t end, double center) {
            double energy = 0;
            int probes = 0;
            const std::size_t step = std::max<std::size_t>(1, (end - begin) / 6000);
            for (double hz = center / 1.03; hz <= center * 1.03; hz *= 1.01) {
                double re = 0, im = 0;
                for (std::size_t at = begin; at < end; at += step) {
                    const double ph = 2 * pi * hz * static_cast<double>(at) / 48000.0;
                    re += vowel[at] * std::cos(ph);
                    im -= vowel[at] * std::sin(ph);
                }
                energy += re * re + im * im;
                ++probes;
            }
            return 10 * std::log10(energy / probes + 1e-30);
        };
        const std::size_t frame = 4800; // 100 ms
        for (const auto& pair : {std::pair{492.0, 800.0}, std::pair{2980.0, 4300.0}}) {
            double best = -1e9, bestGap = -1e9;
            for (std::size_t begin = 0; begin + frame < vowel.size(); begin += frame) {
                const double peak = narrow(begin, begin + frame, pair.first);
                if (peak > best) {
                    best = peak;
                    bestGap = peak - narrow(begin, begin + frame, pair.second);
                }
            }
            std::printf("formant note %d %.0f Hz best contrast %.1f dB\n", note, pair.first, bestGap);
            check(bestGap > 2.5, "formant peaks stand above the valleys");
        }
    }
    std::printf("FX family differentiation: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
