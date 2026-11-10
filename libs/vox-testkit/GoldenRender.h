#pragma once

// Golden-render helpers for suite tests (FOUND-02). Test-only; allocates.
//
// A golden comparison has two layers:
//  1. Source parity: ported DSP files are byte-identical to their reviewed
//     source (PROVENANCE.csv), so the same compiler renders the same audio.
//  2. Render manifest: per-render metrics (peak, RMS, DC, spectral centroid)
//     recorded in a CSV and compared with documented tolerances, so a later
//     change to the DSP, or a compiler that renders differently, is caught.
//     WAV files are written for listening and CI artifacts, not committed.

#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vox::testkit {

struct Metrics {
    double peak = 0.0;
    double rms = 0.0;
    double dc = 0.0;
    double centroidHz = 0.0;
    bool finite = true;
};

// Metrics of one channel. The centroid uses a Hann-windowed FFT of the first
// 2^16 frames (zero-padded).
Metrics measure (std::span<const float> samples, double sampleRate);

// Largest absolute sample difference; infinity when the sizes differ.
double maxAbsDifference (std::span<const float> a, std::span<const float> b);

// 32-bit float WAV, interleaved from planar channels of equal length.
bool writeWav (const std::string& path, const std::vector<std::vector<float>>& channels,
               double sampleRate);

// Magnitude spectrum (bins 0..n/2) of the first n frames, n a power of two,
// Hann window. Test-only, O(n log n).
std::vector<double> magnitudeSpectrum (std::span<const float> samples, std::size_t n);

struct ManifestRow {
    Metrics left, right;
};

// CSV: key,peakL,rmsL,dcL,centroidL,peakR,rmsR,dcR,centroidR (one header line).
std::map<std::string, ManifestRow> readManifest (const std::string& path);
bool writeManifest (const std::string& path, const std::map<std::string, ManifestRow>& rows);

struct Tolerance {
    double relative = 1e-3;          // peak and RMS
    double absoluteFloor = 1e-6;     // for values near zero
    double centroidRelative = 1e-2;
};

// Empty when equal within tolerance, else a description of the first mismatch.
std::optional<std::string> compare (const Metrics& expected, const Metrics& actual,
                                    const Tolerance& tolerance = {});

} // namespace vox::testkit
