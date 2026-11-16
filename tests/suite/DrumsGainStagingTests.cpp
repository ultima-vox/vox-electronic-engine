// VOX Drums gain staging (TL P1 review on #148; coverage per the TL review
// on #150).
//
// The guarantee, exactly: the FACTORY kits, at their stored Kit Volume and
// pad levels and the factory (all on Main) or a multi-out routing, playing
// the factory content and the stress matrix below, peak at or below -1 dBFS
// SAMPLE peak on every active output bus at 44.1, 48 and 96 kHz, with every
// sample finite. It is not a true-peak (inter-sample) figure, which is not
// measured, and it says nothing about user-edited kits or settings (any Kit
// Volume or pad level can be raised past full scale).
//
// Matrix per style (its factory kit), reproducible (fixed seeds):
//  - the 32-bar Factory Song;
//  - stress A (density, complexity, fills, groove, syncopation, variation and
//    Seq Accent at 1): a 64-bar song (seed 77 + id) and two 32-bar songs
//    (seeds 1009 + 7 id, 4099 + 13 id);
//  - stress B (density, complexity, fills 1; groove, syncopation, variation
//    0.5; humanize 0; Seq Accent 1): two 32-bar songs (seeds 9973 + 3 id,
//    30011 + 11 id);
//  - the original kits' own patterns and song.
// Multi-out routing (pad n on Out 2 + n % 8; each bus carries at most two
// pads) runs the Factory Song and the first 32-bar stress A song.
//
// Modes (argv): "<rate> main|multi" runs the matrix for one rate and
// routing; "blocks" shows that the block size does not change the output
// (64, 333, 512: sample-identical Main) and that a looping song (past its
// end, across the loop point) stays under the limit; "matched" checks that
// the calibration changes only the kit's output gain. No argument: all.
//
// $VOX_GAIN_REPORT=<prefix> writes <prefix>-<mode>.csv (kit, style, content,
// seed, rate, block, routing, bus, peak); a failed write fails the test.

#include "FactoryContent.h"
#include "Presets.h"
#include "Processor.h"
#include "StyleRegistry.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
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

constexpr double limitDbfs = -1.0;

class PlayHead final : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

