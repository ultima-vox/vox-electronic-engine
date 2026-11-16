// VOX Drums gain staging (TL P1 review on #148): every factory kit, with the
// worst-case factory content written for it, must peak at or below -1 dBFS on
// every active output bus at nominal settings (the kit's stored Kit Volume and
// pad levels), at 44.1, 48 and 96 kHz, with every sample finite.
//
// Content per kit: the Factory Song of every style written for the kit (32
// bars), a 64-bar stress song per style (density, complexity, fills, groove,
// syncopation and variation at 1, Seq Accent at 1), and the original kits'
// own patterns and song. Routing: the factory routing (every pad on Main,
// sends to the Main delay/reverb) and a multi-out routing (pad n on Out
// 2 + n % 8, sends unchanged). Offline, fixed 512-sample blocks.
//
// $VOX_GAIN_REPORT=<file> writes the measured peaks as CSV.

#include "FactoryContent.h"
#include "Presets.h"
#include "Processor.h"
#include "StyleRegistry.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace vox::drums;

namespace {

int failures = 0;
void check (bool ok, const std::string& what)
{
    if (! ok)
    {
        ++failures;
        std::printf ("FAIL [VOX Drums gain staging]: %s\n", what.c_str());
    }
}

class PlayHead final : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

std::unique_ptr<DrumsProcessor> makeProcessor (double rate, bool multiOut)
{
    auto p = std::make_unique<DrumsProcessor>();
    auto layout = p->getBusesLayout();
    for (int bus = 1; bus < layout.outputBuses.size(); ++bus)
        layout.outputBuses.getReference (bus) = multiOut ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    if (! p->setBusesLayout (layout))
        check (false, "the bus layout is accepted");
    p->setRateAndBufferSizeDetails (rate, 512);
    p->prepareToPlay (rate, 512);
    return p;
}

void setPlain (DrumsProcessor& p, const juce::String& id, float plain)
{
    auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.state().getParameter (id));
    if (param != nullptr)
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
}

struct Peaks {
    std::vector<float> bus;   // per output bus (0 Main, 1..8 Out 2..9), max |sample| of both channels
    bool finite = true;
};

// Plays the processor's song for `bars` 4/4 bars at `bpm`, then 2 s of tails.
// `main`: when given, receives the Main output (left, right interleaved).
Peaks render (DrumsProcessor& p, double rate, double bpm, int bars, std::vector<float>* main = nullptr)
{
    constexpr int block = 512;
    PlayHead head;
    p.setPlayHead (&head);
    p.reset();
    const double perFrame = bpm / (60.0 * rate), toPpq = bars * 4.0;
    const int frames = static_cast<int> (std::ceil (toPpq / perFrame)) + static_cast<int> (2.0 * rate);
    juce::AudioBuffer<float> buffer (p.getTotalNumOutputChannels(), block);
    juce::MidiBuffer midi;
    Peaks out;
    out.bus.assign (static_cast<std::size_t> (p.getBusCount (false)), 0.0f);
    for (int done = 0; done < frames; done += block)
    {
        const int n = std::min (block, frames - done);
        const double ppq = done * perFrame;
        head.info = {};
        head.info.setIsPlaying (ppq < toPpq);
        head.info.setBpm (bpm);
        head.info.setPpqPosition (ppq);
        head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        buffer.setSize (buffer.getNumChannels(), n, false, false, true);
        buffer.clear();
        midi.clear();
        p.processBlock (buffer, midi);
        if (main != nullptr)
            for (int i = 0; i < n; ++i)
            {
                main->push_back (buffer.getSample (0, i));
                main->push_back (buffer.getSample (1, i));
            }
        int channel = 0;
        for (int bus = 0; bus < p.getBusCount (false); ++bus)
        {
            const int channels = p.getChannelCountOfBus (false, bus);
            for (int c = 0; c < channels; ++c, ++channel)
            {
                const float* x = buffer.getReadPointer (channel);
                for (int i = 0; i < n; ++i)
                {
                    if (! std::isfinite (x[i]))
                        out.finite = false;
                    else
                        out.bus[static_cast<std::size_t> (bus)] = std::max (out.bus[static_cast<std::size_t> (bus)], std::fabs (x[i]));
                }
            }
        }
    }
    p.setPlayHead (nullptr);
    return out;
}

double dbfs (float x) { return x > 0.0f ? 20.0 * std::log10 (static_cast<double> (x)) : -200.0; }

} // namespace

