#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace vox::fx::timeline {

// Segment shape belongs to the left node; the final node's shape is ignored.
// Exponential is a quadratic ease-in and is well-defined even at zero.
enum class Shape : std::uint8_t { linear, smooth, exponential, hold };
struct Node { float time = 0.0f, value = 0.0f; Shape shape = Shape::linear; };

// POD, prepared/validated away from the callback. No duplicate times are
// allowed, endpoints are exactly 0 and 1, and all values are normalized.
struct Curve {
    std::array<Node, 16> nodes {};
    std::uint8_t count = 2;

    static Curve constant(float value) noexcept {
        Curve c;
        c.nodes[0] = {0.0f, value, Shape::linear};
        c.nodes[1] = {1.0f, value, Shape::linear};
        return c;
    }
    bool valid() const noexcept {
        if (count < 2 || count > nodes.size() || nodes[0].time != 0.0f || nodes[count - 1].time != 1.0f) return false;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& n = nodes[i];
            if (!std::isfinite(n.time) || !std::isfinite(n.value) || n.value < 0.0f || n.value > 1.0f ||
                static_cast<unsigned>(n.shape) > static_cast<unsigned>(Shape::hold)) return false;
            if (i > 0 && n.time <= nodes[i - 1].time) return false;
        }
        return true;
    }
    // Precondition: valid(), phase finite. Fixed maximum of 15 segments.
    float evaluate(float phase) const noexcept {
        if (phase <= 0.0f) return nodes[0].value;
        if (phase >= 1.0f) return nodes[count - 1].value;
        for (std::size_t i = 0; i + 1 < count; ++i) {
            const Node& a = nodes[i]; const Node& b = nodes[i + 1];
            if (phase < b.time) {
                float x = (phase - a.time) / (b.time - a.time);
                switch (a.shape) {
                    case Shape::smooth: x = x * x * (3.0f - 2.0f * x); break;
                    case Shape::exponential: x *= x; break;
                    case Shape::hold: x = 0.0f; break;
                    case Shape::linear: break;
                }
                return a.value + (b.value - a.value) * x;
            }
        }
        return nodes[count - 1].value;
    }
};

// Per-layer offset and span are fractions of the event. A layer is active in
// [offset, offset + span); a zero-length layer is invalid, not silently muted.
struct LayerTime {
    float offset = 0.0f, span = 1.0f;
    bool valid() const noexcept {
        return std::isfinite(offset) && std::isfinite(span) && offset >= 0.0f &&
               span > 0.0f && offset < 1.0f && offset + span <= 1.0f;
    }
    bool phaseAt(double eventPhase, float& localPhase) const noexcept {
        if (eventPhase < offset || eventPhase >= static_cast<double>(offset) + span) return false;
        localPhase = static_cast<float>((eventPhase - offset) / span);
        return true;
    }
};

// Seconds clock: exact integer sample counter and a double-precision fractional
// duration. A new event snapshots its duration; later edits affect new events.
class SecondsClock {
public:
    bool start(double seconds, double sampleRate) noexcept {
        if (!std::isfinite(seconds) || seconds < 0.05 || seconds > 32.0 ||
            !std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 384000) return false;
        length_ = seconds * sampleRate; age_ = 0; return true;
    }
    bool next(double& phase) noexcept {
        if (static_cast<double>(age_) >= length_) return false;
        phase = static_cast<double>(age_++) / length_; return true;
    }
private:
    double length_ = 0;
    std::uint64_t age_ = 0;
};

// Bars clock integrates beat increments, so tempo changes affect only future
// samples and never reinterpret phase already played. Denominator is the
// notated time-signature denominator (4 = quarter note). Callers explicitly
// reset/retrigger on transport discontinuity; it does not query the host.
class BarsClock {
public:
    bool start(double bars, int numerator, int denominator, double sampleRate) noexcept {
        if (!std::isfinite(bars) || bars <= 0 || bars > 32 || numerator < 1 || numerator > 32 ||
            denominator < 1 || denominator > 32 || (denominator & (denominator - 1)) != 0 ||
            !std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 384000) return false;
        beats_ = bars * numerator * 4.0 / denominator; rate_ = sampleRate; elapsed_ = 0; compensation_ = 0; return true;
    }
    // Invalid host tempo leaves the clock untouched; no invented BPM fallback.
    bool next(double bpm, double& phase) noexcept {
        if (!std::isfinite(bpm) || bpm <= 0 || bpm > 1000 || beats_ <= 0 || elapsed_ >= beats_) return false;
        phase = elapsed_ / beats_;
        // Compensated summation avoids a multi-sample boundary drift on long,
        // low-tempo events without allocating or retaining a rendered buffer.
        const double increment = bpm / (60.0 * rate_);
        const double corrected = increment - compensation_;
        const double sum = elapsed_ + corrected;
        compensation_ = (sum - elapsed_) - corrected;
        elapsed_ = sum;
        return true;
    }
private:
    double beats_ = 0, rate_ = 44100, elapsed_ = 0, compensation_ = 0;
};
} // namespace vox::fx::timeline