std::unique_ptr<DrumsProcessor> makeProcessor (double rate, bool multiOut, int block)
{
    auto p = std::make_unique<DrumsProcessor>();
    auto layout = p->getBusesLayout();
    for (int bus = 1; bus < layout.outputBuses.size(); ++bus)
        layout.outputBuses.getReference (bus) = multiOut ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    if (! p->setBusesLayout (layout))
        check (false, "the bus layout is accepted");
    p->setRateAndBufferSizeDetails (rate, block);
    p->prepareToPlay (rate, block);
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

// Plays the processor's song for `bars` 4/4 bars at `bpm` in blocks of
// `block` frames, then 2 s of tails. `main`: receives the Main output.
Peaks render (DrumsProcessor& p, double rate, double bpm, int bars, int block, std::vector<float>* main = nullptr)
{
    PlayHead head;
    p.setPlayHead (&head);
    p.reset();
    const double perFrame = bpm / (60.0 * rate), toPpq = bars * 4.0;
    // The transport stops exactly at the song's end frame (a short block
    // there, as a host does), whatever the block size.
    const int stopFrame = static_cast<int> (std::ceil (toPpq / perFrame));
    const int frames = stopFrame + static_cast<int> (2.0 * rate);
    juce::AudioBuffer<float> buffer (p.getTotalNumOutputChannels(), block);
    juce::MidiBuffer midi;
    Peaks out;
    out.bus.assign (static_cast<std::size_t> (p.getBusCount (false)), 0.0f);
    for (int done = 0, n = 0; done < frames; done += n)
    {
        n = std::min (block, frames - done);
        if (done < stopFrame)
            n = std::min (n, stopFrame - done);
        const double ppq = done * perFrame;
        head.info = {};
        head.info.setIsPlaying (done < stopFrame);
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

double tempoOf (int style)
{
    const auto* info = style::infoById (style);
    return std::round ((info->tempoMin + info->tempoMax) * 0.5);
}

// The processor ready to play: Internal sequencer, Song mode.
std::unique_ptr<DrumsProcessor> player (double rate, bool multiOut, int block)
{
    auto p = makeProcessor (rate, multiOut, block);
    setPlain (*p, "drm.seq.mode", 1.0f);
    p->editSong ([] (SongData& s) { s.songMode = true; });
    return p;
}

void routeMultiOut (DrumsProcessor& p)
{
    for (int pad = 0; pad < padCount; ++pad)
    {
        auto s = p.kitStore().pad (pad);
        s.output = static_cast<std::uint8_t> (1 + pad % 8);
        p.kitStore().setPad (pad, s);
    }
}

// A stress song of style `s` on its factory kit.
bool loadStress (DrumsProcessor& p, int s, char variant, std::uint32_t seed, int bars)
{
    if (p.presets().loadFactoryKit (factoryKitOfStyle (s)).failed())
        return false;
    GeneratorSettings g = factoryStyleGenerator (s);
    if (variant == 'A')
        g.density = g.complexity = g.fills = g.groove = g.syncopation = g.variation = 1.0f;
    else
    {
        g.density = g.complexity = g.fills = 1.0f;
        g.groove = g.syncopation = g.variation = 0.5f;
        g.humanize = 0.0f;
    }
    g.seed = seed;
    p.setGenerator (g);
    for (int slot = 0; slot < PatternStore::slotCount; ++slot)
        p.patterns().setSlot (slot, StoredPattern {});
    p.editSong ([bars] (SongData& song) {
        song.length = static_cast<std::uint8_t> (bars);
        song.loopStart = 0;
        song.loopLength = static_cast<std::uint8_t> (bars);
    });
    setPlain (p, "drm.seq.accent", 1.0f);
    return p.generate (DrumsProcessor::Generate::song);
}

struct Report {
    std::ostringstream csv;
    struct Worst {
        double db = -200.0;
        std::string where;
    };
    std::map<int, Worst> worst;   // per kit
    Report() { csv << "kit,style,content,seed,rate,block,routing,bus,peak_dbfs\n"; }
    void add (int kit, int style, const std::string& content, std::uint32_t seed, double rate, int block, bool multiOut, const Peaks& peaks)
    {
        check (peaks.finite, "every sample is finite: " + std::string (factoryKitInfo (kit).name) + " " + content);
        for (std::size_t bus = 0; bus < peaks.bus.size(); ++bus)
        {
            const double db = dbfs (peaks.bus[bus]);
            char line[320];
            std::snprintf (line, sizeof line, "%s,%s,%s,%u,%.0f,%d,%s,%d,%.3f\n", factoryKitInfo (kit).name, style >= 0 ? style::infoById (style)->key : "-",
                           content.c_str(), seed, rate, block, multiOut ? "multi" : "main", static_cast<int> (bus), db);
            csv << line;
            if (db > worst[kit].db)
                worst[kit] = { db, std::string (style >= 0 ? style::infoById (style)->key : "own") + " " + content + " seed " + std::to_string (seed) + " @"
                                       + std::to_string (static_cast<int> (rate)) + " block " + std::to_string (block)
                                       + (multiOut ? " multi bus " : " main bus ") + std::to_string (bus) };
        }
    }
    void finish (const std::string& mode)
    {
        for (const auto& [kit, w] : worst)
        {
            std::printf ("gain staging [%s]: %-22s worst %+6.2f dBFS sample peak (%s)\n", mode.c_str(), factoryKitInfo (kit).name, w.db, w.where.c_str());
            check (w.db <= limitDbfs, std::string ("kit ") + factoryKitInfo (kit).name + " peaks at " + std::to_string (w.db) + " dBFS (" + w.where
                                          + "), above -1 dBFS");
        }
        if (const char* prefix = std::getenv ("VOX_GAIN_REPORT"))
        {
            const juce::File file (juce::String (prefix) + "-" + mode + ".csv");
            check (file.getParentDirectory().createDirectory().wasOk() && file.replaceWithText (csv.str()),
                   "the report is written: " + file.getFullPathName().toStdString());
        }
    }
};

void runMatrix (double rate, bool multiOut)
{
    constexpr int block = 512;
    Report report;
    for (int s = 0; s < factoryStyleCount; ++s)
    {
        const int kit = factoryKitOfStyle (s);
        const double bpm = tempoOf (s);
        const auto one = [&] (const std::string& content, std::uint32_t seed, int bars, const std::function<bool (DrumsProcessor&)>& load) {
            auto p = player (rate, multiOut, block);
            if (! load (*p))
            {
                check (false, "load " + std::string (style::infoById (s)->key) + " " + content);
                return;
            }
            if (multiOut)
                routeMultiOut (*p);
            report.add (kit, s, content, seed, rate, block, multiOut, render (*p, rate, bpm, bars, block));
        };
        one ("factory song", factoryStyleGenerator (s).seed, 32, [s] (DrumsProcessor& p) { return p.presets().loadFactoryStyleSong (s).wasOk(); });
        const std::uint32_t id = static_cast<std::uint32_t> (s);
        struct Case {
            char variant;
            std::uint32_t seed;
            int bars;
        };
        std::vector<Case> cases { { 'A', 1009u + 7u * id, 32 } };
        if (! multiOut)
        {
            cases.push_back ({ 'A', 77u + id, 64 });
            cases.push_back ({ 'A', 4099u + 13u * id, 32 });
            cases.push_back ({ 'B', 9973u + 3u * id, 32 });
            cases.push_back ({ 'B', 30011u + 11u * id, 32 });
        }
        for (const auto& c : cases)
            one (std::string ("stress ") + c.variant + " " + std::to_string (c.bars) + " bars", c.seed, c.bars,
                 [&] (DrumsProcessor& p) { return loadStress (p, s, c.variant, c.seed, c.bars); });
    }
    for (int kit = 0; kit < originalFactoryKitCount; ++kit)
    {
        auto p = player (rate, multiOut, block);
        if (p->presets().loadFactoryKit (kit).failed() || p->presets().loadFactoryPatterns (kit).failed())
            check (false, "load the own song of " + std::string (factoryKitInfo (kit).name));
        if (multiOut)
            routeMultiOut (*p);
        report.add (kit, -1, "own patterns and song", 0, rate, block, multiOut, render (*p, rate, factoryKitInfo (kit).tempo, 16, block));
    }
    report.finish (std::to_string (static_cast<int> (rate)) + (multiOut ? "-multi" : "-main"));
}

// Block size and loop boundaries (48 kHz): the same song rendered in blocks
// of 64, 333 and 512 frames gives the same Main output sample for sample (the
// sequencer is sample-accurate, the DSP per sample; the transport stops at the
// same frame), so the block size cannot change a peak. A looping song played past its end (across the loop point)
// stays under the limit.
void runBlocks()
{
    constexpr double rate = 48000.0;
    Report report;
    for (int kit = 0; kit < factoryKitCount; ++kit)
    {
        int s = 0;
        while (factoryKitOfStyle (s) != kit && s < factoryStyleCount - 1)
            ++s;
        std::vector<std::vector<float>> outputs;
        for (const int block : { 64, 333, 512 })
        {
            auto p = player (rate, false, block);
            loadStress (*p, s, 'A', 1009u + 7u * static_cast<std::uint32_t> (s), 32);
            outputs.emplace_back();
            report.add (kit, s, "stress A 32 bars", 1009u + 7u * static_cast<std::uint32_t> (s), rate, block, false,
                        render (*p, rate, tempoOf (s), 32, block, &outputs.back()));
        }
        check (outputs[0] == outputs[1] && outputs[1] == outputs[2],
               std::string ("blocks: ") + factoryKitInfo (kit).name + " renders sample-identically in blocks of 64, 333 and 512");
        // Loop: the 32-bar Factory Song looped, played for 40 bars (across the loop point twice for short loops).
        for (const int block : { 333, 512 })
        {
            auto p = player (rate, false, block);
            p->presets().loadFactoryStyleSong (s);
            p->editSong ([] (SongData& song) {
                song.loopOn = true;
                song.loopStart = 24;
                song.loopLength = 8;
            });
            report.add (kit, s, "factory song; loop 25-32; 40 bars", factoryStyleGenerator (s).seed, rate, block, false,
                        render (*p, rate, tempoOf (s), 40, block));
        }
    }
    report.finish ("blocks");
}

// Matched gain (48 kHz): each calibrated kit's output is the legacy output
// (Kit Volume 0.6 before calibration) times the volume ratio, sample for
// sample: only the kit's output gain changed, not the DSP or the timbre.
void runMatched()
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
            auto p = player (48000.0, false, 512);
            p->presets().loadFactoryStyleSong (style);
            if (old)
                setPlain (*p, "drm.kit.volume", legacyVolume);
            render (*p, 48000.0, 120.0, 8, 512, old ? &legacy : &calibrated);
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

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    const std::string mode = argc > 1 ? argv[1] : "all";
    if (mode == "blocks" || mode == "all")
        runBlocks();
    if (mode == "matched" || mode == "all")
        runMatched();
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        if (mode == "all" || std::abs (std::atof (mode.c_str()) - rate) < 1.0)
            for (const bool multiOut : { false, true })
                if (argc < 3 || (std::strcmp (argv[2], "multi") == 0) == multiOut)
                    runMatrix (rate, multiOut);
    if (failures == 0)
        std::printf ("VOX Drums gain staging tests passed (%s)\n", mode.c_str());
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