int main (int argc, char** argv)
{
    const double onlyRate = argc > 1 ? std::atof (argv[1]) : 0.0;   // 44100, 48000 or 96000; none: all
    juce::ScopedJuceInitialiser_GUI juce;
    constexpr double target = -1.0;   // dBFS
    std::ostringstream csv;
    csv << "kit,content,rate,routing,bus,peak_dbfs\n";
    struct Worst {
        double db = -200.0;
        std::string where;
    };
    std::map<int, Worst> worst;   // per kit
    bool allFinite = true;
    const auto measure = [&] (int kit, const std::string& content, double rate, bool multiOut, double bpm, int bars,
                              const std::function<bool (DrumsProcessor&)>& load) {
        auto p = makeProcessor (rate, multiOut);
        setPlain (*p, "drm.seq.mode", 1.0f);   // Internal: the plugin plays its song
        p->editSong ([] (SongData& s) { s.songMode = true; });
        if (! load (*p))
        {
            check (false, "load " + content);
            return;
        }
        if (multiOut)
            for (int pad = 0; pad < padCount; ++pad)
            {
                auto s = p->kitStore().pad (pad);
                s.output = static_cast<std::uint8_t> (1 + pad % 8);
                p->kitStore().setPad (pad, s);
            }
        const auto peaks = render (*p, rate, bpm, bars);
        allFinite = allFinite && peaks.finite;
        if (! peaks.finite)
            check (false, "non-finite samples: kit " + std::to_string (kit) + " " + content);
        for (std::size_t bus = 0; bus < peaks.bus.size(); ++bus)
        {
            const double db = dbfs (peaks.bus[bus]);
            char line[256];
            std::snprintf (line, sizeof line, "%s,%s,%.0f,%s,%d,%.2f\n", factoryKitInfo (kit).name, content.c_str(), rate, multiOut ? "multi" : "main",
                           static_cast<int> (bus), db);
            csv << line;
            if (db > worst[kit].db)
                worst[kit] = { db, content + " @" + std::to_string (static_cast<int> (rate)) + (multiOut ? " multi bus " : " main bus ") + std::to_string (bus) };
        }
    };
    const auto rates = [&] {
        std::vector<double> r;
        for (const double rate : { 44100.0, 48000.0, 96000.0 })
            if (onlyRate <= 0.0 || std::abs (rate - onlyRate) < 1.0)
                r.push_back (rate);
        return r;
    }();
    for (const double rate : rates)
        for (const bool multiOut : { false, true })
            for (int s = 0; s < factoryStyleCount; ++s)
            {
                const auto* info = style::infoById (s);
                const int kit = factoryKitOfStyle (s);
                const double bpm = std::round ((info->tempoMin + info->tempoMax) * 0.5);
                measure (kit, std::string (info->key) + " factory song", rate, multiOut, bpm, 32,
                         [s] (DrumsProcessor& p) { return p.presets().loadFactoryStyleSong (s).wasOk(); });
                measure (kit, std::string (info->key) + " stress song", rate, multiOut, bpm, 64, [s, kit] (DrumsProcessor& p) {
                    if (p.presets().loadFactoryKit (kit).failed())
                        return false;
                    GeneratorSettings g = factoryStyleGenerator (s);
                    g.density = g.complexity = g.fills = g.groove = g.syncopation = g.variation = 1.0f;
                    g.seed = 77u + static_cast<std::uint32_t> (s);
                    p.setGenerator (g);
                    for (int slot = 0; slot < PatternStore::slotCount; ++slot)
                        p.patterns().setSlot (slot, StoredPattern {});
                    p.editSong ([] (SongData& song) {
                        song.length = 64;
                        song.loopStart = 0;
                        song.loopLength = 64;
                    });
                    setPlain (p, "drm.seq.accent", 1.0f);
                    return p.generate (DrumsProcessor::Generate::song);
                });
            }
    for (const double rate : rates)
        for (const bool multiOut : { false, true })
            for (int kit = 0; kit < originalFactoryKitCount; ++kit)
                measure (kit, "own patterns and song", rate, multiOut, factoryKitInfo (kit).tempo, 16, [kit] (DrumsProcessor& p) {
                    return p.presets().loadFactoryKit (kit).wasOk() && p.presets().loadFactoryPatterns (kit).wasOk();
                });
    for (const auto& [kit, w] : worst)
    {
        std::printf ("gain staging: %-22s worst %+6.2f dBFS (%s)\n", factoryKitInfo (kit).name, w.db, w.where.c_str());
        check (w.db <= target, std::string ("kit ") + factoryKitInfo (kit).name + " peaks at " + std::to_string (w.db) + " dBFS (" + w.where
                                   + "), above -1 dBFS");
    }
    check (allFinite, "every sample is finite");
    // Matched gain (48 kHz run): the calibration changes only the kit's output
    // gain, so the calibrated kit's output is the legacy output (Kit Volume
    // 0.6) times the volume ratio, sample for sample: no DSP or timbre change.
    if (onlyRate <= 0.0 || std::abs (onlyRate - 48000.0) < 1.0)
    {
        constexpr float legacyVolume = 0.6f;
        for (int kit = 0; kit < factoryKitCount; ++kit)
        {
            int style = 0;
            while (factoryKitOfStyle (style) != kit && style < factoryStyleCount - 1)
                ++style;
            std::vector<float> calibrated, legacy;
            for (const bool old : { false, true })
            {
                auto p = makeProcessor (48000.0, false);
                setPlain (*p, "drm.seq.mode", 1.0f);
                p->editSong ([] (SongData& s) { s.songMode = true; });
                p->presets().loadFactoryStyleSong (style);
                if (old)
                    setPlain (*p, "drm.kit.volume", legacyVolume);
                render (*p, 48000.0, 120.0, 8, old ? &legacy : &calibrated);
            }
            const double ratio = static_cast<double> (factoryKit (kit).volume) / legacyVolume;
            double peak = 0.0, error = 0.0;
            for (std::size_t i = 0; i < std::min (calibrated.size(), legacy.size()); ++i)
            {
                peak = std::max (peak, std::fabs (static_cast<double> (calibrated[i])));
                error = std::max (error, std::fabs (static_cast<double> (calibrated[i]) - ratio * legacy[i]));
            }
            std::printf ("matched gain: %-22s volume 0.600 -> %.3f (%+.2f dB), max deviation %.2e of peak\n", factoryKitInfo (kit).name,
                         static_cast<double> (factoryKit (kit).volume), 20.0 * std::log10 (ratio), peak > 0.0 ? error / peak : 0.0);
            check (calibrated.size() == legacy.size() && peak > 0.0 && error <= 1.0e-4 * peak,
                   std::string ("matched gain: ") + factoryKitInfo (kit).name + " is the legacy output times the volume ratio (DSP unchanged)");
        }
    }
    if (const char* path = std::getenv ("VOX_GAIN_REPORT"))
        juce::File (juce::String (path)).replaceWithText (csv.str());
    if (failures == 0)
        std::printf ("VOX Drums gain staging tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
