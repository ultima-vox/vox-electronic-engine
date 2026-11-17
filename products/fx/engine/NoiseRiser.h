#pragma once

#include "Timeline.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace vox::fx {

// Interim layered Noise Riser. Programs are POD; install only while stopped or
// on the audio thread from a separately prepared handoff. Each event snapshots
// the layer settings at note-on; no pointer into UI or host state is retained.
enum class NoiseFamily : std::uint8_t { riser, downlifter, shepard, impact, whoosh, laser, squelch, metallic, texture };
struct NoiseLayer {
    NoiseFamily family = NoiseFamily::riser;
    bool enabled = false;
    timeline::LayerTime timing {};
    float gain = 1.0f, pan = 0.0f; // pan -1..1; center preserves original gain
    timeline::Curve gainCurve = timeline::Curve::constant(1.0f);
    timeline::Curve panCurve = timeline::Curve::constant(0.5f); // 0..1 -> -1..1 offset
    bool valid() const noexcept {
        return static_cast<unsigned>(family) <= static_cast<unsigned>(NoiseFamily::texture) &&
               timing.valid() && std::isfinite(gain) && gain >= 0 && gain <= 1 &&
               std::isfinite(pan) && pan >= -1 && pan <= 1 && gainCurve.valid() && panCurve.valid();
    }
};
struct NoiseProgram {
    std::array<NoiseLayer, 4> layers {};
    NoiseProgram() noexcept { layers[0].enabled = true; }
    bool valid() const noexcept {
        for (const auto& layer : layers) if (!layer.valid()) return false;
        return true;
    }
};

