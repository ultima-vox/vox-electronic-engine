// VOX Drums callback timing (P1 performance evidence). Measures the
// duration of processBlock for a heavy scene (Song mode with the generated
// 64-bar song, Host MIDI + Internal, 16 pads on the nine buses, both sends
// on every pad) at 44.1/48/96 kHz and blocks of 64/128/333/512, and prints
// p50/p95/p99/p99.9/worst against the block's deadline.
//
// It reports; it does not gate: the numbers depend on the machine and on
// what else it is running. The exit code is 0 unless the audio is invalid.
// Optional argument: a file to write the table to (CSV).
#include "Processor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

using namespace vox::drums;

namespace {

class PlayHead final : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::ostringstream csv;
    csv << "sample_rate,block,blocks,deadline_us,p50_us,p95_us,p99_us,p999_us,worst_us,worst_percent_of_deadline\n";
    std::printf ("%-8s %-6s %-12s %-9s %-9s %-9s %-9s %-9s %s\n", "rate", "block", "deadline_us", "p50", "p95", "p99", "p99.9", "worst", "worst/deadline");
    bool valid = true;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 64, 128, 333, 512 })
        {
            DrumsProcessor p;
            auto layout = p.getBusesLayout();
            for (int bus = 1; bus < layout.outputBuses.size(); ++bus)
                layout.outputBuses.getReference (bus) = juce::AudioChannelSet::stereo();
            p.setBusesLayout (layout);
            p.setRateAndBufferSizeDetails (rate, block);
            p.prepareToPlay (rate, block);
            const auto set = [&p] (const juce::String& id, float plain) {
                auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.state().getParameter (id));
                param->setValueNotifyingHost (param->convertTo0to1 (plain));
            };
            auto g = p.generator();
            g.density = 1.0f;
            g.complexity = 1.0f;
            p.setGenerator (g);
            p.editSong ([] (SongData& s) { s.length = 64; s.songMode = true; });
            p.generate (DrumsProcessor::Generate::song, 0xffffu, false);
            set ("drm.seq.mode", 2.0f);
            for (int pad = 0; pad < padCount; ++pad)
            {
                auto settings = p.kitStore().pad (pad);
                settings.output = static_cast<std::uint8_t> (pad % 9);
                settings.polyphony = 4;
                p.kitStore().setPad (pad, settings);
                set (juce::String (padPrefix (pad)) + ".send1", 0.5f);
                set (juce::String (padPrefix (pad)) + ".send2", 0.5f);
                set (juce::String (padPrefix (pad)) + ".decay", 1.5f);
            }
            PlayHead head;
            p.setPlayHead (&head);
            juce::AudioBuffer<float> buffer (p.getTotalNumOutputChannels(), block);
            juce::MidiBuffer midi;
            midi.ensureSize (2048);
            const int blocks = static_cast<int> (20.0 * rate / block);   // 20 s of audio
            std::vector<double> micros;
            micros.reserve (static_cast<std::size_t> (blocks));
            double ppq = 0.0;
            for (int b = 0; b < blocks; ++b)
            {
                head.info = {};
                head.info.setIsPlaying (true);
                head.info.setBpm (145.0);
                head.info.setPpqPosition (ppq);
                midi.clear();
                if (b % 3 == 0)   // host notes on top of the sequencer
                    midi.addEvent (juce::MidiMessage::noteOn (10, 36 + (b / 3) % 22, static_cast<juce::uint8> (100)), 0);
                const auto start = std::chrono::steady_clock::now();
                p.processBlock (buffer, midi);
                micros.push_back (std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - start).count());
                if (b % 64 == 0)
                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        valid = valid && std::isfinite (buffer.getMagnitude (ch, 0, block));
                ppq += block * 145.0 / (60.0 * rate);
            }
            p.setPlayHead (nullptr);
            std::sort (micros.begin(), micros.end());
            const auto at = [&] (double q) { return micros[std::min (micros.size() - 1, static_cast<std::size_t> (q * static_cast<double> (micros.size())))]; };
            const double deadline = 1.0e6 * block / rate;
            std::printf ("%-8.0f %-6d %-12.1f %-9.1f %-9.1f %-9.1f %-9.1f %-9.1f %.1f %%\n", rate, block, deadline, at (0.5), at (0.95), at (0.99),
                         at (0.999), micros.back(), 100.0 * micros.back() / deadline);
            csv << rate << ',' << block << ',' << blocks << ',' << deadline << ',' << at (0.5) << ',' << at (0.95) << ',' << at (0.99) << ','
                << at (0.999) << ',' << micros.back() << ',' << 100.0 * micros.back() / deadline << "\n";
        }
    if (argc > 1)
        std::ofstream (argv[1], std::ios::binary) << csv.str();
    if (! valid)
        std::printf ("FAIL: non-finite audio\n");
    return valid ? 0 : 1;
}
