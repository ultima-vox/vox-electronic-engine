#include "GoldenRender.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>

namespace vox::testkit {
namespace {

constexpr double pi = 3.14159265358979323846;

void fft (std::vector<std::complex<double>>& a)
{
    const auto n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (std::size_t length = 2; length <= n; length <<= 1)
    {
        const double angle = -2.0 * pi / static_cast<double> (length);
        const std::complex<double> step (std::cos (angle), std::sin (angle));
        for (std::size_t i = 0; i < n; i += length)
        {
            std::complex<double> w (1.0, 0.0);
            for (std::size_t k = 0; k < length / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + length / 2] * w;
                a[i + k] = u + v;
                a[i + k + length / 2] = u - v;
                w *= step;
            }
        }
    }
}

void putU16 (std::ofstream& out, std::uint16_t v)
{
    const char b[2] = { static_cast<char> (v & 0xff), static_cast<char> (v >> 8) };
    out.write (b, 2);
}

void putU32 (std::ofstream& out, std::uint32_t v)
{
    const char b[4] = { static_cast<char> (v & 0xff), static_cast<char> ((v >> 8) & 0xff),
                        static_cast<char> ((v >> 16) & 0xff), static_cast<char> (v >> 24) };
    out.write (b, 4);
}

bool near (double expected, double actual, double relative, double floor)
{
    return std::fabs (expected - actual) <= std::max (floor, relative * std::fabs (expected));
}

} // namespace

std::vector<double> magnitudeSpectrum (std::span<const float> samples, std::size_t n)
{
    std::vector<std::complex<double>> data (n);
    for (std::size_t i = 0; i < n && i < samples.size(); ++i)
    {
        const double window = 0.5 - 0.5 * std::cos (2.0 * pi * static_cast<double> (i) / static_cast<double> (n - 1));
        data[i] = { samples[i] * window, 0.0 };
    }
    fft (data);
    std::vector<double> magnitude (n / 2 + 1);
    for (std::size_t k = 0; k < magnitude.size(); ++k)
        magnitude[k] = std::abs (data[k]);
    return magnitude;
}

Metrics measure (std::span<const float> samples, double sampleRate)
{
    Metrics m;
    double sum = 0.0, squares = 0.0;
    for (float s : samples)
    {
        if (! std::isfinite (s))
        {
            m.finite = false;
            continue;
        }
        m.peak = std::max (m.peak, static_cast<double> (std::fabs (s)));
        sum += s;
        squares += static_cast<double> (s) * s;
    }
    if (! samples.empty())
    {
        m.dc = sum / static_cast<double> (samples.size());
        m.rms = std::sqrt (squares / static_cast<double> (samples.size()));
    }
    if (m.finite && m.peak > 0.0)
    {
        constexpr std::size_t n = 1u << 16;
        const auto spectrum = magnitudeSpectrum (samples, n);
        double weighted = 0.0, total = 0.0;
        for (std::size_t k = 1; k < spectrum.size(); ++k)
        {
            const double power = spectrum[k] * spectrum[k];
            weighted += power * static_cast<double> (k) * sampleRate / static_cast<double> (n);
            total += power;
        }
        m.centroidHz = total > 0.0 ? weighted / total : 0.0;
    }
    return m;
}

double maxAbsDifference (std::span<const float> a, std::span<const float> b)
{
    if (a.size() != b.size())
        return std::numeric_limits<double>::infinity();
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, static_cast<double> (std::fabs (a[i] - b[i])));
    return worst;
}

bool writeWav (const std::string& path, const std::vector<std::vector<float>>& channels,
               double sampleRate)
{
    if (channels.empty())
        return false;
    const auto frames = channels.front().size();
    for (const auto& c : channels)
        if (c.size() != frames)
            return false;
    std::ofstream out (path, std::ios::binary);
    if (! out)
        return false;
    const auto count = static_cast<std::uint16_t> (channels.size());
    const auto dataBytes = static_cast<std::uint32_t> (frames * count * 4u);
    out.write ("RIFF", 4);
    putU32 (out, 36u + dataBytes);
    out.write ("WAVEfmt ", 8);
    putU32 (out, 16);
    putU16 (out, 3);   // IEEE float
    putU16 (out, count);
    putU32 (out, static_cast<std::uint32_t> (sampleRate));
    putU32 (out, static_cast<std::uint32_t> (sampleRate) * count * 4u);
    putU16 (out, static_cast<std::uint16_t> (count * 4u));
    putU16 (out, 32);
    out.write ("data", 4);
    putU32 (out, dataBytes);
    for (std::size_t i = 0; i < frames; ++i)
        for (const auto& c : channels)
            out.write (reinterpret_cast<const char*> (&c[i]), 4);
    return static_cast<bool> (out);
}

std::map<std::string, ManifestRow> readManifest (const std::string& path)
{
    std::map<std::string, ManifestRow> rows;
    std::ifstream in (path);
    std::string line;
    std::getline (in, line);   // header
    while (std::getline (in, line))
    {
        if (line.empty())
            continue;
        std::stringstream fields (line);
        std::string key, value;
        std::getline (fields, key, ',');
        double v[8] {};
        for (auto& x : v)
        {
            std::getline (fields, value, ',');
            x = std::stod (value);
        }
        rows[key] = { { v[0], v[1], v[2], v[3], true }, { v[4], v[5], v[6], v[7], true } };
    }
    return rows;
}

bool writeManifest (const std::string& path, const std::map<std::string, ManifestRow>& rows)
{
    std::ofstream out (path);
    out << "key,peakL,rmsL,dcL,centroidL,peakR,rmsR,dcR,centroidR\n";
    char buffer[512];
    for (const auto& [key, row] : rows)
    {
        std::snprintf (buffer, sizeof (buffer), "%s,%.9g,%.9g,%.9g,%.6g,%.9g,%.9g,%.9g,%.6g\n",
                       key.c_str(), row.left.peak, row.left.rms, row.left.dc, row.left.centroidHz,
                       row.right.peak, row.right.rms, row.right.dc, row.right.centroidHz);
        out << buffer;
    }
    return static_cast<bool> (out);
}

std::optional<std::string> compare (const Metrics& expected, const Metrics& actual,
                                    const Tolerance& t)
{
    if (! actual.finite)
        return std::string ("render is not finite");
    const auto describe = [] (const char* what, double e, double a) {
        char buffer[160];
        std::snprintf (buffer, sizeof (buffer), "%s expected %.9g, got %.9g", what, e, a);
        return std::string (buffer);
    };
    if (! near (expected.peak, actual.peak, t.relative, t.absoluteFloor))
        return describe ("peak", expected.peak, actual.peak);
    if (! near (expected.rms, actual.rms, t.relative, t.absoluteFloor))
        return describe ("rms", expected.rms, actual.rms);
    if (! near (expected.dc, actual.dc, t.relative, 1e-5))
        return describe ("dc", expected.dc, actual.dc);
    if (! near (expected.centroidHz, actual.centroidHz, t.centroidRelative, 1.0))
        return describe ("centroid Hz", expected.centroidHz, actual.centroidHz);
    return std::nullopt;
}

} // namespace vox::testkit