// Allocation-free one-shot events. The plugin currently uses a four-second
// event. Note-off is ignored in one-shot mode; all-sound-off fades over 5 ms.
class NoiseRiser {
public:
    void prepare(double sampleRate) noexcept {
        rate_ = std::isfinite(sampleRate) && sampleRate >= 8000.0 && sampleRate <= 384000.0 ? sampleRate : 44100.0;
        for (std::size_t i = 0; i < coefficients_.size(); ++i) {
            const double phase = static_cast<double>(i) / tableIntervals;
            const double cutoff = std::min(180.0 * std::exp(phase * std::log(10000.0 / 180.0)), rate_ * 0.45);
            coefficients_[i] = 1.0 - std::exp(-6.283185307179586 * cutoff / rate_);
            svfG_[i] = std::tan(3.141592653589793 * cutoff / rate_);
            pitchFactors_[i] = std::exp2(3.0 * (1.0 - phase));
            risingFactors_[i] = std::exp2(3.0 * phase);
            const double seconds = 8.0 * phase;
            impactPitch_[i] = std::exp(-22.0 * seconds);
            impactBody_[i] = std::exp(-2.0 * seconds);
            impactNoise_[i] = std::exp(-14.0 * seconds);
            squelchDecay_[i] = std::exp(-5.0 * seconds);
            for (std::size_t partial = 0; partial < shepardWeights_.size(); ++partial) {
                const double position = std::fmod(static_cast<double>(partial) + 3.0 * phase, 6.0);
                const double sine = std::sin(3.141592653589793 * position / 6.0);
                shepardWeights_[partial][i] = sine * sine;
            }
        }
        constexpr std::array<double, 8> ratios {1.0, 1.47, 2.08, 2.71, 3.93, 5.11, 6.48, 7.83};
        for (std::size_t i = 0; i < noteHz_.size(); ++i) {
            noteHz_[i] = 440.0 * std::exp2((static_cast<double>(i) - 69.0) / 12.0);
            for (std::size_t j = 0; j < ratios.size(); ++j) {
                auto& mode = modal_[i][j];
                const double frequency = noteHz_[i] * ratios[j];
                mode.enabled = frequency < rate_ * 0.45;
                const double radius = std::exp(-1.0 / (rate_ * (1.2 / (1.0 + j * 0.18))));
                mode.a1 = 2.0 * radius * std::cos(6.283185307179586 * frequency / rate_);
                mode.a2 = -radius * radius;
            }
        }
        voices_ = {}; retiring_ = {}; serial_ = 0;
    }
    // The caller must ensure there is no concurrent access. UI/worker threads
    // must not call this during rendering without a safe publication bridge.
    bool setProgram(const NoiseProgram& program) noexcept {
        if (!program.valid()) return false;
        program_ = program;
        return true;
    }
    // A ninth event fades the oldest in a dedicated bounded retirement slot.
    // If that slot is still fading, the incoming event is dropped.
    bool noteOn(int channel, int note, float velocity, double duration = 4.0) noexcept {
        timeline::SecondsClock clock;
        if (channel < 1 || channel > 16 || note < 0 || note > 127 || !std::isfinite(velocity) || velocity <= 0.0f ||
            !clock.start(duration, rate_)) return false;
        Voice* slot = nullptr;
        for (auto& voice : voices_) if (!voice.active) { slot = &voice; break; }
        if (!slot) {
            if (retiring_.active) return false;
            slot = &*std::min_element(voices_.begin(), voices_.end(),
                [](const Voice& a, const Voice& b) { return a.serial < b.serial; });
            retiring_ = *slot; beginRelease(retiring_);
        }
        *slot = {};
        slot->active = true; slot->channel = channel; slot->note = note;
        slot->velocity = std::clamp(velocity, 0.0f, 1.0f);
        slot->clock = clock; slot->duration = duration; slot->program = program_;
        slot->serial = ++serial_;
        int enabled = 0;
        for (std::size_t i = 0; i < slot->layers.size(); ++i) {
            enabled += slot->program.layers[i].enabled ? 1 : 0;
            auto& layer = slot->layers[i];
            layer.rng = 0x9e3779b9u ^ static_cast<std::uint32_t>(serial_ * 0x85ebca6bu) ^
                        static_cast<std::uint32_t>(i * 0x27d4eb2du);
            if (layer.rng == 0) layer.rng = 1;
        }
        slot->layerScale = 1.0 / std::max(1, enabled);
        return true;
    }
    void allSoundOff(int channel) noexcept {
        for (auto& v : voices_) if (v.active && (channel == 0 || v.channel == channel)) beginRelease(v);
        if (retiring_.active && (channel == 0 || retiring_.channel == channel)) beginRelease(retiring_);
    }
    // Both pointers must address at least count writable samples; outputs are
    // overwritten, not accumulated. The processor validates its bus layout.
    void render(float* left, float* right, int count) noexcept {
        for (int i = 0; i < count; ++i) {
            double l = 0, r = 0;
            for (auto& v : voices_) tick(v, l, r);
            tick(retiring_, l, r);
            left[i] = static_cast<float>(l * 0.055);
            right[i] = static_cast<float>(r * 0.055);
        }
    }
private:
    struct LayerState {
        std::uint32_t rng = 1;
        double lowpass = 0, dcInput = 0, dcOutput = 0, phase = 0, subPhase = 0;
        double svf1 = 0, svf2 = 0;
        std::array<double, 6> partialPhases {};
        std::array<double, 8> mode1 {}, mode2 {};
        bool modesStarted = false;
        std::array<float, 256> delay {};
        unsigned delayWrite = 0;
    };
    struct Voice {
        bool active = false;
        int channel = 0, note = 0;
        float velocity = 0;
        std::uint64_t serial = 0;
        timeline::SecondsClock clock;
        double duration = 4.0, layerScale = 1.0;
        int releaseRemaining = 0, releaseLength = 0;
        NoiseProgram program;
        std::array<LayerState, 4> layers {};
    };
    void beginRelease(Voice& v) noexcept {
        if (v.releaseRemaining != 0) return;
        v.releaseLength = std::max(1, static_cast<int>(rate_ * 0.005));
        v.releaseRemaining = v.releaseLength;
    }
    void tick(Voice& v, double& left, double& right) noexcept {
        if (!v.active) return;
        double eventPhase = 0;
        if (!v.clock.next(eventPhase)) { v.active = false; return; }
        double release = 1.0;
        if (v.releaseRemaining > 0) {
            release = static_cast<double>(v.releaseRemaining) / v.releaseLength;
            if (--v.releaseRemaining == 0) v.active = false;
        }
        for (std::size_t i = 0; i < v.layers.size(); ++i) {
            const auto& settings = v.program.layers[i];
            if (!settings.enabled) continue;
            const double offset = settings.timing.offset, span = settings.timing.span;
            if (eventPhase < offset || eventPhase >= offset + span) continue;
            const double t = (eventPhase - offset) / span;
            auto& layer = v.layers[i];
            layer.rng ^= layer.rng << 13; layer.rng ^= layer.rng >> 17; layer.rng ^= layer.rng << 5;
            const double noise = static_cast<double>(layer.rng) / 2147483648.0 - 1.0;
            const double position = std::min(t * tableIntervals, std::nextafter(static_cast<double>(tableIntervals), 0.0));
            const auto index = static_cast<std::size_t>(position);
            const double fraction = position - static_cast<double>(index);
            const bool descending = settings.family == NoiseFamily::downlifter;
            const bool movingBack = descending || (settings.family == NoiseFamily::whoosh && t >= 0.5);
            const double filterPosition = settings.family == NoiseFamily::whoosh ? (movingBack ? (2.0 - 2.0 * t) : 2.0 * t) : t;
            const double filterPos = std::min(filterPosition * tableIntervals, std::nextafter(static_cast<double>(tableIntervals), 0.0));
            const auto filterIndex = static_cast<std::size_t>(filterPos);
            const double filterFraction = filterPos - static_cast<double>(filterIndex);
            const auto coefficient = descending
                ? coefficients_[tableIntervals - index] + fraction * (coefficients_[tableIntervals - index - 1] - coefficients_[tableIntervals - index])
                : coefficients_[filterIndex] + filterFraction * (coefficients_[filterIndex + 1] - coefficients_[filterIndex]);
            layer.lowpass += coefficient * (noise - layer.lowpass);
            const double filtered = layer.lowpass - layer.dcInput + 0.995 * layer.dcOutput;
            layer.dcInput = layer.lowpass; layer.dcOutput = filtered;
            const double layerDuration = v.duration * span;
            const double fadeIn = std::min(1.0, t * layerDuration / ((settings.family == NoiseFamily::impact || settings.family == NoiseFamily::laser || settings.family == NoiseFamily::squelch || settings.family == NoiseFamily::metallic) ? 0.001 : 0.01));
            const double fadeOut = std::min(1.0, (1.0 - t) * layerDuration / 0.02);
            double source = filtered;
            double envelope = 0.15 + 0.85 * t;
            if (settings.family == NoiseFamily::texture) {
                // Two continuously phased detuned sine oscillators and
                // filtered noise, with slow level and stereo evolution.
                const double elapsed = t * layerDuration;
                const double frequency = std::min(noteHz_[static_cast<std::size_t>(v.note)], rate_ * 0.40);
                const double a = std::sin(6.283185307179586 * layer.phase);
                const double b = std::sin(6.283185307179586 * layer.subPhase);
                layer.phase += frequency / rate_; if (layer.phase >= 1.0) layer.phase -= 1.0;
                layer.subPhase += std::min(frequency * 1.007, rate_ * 0.45) / rate_; if (layer.subPhase >= 1.0) layer.subPhase -= 1.0;
                source = 0.3 * (a + b) + 0.15 * filtered;
                envelope = 0.65 + 0.15 * std::sin(6.283185307179586 * elapsed * 0.17);
            } else if (settings.family == NoiseFamily::metallic) {
                // Eight stable damped inharmonic modes excited once per layer.
                // Coefficients are prepared at the active sample rate; modes
                // beyond the anti-alias cutoff are omitted.
                const auto& modes = modal_[static_cast<std::size_t>(v.note)];
                source = 0.0;
                for (std::size_t mode = 0; mode < modes.size(); ++mode) {
                    if (!modes[mode].enabled) continue;
                    if (!layer.modesStarted) {
                        const double polarity = (layer.rng & (1u << mode)) ? 1.0 : -1.0;
                        layer.mode1[mode] = 0.4 * polarity;
                        layer.mode2[mode] = 0.5 * modes[mode].a1 * layer.mode1[mode];
                    }
                    const double next = modes[mode].a1 * layer.mode1[mode] + modes[mode].a2 * layer.mode2[mode];
                    layer.mode2[mode] = layer.mode1[mode]; layer.mode1[mode] = next;
                    source += next;
                }
                layer.modesStarted = true;
                source *= 1.0 / 8.0;
                envelope = 1.0;
            } else if (settings.family == NoiseFamily::squelch) {
                // Resonant TPT low-pass excited by a fast pitch-falling sine
                // plus a short noise burst; cutoff rises then falls in 300 ms.
                const double elapsed = t * layerDuration;
                const double pos = std::min(elapsed / 8.0 * tableIntervals, static_cast<double>(tableIntervals));
                const auto at = static_cast<std::size_t>(pos), next = std::min(at + 1, tableIntervals);
                const double f = pos - static_cast<double>(at);
                const auto read = [at, next, f] (const auto& table) { return table[at] + f * (table[next] - table[at]); };
                const double frequency = std::min(noteHz_[static_cast<std::size_t>(v.note)] * (1.0 + 3.0 * read(impactPitch_)), 0.4 * rate_);
                const double exciter = 0.7 * std::sin(6.283185307179586 * layer.phase) + 0.3 * noise * read(impactNoise_);
                layer.phase += frequency / rate_; if (layer.phase >= 1.0) layer.phase -= 1.0;
                const double sweep = std::clamp(1.0 - std::abs(elapsed / 0.15 - 1.0), 0.0, 1.0);
                const double gp = std::min(sweep * tableIntervals, std::nextafter(static_cast<double>(tableIntervals), 0.0));
                const auto gi = static_cast<std::size_t>(gp);
                const double g = svfG_[gi] + (gp - static_cast<double>(gi)) * (svfG_[gi+1] - svfG_[gi]);
                constexpr double damping = 0.28;
                const double a1 = 1.0 / (1.0 + g * (g + damping));
                const double v3 = exciter - layer.svf2;
                const double v1 = a1 * layer.svf1 + g * a1 * v3;
                const double v2 = layer.svf2 + g * v1;
                layer.svf1 = 2.0 * v1 - layer.svf1;
                layer.svf2 = 2.0 * v2 - layer.svf2;
                source = 0.35 * v2;
                envelope = read(squelchDecay_);
            } else if (settings.family == NoiseFamily::laser) {
                // Fast exponential chirp with a short synthetic noise tick.
                // Rate-independent prepared curves avoid per-sample exp().
                const double elapsed = t * layerDuration;
                const double pos = std::min(elapsed / 8.0 * tableIntervals, static_cast<double>(tableIntervals));
                const auto at = static_cast<std::size_t>(pos);
                const auto next = std::min(at + 1, tableIntervals);
                const double f = pos - static_cast<double>(at);
                const auto read = [at, next, f] (const auto& table) { return table[at] + f * (table[next] - table[at]); };
                const double frequency = std::min(noteHz_[static_cast<std::size_t>(v.note)] * (2.0 + 12.0 * read(impactPitch_)), 0.40 * rate_);
                source = 0.8 * std::sin(6.283185307179586 * layer.phase) + 0.2 * noise * read(impactNoise_);
                layer.phase += frequency / rate_; if (layer.phase >= 1.0) layer.phase -= 1.0;
                envelope = read(impactNoise_);
            } else if (settings.family == NoiseFamily::whoosh) {
                // Bounded variable delay on this layer's own signal. Its
                // slowly moving read tap supplies a subtle Doppler shift.
                constexpr unsigned mask = 255;
                const double delaySamples = 1.0 + t * (std::min(254.0, rate_ * 0.002) - 1.0);
                const auto whole = static_cast<unsigned>(delaySamples);
                const double mix = delaySamples - whole;
                const auto read0 = (layer.delayWrite - whole) & mask;
                const auto read1 = (read0 - 1) & mask;
                const double delayed = layer.delay[read0] * (1.0 - mix) + layer.delay[read1] * mix;
                layer.delay[layer.delayWrite] = static_cast<float>(filtered);
                layer.delayWrite = (layer.delayWrite + 1) & mask;
                const double frequency = std::min(noteHz_[static_cast<std::size_t>(v.note)] * (2.0 - 1.5 * t), rate_ * 0.45);
                const double tonal = std::sin(6.283185307179586 * layer.phase);
                layer.phase += frequency / rate_; if (layer.phase >= 1.0) layer.phase -= 1.0;
                source = 0.65 * delayed + 0.2 * tonal;
                const double window = std::sin(3.141592653589793 * t);
                envelope = window * window;
            } else if (settings.family == NoiseFamily::impact) {
                // Decaying sine body with a fast pitch fall, separate sub,
                // and short noise exciter. Tables are prepared off-thread.
                const double elapsed = t * layerDuration;
                const double pos = std::min(elapsed / 8.0 * tableIntervals, static_cast<double>(tableIntervals));
                const auto at = static_cast<std::size_t>(pos);
                const auto next = std::min(at + 1, tableIntervals);
                const double f = pos - static_cast<double>(at);
                const auto read = [at, next, f] (const auto& table) {
                    return table[at] + f * (table[next] - table[at]);
                };
                const double fundamental = std::clamp(noteHz_[static_cast<std::size_t>(v.note)] * 0.19, 30.0, 160.0);
                const double frequency = std::min(fundamental * (1.0 + 3.0 * read(impactPitch_)), rate_ * 0.45);
                const double body = std::sin(6.283185307179586 * layer.phase);
                const double sub = std::sin(6.283185307179586 * layer.subPhase);
                layer.phase += frequency / rate_; if (layer.phase >= 1.0) layer.phase -= 1.0;
                layer.subPhase += fundamental * 0.5 / rate_; if (layer.subPhase >= 1.0) layer.subPhase -= 1.0;
                source = (0.65 * body + 0.20 * sub) * read(impactBody_) + 0.25 * noise * read(impactNoise_);
                envelope = 1.0;
            } else if (settings.family == NoiseFamily::shepard) {
                // Six octave-spaced sine partials traverse a six-octave
                // window. At each wrap, the Hann-like weight is zero, so
                // resetting the frequency by six octaves is masked.
                const double factor = risingFactors_[index] + fraction * (risingFactors_[index + 1] - risingFactors_[index]);
                source = 0.0;
                for (std::size_t partial = 0; partial < layer.partialPhases.size(); ++partial) {
                    double octave = std::ldexp(1.0, static_cast<int>(partial) - 3) * factor;
                    if (static_cast<double>(partial) + 3.0 * t >= 6.0) octave *= 1.0 / 64.0;
                    const double frequency = noteHz_[static_cast<std::size_t>(v.note)] * octave;
                    const double weight = shepardWeights_[partial][index] + fraction * (shepardWeights_[partial][index + 1] - shepardWeights_[partial][index]);
                    auto& phase = layer.partialPhases[partial];
                    // Fade partials to zero before Nyquist rather than abruptly
                    // gating a moving partial as it crosses the cutoff.
                    const double antiAlias = std::clamp((0.45 * rate_ - frequency) / (0.05 * rate_), 0.0, 1.0);
                    source += weight * antiAlias * std::sin(6.283185307179586 * phase);
                    phase += std::min(frequency, 0.45 * rate_) / rate_;
                    if (phase >= 1.0) phase -= 1.0;
                }
                source *= 1.0 / 3.0;
                envelope = 0.8;
            } else if (descending) {
                const double pitchFactor = pitchFactors_[index] + fraction * (pitchFactors_[index + 1] - pitchFactors_[index]);
                const double frequency = std::min(noteHz_[static_cast<std::size_t>(v.note)] * pitchFactor, 0.45 * rate_);
                const double tonal = std::sin(6.283185307179586 * layer.phase);
                layer.phase += frequency / rate_;
                if (layer.phase >= 1.0) layer.phase -= 1.0;
                source = 0.5 * filtered + 0.5 * tonal;
                envelope = 1.0 - 0.85 * t;
            }
            const double sample = source * v.velocity * envelope * fadeIn * fadeOut * release *
                                  v.layerScale * settings.gain * settings.gainCurve.evaluate(static_cast<float>(t));
            const double pan = std::clamp(static_cast<double>(settings.pan) +
                                          2.0 * settings.panCurve.evaluate(static_cast<float>(t)) - 1.0 +
                                          (settings.family == NoiseFamily::whoosh ? 2.0 * t - 1.0 : 0.0) +
                                          (settings.family == NoiseFamily::texture ? 0.35 * std::sin(6.283185307179586 * t * layerDuration * 0.11) : 0.0), -1.0, 1.0);
            left += sample * std::min(1.0, 1.0 - pan);
            right += sample * std::min(1.0, 1.0 + pan);
        }
    }
    static constexpr std::size_t tableIntervals = 4096;
    std::array<double, tableIntervals + 1> coefficients_ {}, pitchFactors_ {}, risingFactors_ {};
    std::array<double, tableIntervals + 1> impactPitch_ {}, impactBody_ {}, impactNoise_ {};
    std::array<double, tableIntervals + 1> svfG_ {}, squelchDecay_ {};
    std::array<std::array<double, tableIntervals + 1>, 6> shepardWeights_ {};
    struct ModalCoefficient { double a1 = 0, a2 = 0; bool enabled = false; };
    std::array<double, 128> noteHz_ {};
    std::array<std::array<ModalCoefficient, 8>, 128> modal_ {};
    NoiseProgram program_;
    double rate_ = 44100.0;
    std::uint64_t serial_ = 0;
    std::array<Voice, 8> voices_ {};
    Voice retiring_ {};
};
} // namespace vox::fx
