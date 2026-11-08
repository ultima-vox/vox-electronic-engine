#pragma once
// Vox DSP Core primitives (#75 phase 1): small, deterministic, allocation-free
// building blocks shared by graph instruments. Plain structs, no JUCE, no
// virtual calls; every function is safe on the audio thread. Each primitive
// reproduces an existing, verified implementation exactly (here: the native
// Vox Demo Synth) so instruments can move onto the core with audio parity.
#include <cmath>
#include <cstdint>

namespace vstengine::dspcore {

inline constexpr double pi = 3.14159265358979323846;

// One-pole smoothing that lands exactly on its target (no ulp limbo).
struct Smoothed {
  double value{};
  double coeff{};
  void prepare(double sampleRate, double seconds, double initial) noexcept {
    coeff = 1.0 - std::exp(-1.0 / (seconds * sampleRate));
    value = initial;
  }
  void snap(double target) noexcept { value = target; }
  double next(double target) noexcept {
    const double v = value + coeff * (target - value);
    value = std::fabs(target - v) <= 1e-9 * std::fmax(1.0, std::fabs(target)) ? target : v;
    return value;
  }
};

// Band-limited oscillator (polyBLEP): sine, saw, square. Phase in [0, 1).
struct BlepOscillator {
  enum Wave { sine = 0, saw = 1, square = 2 };
  double phase{};
  static double polyBlep(double t, double dt) noexcept {
    if (t < dt) {
      t /= dt;
      return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
      t = (t - 1.0) / dt;
      return t * t + t + t + 1.0;
    }
    return 0.0;
  }
  void reset() noexcept { phase = 0.0; }
  // One sample at `dt` = frequency / sampleRate; advances the phase.
  double next(int wave, double dt) noexcept {
    double out;
    switch (wave) {
    case sine:
      out = std::sin(2.0 * pi * phase);
      break;
    case square:
      out = (phase < 0.5 ? 1.0 : -1.0) + polyBlep(phase, dt) -
            polyBlep(std::fmod(phase + 0.5, 1.0), dt);
      break;
    default:
      out = 2.0 * phase - 1.0 - polyBlep(phase, dt);
      break;
    }
    phase += dt;
    phase -= std::floor(phase);
    return out;
  }
};

// TPT state-variable low-pass (Zavalishin). resonance 0..1 maps to damping
// k = 2 - 1.9 * resonance; cutoff is clamped below 0.45 * sampleRate.
struct SvfLowpass {
  double ic1{}, ic2{};
  void reset() noexcept { ic1 = ic2 = 0.0; }
  double next(double x, double cutoffHz, double resonance, double sampleRate) noexcept {
    const double fc = std::fmin(cutoffHz, 0.45 * sampleRate);
    const double g = std::tan(pi * fc / sampleRate);
    const double k = 2.0 - 1.9 * resonance;
    const double a1 = 1.0 / (1.0 + g * (g + k));
    const double v3 = x - ic2;
    const double v1 = a1 * ic1 + g * a1 * v3;
    const double v2 = ic2 + g * v1;
    ic1 = 2.0 * v1 - ic1;
    ic2 = 2.0 * v2 - ic2;
    return v2;
  }
};

// Linear ADSR in seconds. Attack restarts from the current level (no click);
// release falls linearly from wherever it starts. idle() means silent.
struct LinearAdsr {
  enum Stage { idleStage, attack, decay, sustain, release };
  Stage stage{idleStage};
  double level{}, releaseStep{};
  bool idle() const noexcept { return stage == idleStage; }
  void kill() noexcept {
    stage = idleStage;
    level = 0.0;
  }
  void gate() noexcept { stage = attack; }
  void startRelease(double releaseSeconds, double sampleRate) noexcept {
    if (stage == idleStage || stage == release)
      return;
    stage = release;
    releaseStep = level / (releaseSeconds * sampleRate);
  }
  double next(double a, double d, double s, double sampleRate) noexcept {
    switch (stage) {
    case attack:
      level += 1.0 / (a * sampleRate);
      if (level >= 1.0) {
        level = 1.0;
        stage = decay;
      }
      break;
    case decay:
      level -= (1.0 - s) / (d * sampleRate);
      if (level <= s) {
        level = s;
        stage = sustain;
      }
      break;
    case sustain:
      level = s;
      break;
    case release:
      level -= releaseStep;
      if (level <= 0.0) {
        level = 0.0;
        stage = idleStage;
      }
      break;
    default:
      level = 0.0;
      break;
    }
    return level;
  }
};

inline double midiNoteHz(int note) noexcept {
  return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

} // namespace vstengine::dspcore
