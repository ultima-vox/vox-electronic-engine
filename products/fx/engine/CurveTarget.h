#pragma once

#include "NoiseRiser.h"
#include <optional>

namespace vox::fx {
enum class CurveKind { gain, pitch, cutoff, pan };

// nullopt selects the Program trajectory, not layer A. Keep the target
// mapping shared between controller operations and testable without JUCE.
struct CurveTarget {
    std::optional<std::size_t> layer;
    CurveKind kind = CurveKind::gain;

    template <typename Program>
    auto* get(Program& program) const noexcept {
        using Pointer = decltype(&program.amplitudeCurve);
        if (layer) {
            if (*layer >= program.layers.size()) return Pointer{};
            auto& l = program.layers[*layer];
            switch (kind) {
                case CurveKind::gain: return &l.gainCurve;
                case CurveKind::pitch: return &l.pitchCurve;
                case CurveKind::cutoff: return &l.cutoffCurve;
                case CurveKind::pan: return &l.panCurve;
            }
        } else {
            switch (kind) {
                case CurveKind::gain: return &program.amplitudeCurve;
                case CurveKind::pitch: return &program.pitchCurve;
                case CurveKind::cutoff: return &program.cutoffCurve;
                case CurveKind::pan: return &program.panoramaCurve;
            }
        }
        return Pointer{};
    }

    float defaultValue() const noexcept { return kind == CurveKind::gain ? 1.0f : 0.5f; }
};
} // namespace vox::fx
