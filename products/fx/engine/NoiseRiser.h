#pragma once
#include "SampleStore.h"

#include "Timeline.h"
#include "SourceSound.h"
#include "MacroRouting.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace vox::fx {

// Interim layered Noise Riser. Programs are POD; install only while stopped or
// on the audio thread from a separately prepared handoff. Each event snapshots
// the layer settings at note-on; no pointer into UI or host state is retained.
enum class NoiseFamily : std::uint8_t { riser, downlifter, shepard, impact, whoosh, laser, squelch, metallic, texture, formant, glitch, granular };
enum class PlayMode : std::uint8_t { oneShot, gate, continuous, loop, sequence, transition };
enum class TriggerSource : std::uint8_t { midi, pattern, preview };
enum class TriggerPolicy : std::uint8_t { overlap, ignore, replace };
struct NoiseLayer {
    NoiseFamily family = NoiseFamily::riser;
    bool enabled = false;
    timeline::LayerTime timing {};
    float gain = 1.0f, pan = 0.0f; // pan -1..1; center preserves original gain
    timeline::Curve gainCurve = timeline::Curve::constant(1.0f);
    timeline::Curve panCurve = timeline::Curve::constant(0.5f); // 0..1 -> -1..1 offset
    // Normalized trajectories; pitch maps to +/- 24 semitones and cutoff
    // contributes logarithmic filter motion around the family sweep. Defaults
    // preserve legacy sound.
    timeline::Curve pitchCurve = timeline::Curve::constant(0.5f);
    timeline::Curve cutoffCurve = timeline::Curve::constant(0.5f);
    std::array<source::Filter,2> filters{};
    source::GrainShape grains{};
    float character = 0; // signed, family-specific; zero is the legacy sound
    float transpose = 0; // semitones; authored bias for pitched sources
    bool valid() const noexcept {
        if(!grains.valid()||!std::isfinite(character)||character< -1||character>1)return false;
        if(!std::isfinite(transpose)||transpose< -24||transpose>24||!filters[0].valid()||!filters[1].valid())return false;
        return static_cast<unsigned>(family) <= static_cast<unsigned>(NoiseFamily::granular) &&
               timing.valid() && std::isfinite(gain) && gain >= 0 && gain <= 1 &&
               std::isfinite(pan) && pan >= -1 && pan <= 1 && gainCurve.valid() && panCurve.valid() &&
               pitchCurve.valid() && cutoffCurve.valid();
    }
};
struct NoiseProgram {
    std::array<NoiseLayer, 4> layers {};
    // Program-wide amplitude, pitch, cutoff and panorama trajectories.
    timeline::Curve amplitudeCurve = timeline::Curve::constant(1.0f);
    timeline::Curve pitchCurve = timeline::Curve::constant(0.5f);
    timeline::Curve cutoffCurve = timeline::Curve::constant(0.5f);
    timeline::Curve panoramaCurve = timeline::Curve::constant(0.5f);
    PlayMode mode = PlayMode::oneShot;
    TriggerPolicy triggerPolicy = TriggerPolicy::overlap;
    std::uint8_t bars = 0; // 0 = seconds; otherwise 1, 2, 4, 8 or 16 bars
    source::Tonality tonality{};
    NoiseProgram() noexcept { layers[0].enabled = true; }
    bool valid() const noexcept {
        if (!tonality.valid()) return false;
        if (bars != 0 && bars != 1 && bars != 2 && bars != 4 && bars != 8 && bars != 16) return false;
        if (static_cast<unsigned>(mode) > static_cast<unsigned>(PlayMode::transition) ||
            static_cast<unsigned>(triggerPolicy) > static_cast<unsigned>(TriggerPolicy::replace)) return false;
        for (const auto& layer : layers) if (!layer.valid()) return false;
        return amplitudeCurve.valid() && pitchCurve.valid() && cutoffCurve.valid() && panoramaCurve.valid();
    }
};

// Allocation-free event engine. Duration is captured at note-on; PlayMode
// owns the held lifecycle and all-sound-off uses the shared 5 ms fade.
class NoiseRiser {
public:
    ~NoiseRiser() { releaseSampleLeases(); }
    // Control thread only, while stopped. Attached store must outlive this
    // engine and all rendering, including the destructor.
    bool attachSampleStore(sample::Store* store) noexcept {
        if(activeEventCount()!=0)return false;
        releaseSampleLeases();sampleStore_=store;return true;
    }
    // Audio thread or stopped controller only. Tempo changes apply to future
    // samples of glitch and tempo-synced voice LFOs, independent of whether
    // the host supplies a usable bar position. Existing phase is preserved.
    bool setTempo(double bpm) noexcept {
        if(!std::isfinite(bpm)||bpm<1.0||bpm>1000.0)return false;
        motionTempo_=bpm;glitchGridIncrement_=bpm/(15.0*rate_); return true;
    }
    // When host musical position is unavailable, reject new bar events and
    // fade only existing bar voices; keep the last tempo for their release.
    void setMusicalClockAvailable(bool available, double bpm = 120.0) noexcept {
        available = available && std::isfinite(bpm) && bpm >= 1.0 && bpm <= 1000.0;
        musicalClockAvailable_ = available;
        if (!available) barGrid_ = {};
        if (available) tempo_ = motionTempo_ = bpm;
        if (!available) {
            for (auto& v : voices_) if (v.active && v.program.bars != 0) beginRelease(v);
            for (auto& v : retiring_) if (v.active && v.program.bars != 0) beginRelease(v);
        }
    }
    void setBarGrid(timeline::BarGrid grid) noexcept {
        barGrid_ = grid;
        blockOffset_ = 0;
        if (!grid.valid()) releaseAlignedTransitions();
    }
    void releaseAlignedTransitions() noexcept {
        for (auto& v : voices_) if (v.active && v.targetSpan > 0) beginRelease(v);
    }
    static double samplePlaybackStep(double sourceRate,double outputRate,double pitchRatio) noexcept {
        return sourceRate/outputRate*pitchRatio;
    }
    void prepare(double sampleRate) noexcept {
        releaseSampleLeases();
        rate_ = std::isfinite(sampleRate) && sampleRate >= 8000.0 && sampleRate <= 384000.0 ? sampleRate : 44100.0;
        (void)voiceMotion_.prepare(rate_);
        (void)setTempo(120.0); tempo_ = 120.0; musicalClockAvailable_ = false;
        barGrid_ = {}; blockOffset_ = 0; midiKeys_ = {};
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
        // Explicit built-in procedural sample, prepared outside the callback.
        // External sample loading/ownership is a separate unfinished feature.
        std::uint32_t sourceRng = 0x719b83a5u;
        double color = 0;
        for (std::size_t i=0; i<sample_.size(); ++i) {
            sourceRng ^= sourceRng << 13; sourceRng ^= sourceRng >> 17; sourceRng ^= sourceRng << 5;
            const double white = static_cast<double>(sourceRng) / 2147483648.0 - 1.0;
            color += 0.15 * (white - color);
            const double fade = std::sin(3.141592653589793 * i / sample_.size());
            sample_[i] = static_cast<float>(0.65 * color * fade * fade + 0.25 * std::sin(6.283185307179586 * 13.0 * i / sample_.size()));
        }
        for (std::size_t i=0; i<grainWindow_.size(); ++i) {
            const double phase = static_cast<double>(i) / (grainWindow_.size() - 1);
            grainWindow_[i] = 0.5 - 0.5 * std::cos(6.283185307179586 * phase);
        }
        grainLength_ = static_cast<unsigned>(std::max(2.0, rate_ * 0.08));
        grainInterval_ = static_cast<unsigned>(std::max(1.0, rate_ * 0.025));
        soloMask_=0;soloGains_.fill(1.0);soloRamping_=false;
        soloStep_=1.0/std::max(1.0,std::floor(rate_*.005));
        grains_ = {}; activeGrains_ = 0; voices_ = {}; retiring_ = {}; serial_ = 0;droppedEvents_=0;
    }
    // The caller must ensure there is no concurrent access. UI/worker threads
    // must not call this during rendering without a safe publication bridge.
    bool setVoiceMotion(const macro::Config& config) noexcept { return voiceMotion_.setConfig(config,true); }
    void setMacroBases(const std::array<float,8>& values) noexcept { macroBases_=values; }
    bool setProgram(const NoiseProgram& program,std::uint64_t publication=0) noexcept {
        if (!program.valid()) return false;
        program_ = program;programPublication_=publication;
        return true;
    }
    // Up to eight active events plus four 5 ms retirement fades. A steal
    // chooses the oldest active event and a free retirement slot; if all four
    // fades are occupied the incoming event is dropped deterministically.
    bool noteOn(int channel, int note, float velocity, double duration = 4.0, bool userSample = false, std::uint64_t expectedSampleToken = 0,
                TriggerSource source = TriggerSource::midi) noexcept {
        timeline::SecondsClock clock;
        timeline::BarsClock barsClock;
        if (channel < 1 || channel > 16 || note < 0 || note > 127 || !std::isfinite(velocity) || velocity <= 0.0f) return false;
        // Every valid MIDI key-down occupies a FIFO position, even if its
        // audio voice is later stolen, replaced, finite, or never started.
        // Otherwise that key-up would release a newer overlapping voice.
        const auto ticket = source == TriggerSource::midi ? ++midiKey(channel, note).issued : 0;
        if (!clock.start(duration, rate_)) return false;
        if (program_.bars != 0 && (!musicalClockAvailable_ || !barsClock.start(program_.bars,4,4,rate_))) return false;
        const bool aligned = program_.mode == PlayMode::transition && program_.bars != 0;
        if (aligned && !barGrid_.valid()) return false;
        const double start = barGrid_.at(blockOffset_);
        const double target = aligned ? barGrid_.target(start, program_.bars) : start;
        if (program_.triggerPolicy == TriggerPolicy::ignore)
            for (const auto& voice : voices_) if (voice.active && voice.channel == channel && voice.note == note && voice.source == source) return false;
        Voice* slot = nullptr;
        for (auto& voice : voices_) if (!voice.active) { slot = &voice; break; }
        const bool stealing=slot==nullptr;
        std::size_t retiringIndex=retiring_.size();
        if(stealing){
            for(std::size_t i=0;i<retiring_.size();++i)if(!retiring_[i].active){retiringIndex=i;break;}
            if(retiringIndex==retiring_.size()){
                if(droppedEvents_!=std::numeric_limits<std::uint32_t>::max())++droppedEvents_;
                return false;
            }
            slot=&*std::min_element(voices_.begin(),voices_.end(),
                [](const Voice& a,const Voice& b){return a.serial<b.serial;});
        }
        // The external source is relevant only to enabled granular layers.
        // A mixed event is rejected as a unit when its granular source is
        // unavailable; unrelated families remain playable.
        userSample = userSample && std::any_of(program_.layers.begin(),program_.layers.end(),
            [](const auto& layer) { return layer.enabled && layer.family == NoiseFamily::granular; });
        // Explicit user selection never falls back if the content is missing
        // or publication races acquisition. Acquire before changing voices.
        sample::Store::Lease incoming;
        if(userSample && (!sampleStore_ || !sampleStore_->acquire(incoming,expectedSampleToken)))return false;
        // Replace only after all bounded resources and optional sample content
        // are secured, so a rejected NoteOn cannot disturb the old owner.
        if(program_.triggerPolicy==TriggerPolicy::replace)
            for(auto& voice:voices_)if(voice.active&&voice.channel==channel&&voice.note==note&&voice.source==source)beginRelease(voice);
        const auto slotIndex=static_cast<std::size_t>(slot-voices_.data());
        if(stealing){
            auto& retired=retiring_[retiringIndex];retired=*slot;beginRelease(retired);
            const auto leaseIndex=voices_.size()+retiringIndex;
            if(sampleStore_&&sampleLeases_[slotIndex].data())(void)sampleStore_->transfer(sampleLeases_[slotIndex],sampleLeases_[leaseIndex]);
            for(auto& grain:grains_)if(grain.active&&grain.serial==retired.serial)grain.voiceIndex=leaseIndex;
        }
        if(userSample)(void)sampleStore_->transfer(incoming,sampleLeases_[slotIndex]);
        *slot = {};
        slot->active = true; slot->channel = channel; slot->note = note; slot->source = source; slot->noteTicket = ticket;
        slot->velocity = std::clamp(velocity, 0.0f, 1.0f);
        slot->clock = clock; slot->barsClock = barsClock;
        slot->duration = program_.bars ? program_.bars * 4.0 * 60.0 / tempo_ : duration; slot->program = program_;
        slot->target = target; slot->targetSpan = target - start;
        if (aligned) slot->duration = slot->targetSpan * 60.0 / tempo_;
        // Pattern/preview send no NoteOff. They audition one bounded pass,
        // without stealing MIDI ownership for the same channel/note.
        if (source != TriggerSource::midi && isHeldMode(slot->program.mode))
            slot->program.mode = PlayMode::oneShot;
        slot->serial = ++serial_;slot->publication=programPublication_;
        if(voiceMotion_.active()){
            slot->motion=voiceMotion_;
            slot->motion.restart(static_cast<std::uint32_t>(serial_));
        }
        int enabled = 0;
        const int mapped=slot->program.tonality.map(note);
        for (std::size_t i = 0; i < slot->layers.size(); ++i) {
            enabled += slot->program.layers[i].enabled ? 1 : 0;
            auto& layer = slot->layers[i];
            const auto& settings = slot->program.layers[i];
            if(settings.enabled) {
                for (std::size_t f=0;f<2;++f) layer.filters[f].prepare(settings.filters[f],rate_);
                layer.filterGrains=settings.family==NoiseFamily::granular&&(settings.filters[0].type!=source::FilterType::off||settings.filters[1].type!=source::FilterType::off);
                if(settings.transpose!=0)layer.transposeRatio=std::exp2(settings.transpose/12.0);
                layer.tunedHz=noteHz_[static_cast<std::size_t>(mapped)];
                layer.modalNote=std::clamp(mapped+static_cast<int>(std::lround(settings.transpose)),0,127);
            }
            layer.rng = 0x9e3779b9u ^ static_cast<std::uint32_t>(serial_ * 0x85ebca6bu) ^
                        static_cast<std::uint32_t>(i * 0x27d4eb2du);
            if (layer.rng == 0) layer.rng = 1;
        }
        slot->layerScale = 1.0 / std::max(1, enabled);
        return true;
    }
    // Audio-thread diagnostics; never read concurrently without a snapshot.
    unsigned activeEventCount() const noexcept {
        unsigned count=0;
        for(const auto& voice:retiring_)count+=voice.active?1u:0u;
        for(const auto& voice:voices_)count+=voice.active?1u:0u;
        return count;
    }
    std::uint32_t droppedEventCount() const noexcept { return droppedEvents_; }
    unsigned activeGrainCount() const noexcept {
        return activeGrains_;
    }
    // Consume exactly one MIDI key-down on this channel/note. A finite or
    // already retired owner still consumes its own key-up. No event IDs are
    // assumed to exist in MIDI 1.0; identical keys are paired in wire order.
    void noteOff(int channel, int note) noexcept {
        if(channel<1||channel>16||note<0||note>127)return;
        auto& key = midiKey(channel, note);
        if (key.released == key.issued) return;
        const auto ticket = ++key.released;
        for (auto& v : voices_)
            if (v.active && v.channel == channel && v.note == note && v.source == TriggerSource::midi &&
                v.noteTicket == ticket && v.releaseRemaining == 0 && isHeldMode(v.program.mode)) { beginRelease(v,true); break; }
    }
    void stopPreview() noexcept {
        for(auto& v:voices_)if(v.active&&v.source==TriggerSource::preview)beginRelease(v);
        for(auto& v:retiring_)if(v.active&&v.source==TriggerSource::preview)beginRelease(v);
    }
    // Offline processing discards live audition voices before any host notes
    // can reuse their slots. The sample store releases a reader token only.
    void discardPreview() noexcept {
        bool discarded=false;
        const auto drop=[&](auto& bank,std::size_t offset){
            for(std::size_t i=0;i<bank.size();++i){
                auto& v=bank[i];
                if(!v.active||v.source!=TriggerSource::preview)continue;
                v.active=false;discarded=true;
                if(sampleLeases_[offset+i].data())sampleStore_->release(sampleLeases_[offset+i]);
            }
        };
        drop(voices_,0);drop(retiring_,voices_.size());
        if(discarded)for(auto& grain:grains_){
            if(!grain.active)continue;
            const auto& owner=grain.voiceIndex<voices_.size()?voices_[grain.voiceIndex]:retiring_[grain.voiceIndex-voices_.size()];
            if(!owner.active){grain.active=false;--activeGrains_;}
        }
    }
    void allSoundOff(int channel) noexcept {
        if (channel < 0 || channel > 16) return;
        if (channel == 0) midiKeys_ = {};
        else midiKeys_[static_cast<std::size_t>(channel - 1)] = {};
        for (auto& v : voices_) if (v.active && (channel == 0 || v.channel == channel)) beginRelease(v);
        for(auto& v:retiring_)if(v.active&&(channel==0||v.channel==channel))beginRelease(v);
    }
    // Monitor-only: fade layer contributions, not their clocks, filters or
    // grain ownership. Zero mask monitors all enabled layers.
    void setLayerSoloMask(unsigned mask,bool immediate=false) noexcept {
        mask&=15u;
        if(immediate){
            for(std::size_t i=0;i<soloGains_.size();++i)soloGains_[i]=mask==0||(mask&(1u<<i))?1.0:0.0;
            soloMask_=mask;soloRamping_=false;
        }else if(mask!=soloMask_){soloMask_=mask;soloRamping_=true;}
    }
    // Select the newest rendered, active voice from this exact graph. Ignore
    // retirement slots and voices which still use pre-edit trajectory data.
    std::array<float,5> playhead(std::uint64_t publication) const noexcept {
        std::array<float,5> result{-1,-1,-1,-1,-1};
        const Voice* newest=nullptr;
        for(const auto& v:voices_)if(v.active&&v.publication==publication&&v.renderedPhase>=0&&
                                    (!newest||v.serial>newest->serial))newest=&v;
        if(!newest)return result;
        result[0]=static_cast<float>(newest->renderedPhase);
        for(std::size_t i=0;i<newest->program.layers.size();++i){
            const auto& layer=newest->program.layers[i];
            if(layer.enabled)(void)layer.timing.phaseAt(newest->renderedPhase,result[i+1]);
        }
        return result;
    }
    // Both pointers must address at least count writable samples; outputs are
    // overwritten, not accumulated. The processor validates its bus layout.
    void render(float* left, float* right, int count) noexcept {
        for (int i = 0; i < count; ++i) {
            double l = 0, r = 0;
            if(soloRamping_){
                soloRamping_=false;
                for(std::size_t j=0;j<soloGains_.size();++j){
                    const double target=soloMask_==0||(soloMask_&(1u<<j))?1.0:0.0;
                    auto& gain=soloGains_[j];
                    if(gain!=target){
                        // Snap at the endpoint with rounding tolerance.
                        if(std::abs(gain-target)<=soloStep_+1e-12)gain=target;
                        else {gain+=target>gain?soloStep_:-soloStep_;soloRamping_=true;}
                    }
                }
            }
            hasFilteredGrains_=false;
            const double ppq = barGrid_.at(blockOffset_);
            if (barGrid_.valid()) ++blockOffset_;
            for (std::size_t voice=0; voice<voices_.size(); ++voice) {
                tick(voices_[voice], l, r, voice, ppq);
                if(!voices_[voice].active&&sampleLeases_[voice].data())sampleStore_->release(sampleLeases_[voice]);
            }
            for(std::size_t i=0;i<retiring_.size();++i){
                const auto leaseIndex=voices_.size()+i;
                tick(retiring_[i],l,r,leaseIndex,ppq);
                if(!retiring_[i].active&&sampleLeases_[leaseIndex].data())sampleStore_->release(sampleLeases_[leaseIndex]);
            }
            mixGrains(l, r);
            left[i] = static_cast<float>(l * 0.055);
            right[i] = static_cast<float>(r * 0.055);
        }
    }
private:
    struct MidiKey { std::uint64_t issued = 0, released = 0; };
    MidiKey& midiKey(int channel, int note) noexcept {
        return midiKeys_[static_cast<std::size_t>(channel - 1)][static_cast<std::size_t>(note)];
    }
    static bool isHeldMode(PlayMode mode) noexcept {
        return mode == PlayMode::gate || mode == PlayMode::continuous || mode == PlayMode::loop;
    }
    struct LayerState {
        std::uint32_t rng = 1;
        std::array<source::FilterState,2> filters{};
        double tunedHz=0,transposeRatio=1,grainSum=0;
        int modalNote=60;
        bool filterGrains=false;
        double shape(double x) noexcept {return filters[1].tick(filters[0].tick(x));}
        double lowpass = 0, dcInput = 0, dcOutput = 0, phase = 0, subPhase = 0;
        double svf1 = 0, svf2 = 0;
        std::array<double, 3> formant1 {}, formant2 {};
        std::array<double, 6> partialPhases {};
        std::array<double, 8> mode1 {}, mode2 {};
        bool modesStarted = false;
        std::array<float, 256> delay {};
        unsigned delayWrite = 0, glitchWritten = 0, grainCountdown = 0,glitchStepAge=0;
        double glitchGrid=0;
        std::uint64_t glitchStep=0;
        double grainGain = 0, grainPan = 0;
        bool grainLayerActive = false;
    };
    struct Voice {
        bool active = false;
        int channel = 0, note = 0;
        TriggerSource source = TriggerSource::midi;
        float velocity = 0;
        std::uint64_t serial = 0, noteTicket = 0, publication = 0;
        timeline::SecondsClock clock;
        timeline::BarsClock barsClock;
        double duration = 4.0, layerScale = 1.0, continuousPhase = 0.0;
        double target = 0, targetSpan = 0, lastPhase = 0, renderedPhase = -1;
        bool continuousStarted = false;
        int releaseRemaining = 0, releaseLength = 0;
        double releaseStart=1;unsigned releaseAge=0;
        NoiseProgram program;
        macro::Runtime motion;
        unsigned motionAge=0;
        std::array<LayerState, 4> layers {};
    };
    static double layerTranspose(const LayerState& layer,double global,double local) noexcept {
        const double octaves=((global-.5)+(local-.5))*4.0;
        return octaves==0.0?layer.transposeRatio:layer.transposeRatio*std::exp2(octaves);
    }
    void beginRelease(Voice& v,bool musical=false) noexcept {
        const auto hard=std::max(1,static_cast<int>(rate_*.005));
        if(v.releaseRemaining!=0){
            if(musical||v.releaseRemaining<=hard)return;
            // Shorten a musical tail on panic/steal without jumping gain.
            v.releaseStart*=static_cast<double>(v.releaseRemaining)/v.releaseLength;
            v.releaseLength=v.releaseRemaining=hard;
            return;
        }
        v.releaseAge=v.motionAge;
        v.releaseLength=v.releaseRemaining=musical&&v.motion.hasCustomAdsr()?
            std::max(hard,static_cast<int>(rate_*v.motion.noteReleaseSeconds())):hard;
    }
    void tick(Voice& v, double& left, double& right, std::size_t voiceIndex, double ppq) noexcept {
        if (!v.active) return;
        double eventPhase = 0;
        bool advanced;
        if (v.targetSpan > 0) {
            // Freeze the last phase only during interruption fades (stop,
            // seek, clock loss or steal). The oscillator itself keeps running.
            advanced = v.releaseRemaining != 0 || ppq < v.target - timeline::BarGrid::tolerance;
            if (advanced && v.releaseRemaining == 0)
                v.lastPhase = std::max(v.lastPhase, 1.0 - (v.target - ppq) / v.targetSpan);
            eventPhase = v.lastPhase;
        } else {
            advanced = v.program.bars ? v.barsClock.next(tempo_,eventPhase) : v.clock.next(eventPhase);
        }
        bool heldSustain=false;
        if(!advanced) {
            switch(v.program.mode) {
                case PlayMode::loop:
                    if(v.releaseRemaining==0) {
                        advanced=v.program.bars
                            ? musicalClockAvailable_ && v.barsClock.start(v.program.bars,4,4,rate_) && v.barsClock.next(tempo_,eventPhase)
                            : v.clock.start(v.duration,rate_) && v.clock.next(eventPhase);
                    } else {eventPhase=std::nextafter(1.0,0.0);advanced=true;heldSustain=true;}
                    break;
                case PlayMode::gate:
                    eventPhase=std::nextafter(1.0,0.0);advanced=true;heldSustain=true;break;
                case PlayMode::continuous:
                    // Complete the authored attack timeline once, then evolve
                    // the normalized source position at a slow free-running
                    // rate. This is procedural sustain, not a one-shot loop.
                    if(!v.continuousStarted){v.continuousStarted=true;v.continuousPhase=.25;}
                    else {v.continuousPhase+=.05/rate_;if(v.continuousPhase>=1.0)v.continuousPhase-=1.0;}
                    eventPhase=.5+.49*std::sin(6.283185307179586*v.continuousPhase);
                    advanced=true;heldSustain=true;break;
                default: break;
            }
        }
        if(!advanced){v.active=false;return;}
        v.renderedPhase=eventPhase;
        double release = 1.0;
        const bool releasing=v.releaseRemaining>0;
        if (releasing) {
            release = v.releaseStart * static_cast<double>(v.releaseRemaining) / v.releaseLength;
            if (--v.releaseRemaining == 0) v.active = false;
        }
        using D=mod::Destination;
        const bool hasMotion=v.motion.active();
        const auto movement=hasMotion?([&]{
            v.motion.setTempo(motionTempo_);
            const auto age=releasing&&v.motion.hasCustomAdsr()?v.releaseAge:v.motionAge;
            ++v.motionAge;
            return v.motion.evaluate(macroBases_,0,{static_cast<float>(eventPhase),v.velocity,
                static_cast<float>(age/rate_),static_cast<float>(release)});
        })():mod::Destinations{};
        const auto offsetOf=[&](D dst){return std::clamp(static_cast<double>(movement[macro::Runtime::at(dst)]),-1.0,1.0);};
        const double pitchOffset=offsetOf(D::pitch),cutoffOffset=offsetOf(D::cutoff),qOffset=offsetOf(D::resonance);
        const double grainRate=offsetOf(D::grainRate),gainOffset=offsetOf(D::layerGain),panOffset=offsetOf(D::layerPan);
        const auto phase=static_cast<float>(eventPhase);
        const double globalPitch=v.program.pitchCurve.evaluate(phase);
        const double globalCutoff=v.program.cutoffCurve.evaluate(phase);
        const double globalPan=v.program.panoramaCurve.evaluate(phase);
        const double globalAmplitude=v.program.amplitudeCurve.evaluate(phase);
        for (std::size_t i = 0; i < v.layers.size(); ++i) {
            v.layers[i].grainGain = 0;
            v.layers[i].grainSum = 0;
            v.layers[i].grainLayerActive = false;
            const auto& settings = v.program.layers[i];
            if (!settings.enabled) continue;
            const double offset = settings.timing.offset, span = settings.timing.span;
            if (eventPhase < offset || eventPhase >= offset + span) continue;
            const double t = (eventPhase - offset) / span;
            const auto character=static_cast<double>(settings.character);
            auto& layer = v.layers[i];
            // Refresh TPT coefficients at a bounded rate without clearing
            // integrators. Smoothing of the route takes place per sample.
            if(hasMotion&&(v.motionAge&15u)==1u&&(v.motion.routesTo(D::cutoff)||v.motion.routesTo(D::resonance)))
                for(std::size_t f=0;f<2;++f){auto value=settings.filters[f];if(value.type==source::FilterType::off)continue;
                    value.hz=static_cast<float>(std::clamp(value.hz*std::exp2(cutoffOffset*2),20.,20000.));
                    value.q=static_cast<float>(std::clamp(value.q*std::exp2(qOffset*2),.5,4.));
                    layer.filters[f].prepare(value,rate_);
                }
            const double layerGain=std::clamp(settings.gain+gainOffset,0.,1.);
            layer.rng ^= layer.rng << 13; layer.rng ^= layer.rng >> 17; layer.rng ^= layer.rng << 5;
            const double layerDuration = v.duration * span;
            const double fadeIn = std::min(1.0, t * layerDuration / ((settings.family == NoiseFamily::impact || settings.family == NoiseFamily::laser || settings.family == NoiseFamily::squelch || settings.family == NoiseFamily::metallic) ? 0.001 : 0.01));
            const bool sustainedMode=v.program.mode==PlayMode::gate||v.program.mode==PlayMode::continuous;
            const double fadeOut = (heldSustain||sustainedMode) ? 1.0 : std::min(1.0, (1.0 - t) * layerDuration / 0.02);
            if (settings.family == NoiseFamily::granular) {
                // Each layer schedules into one shared, preallocated 64-grain
                // pool. Capacity exhaustion drops new grains deterministically.
                layer.grainLayerActive = true;
                hasFilteredGrains_ |= layer.filterGrains;
                layer.grainGain = 0.35 * v.velocity * fadeIn * fadeOut * release * v.layerScale * layerGain *
                                  settings.gainCurve.evaluate(static_cast<float>(t)) *
                                  globalAmplitude;
                layer.grainPan = std::clamp(static_cast<double>(settings.pan) + panOffset +
                                 2.0 * settings.panCurve.evaluate(static_cast<float>(t)) - 1.0 +
                                 2.0 * globalPan - 1.0, -1.0, 1.0);
                if (layer.grainCountdown == 0) {
                    for (auto& grain:grains_) if (!grain.active) {
                        grain.active=true; ++activeGrains_; grain.voiceIndex=voiceIndex; grain.layerIndex=i; grain.serial=v.serial;
                        const auto& lease=sampleLeases_[voiceIndex];
                        const auto length=lease.data()?lease.frames():sample_.size();
                        grain.position=settings.grains.position<0?static_cast<double>(layer.rng % length):
                            static_cast<double>(settings.grains.position)*(length-1);
                        const double localPitch=settings.pitchCurve.evaluate(static_cast<float>(t));
                        const double pitchRatio=layerTranspose(layer,globalPitch+pitchOffset*.25,localPitch);
                        const auto ratio=std::clamp(layer.tunedHz / noteHz_[60] * pitchRatio, 0.25, 4.0);
                        grain.speed=lease.data()?samplePlaybackStep(lease.rate(),rate_,ratio):ratio;
                        if(settings.grains.reverse)grain.speed=-grain.speed;
                        grain.age=0; grain.length=settings.grains.milliseconds==80?grainLength_:
                            static_cast<unsigned>(std::max(2.,rate_*(settings.grains.milliseconds/1000.)));break;
                    }
                    const auto density=std::clamp(grainRate+character,-1.,1.);
                    layer.grainCountdown=density==0?grainInterval_:static_cast<unsigned>(std::max(1.0,grainInterval_*std::exp2(-density)));
                }
                --layer.grainCountdown;
                continue;
            }
            const double localPitch=settings.pitchCurve.evaluate(static_cast<float>(t));
            const double pitchRatio=layerTranspose(layer,globalPitch+pitchOffset*.25,localPitch);
            const double localCutoff=settings.cutoffCurve.evaluate(static_cast<float>(t));
            const double cutoffMotion=(globalCutoff-.5)+(localCutoff-.5)+cutoffOffset*.5+(settings.family==NoiseFamily::riser?character*.5:0.);
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
            double coefficient = descending
                ? coefficients_[tableIntervals - index] + fraction * (coefficients_[tableIntervals - index - 1] - coefficients_[tableIntervals - index])
                : coefficients_[filterIndex] + filterFraction * (coefficients_[filterIndex + 1] - coefficients_[filterIndex]);
            if(cutoffMotion!=0.0)coefficient*=std::exp2(cutoffMotion*4.0);
            coefficient=std::clamp(coefficient,0.0,1.0);
            layer.lowpass += coefficient * (noise - layer.lowpass);
            const double filtered = layer.lowpass - layer.dcInput + 0.995 * layer.dcOutput;
            layer.dcInput = layer.lowpass; layer.dcOutput = filtered;
            double source = filtered;
            double envelope = 0.15 + 0.85 * t;
            if (settings.family == NoiseFamily::glitch) {
                // Capture this layer's own synthesized signal into a bounded
                // 256-sample loop, then windowed slices on an event-local
                // sixteenth grid. Beat phase integrates tempo per sample.
                // There is no external audio input or host-PPQ alignment.
                const auto step=static_cast<std::uint64_t>(layer.glitchGrid);
                const double fractionOfStep=layer.glitchGrid-static_cast<double>(step);
                if(step!=layer.glitchStep){layer.glitchStep=step;layer.glitchStepAge=0;}
                const unsigned sliceAge=layer.glitchStepAge++;
                layer.glitchGrid+=glitchGridIncrement_;
                const double frequency = std::min(layer.tunedHz * pitchRatio * 2.0, rate_ * 0.4);
                const double dry = 0.7 * std::sin(6.283185307179586 * layer.phase) + 0.3 * noise;
                layer.phase += frequency / rate_; if (layer.phase >= 1.0) layer.phase -= 1.0;
                if (layer.glitchWritten < layer.delay.size()) {
                    layer.delay[layer.glitchWritten++] = static_cast<float>(dry);
                    source = dry * 0.5;
                } else {
                    const unsigned speed = (step & 1u) != 0 ? 2u : 1u;
                    const auto sliceIndex = (sliceAge * speed) & 255u;
                    const double window = std::sin(3.141592653589793 * static_cast<double>(sliceIndex) / 255.0);
                    const double edge = std::min(1.0, std::min(fractionOfStep, 1.0-fractionOfStep) * 125.0);
                    source = (step % 4 == 2 ? 0.0 : layer.delay[sliceIndex] * window * window * edge);
                    if(character!=0){ // toward dry pulse or sharper slice gating, neutral is legacy
                        const double amount=std::abs(character)*.6;
                        source=character<0?source*(1-amount)+dry*.5*amount:source*(1-amount*(1-window));
                    }
                }
                envelope = 0.75;
            } else if (settings.family == NoiseFamily::formant) {
                // Three independently moving resonant band-pass formants
                // excited by a tonal oscillator and a small noise component.
                const double elapsed = t * layerDuration;
                const double frequency = std::min(layer.tunedHz * pitchRatio, rate_ * 0.4);
                const double input = 0.7 * std::sin(6.283185307179586 * layer.phase) + 0.3 * noise;
                layer.phase += frequency / rate_; if (layer.phase >= 1.0) layer.phase -= 1.0;
                constexpr std::array<double, 3> centers {0.25, 0.48, 0.70};
                constexpr std::array<double, 3> depths {0.11, 0.08, 0.06};
                source = 0.0;
                for (std::size_t formant = 0; formant < centers.size(); ++formant) {
                    const double morph = std::sin(6.283185307179586 * elapsed * 0.24 + formant * 1.3);
                    const double pos = (centers[formant] + depths[formant] * morph + character*(static_cast<double>(formant)-1)*.11) * tableIntervals;
                    const auto gi = static_cast<std::size_t>(pos);
                    const double g = svfG_[gi] + (pos - static_cast<double>(gi)) * (svfG_[gi+1] - svfG_[gi]);
                    constexpr double damping = 0.32;
                    const double a1 = 1.0 / (1.0 + g * (g + damping));
                    const double v3 = input - layer.formant2[formant];
                    const double v1 = a1 * layer.formant1[formant] + g * a1 * v3;
                    const double v2 = layer.formant2[formant] + g * v1;
                    layer.formant1[formant] = 2.0 * v1 - layer.formant1[formant];
                    layer.formant2[formant] = 2.0 * v2 - layer.formant2[formant];
                    source += v1;
                }
                source *= 0.15;
                envelope = 0.7;
            } else if (settings.family == NoiseFamily::texture) {
                // Two continuously phased detuned sine oscillators and
                // filtered noise, with slow level and stereo evolution.
                const double elapsed = t * layerDuration;
                const double frequency = std::min(layer.tunedHz * pitchRatio, rate_ * 0.40);
                const double a = std::sin(6.283185307179586 * layer.phase);
                const double b = std::sin(6.283185307179586 * layer.subPhase);
                layer.phase += frequency / rate_; if (layer.phase >= 1.0) layer.phase -= 1.0;
                layer.subPhase += std::min(frequency * (1.007+.006*character), rate_ * 0.45) / rate_; if (layer.subPhase >= 1.0) layer.subPhase -= 1.0;
                source = 0.3 * (a + b) + 0.15 * filtered;
                envelope = 0.65 + 0.15 * std::sin(6.283185307179586 * elapsed * 0.17);
            } else if (settings.family == NoiseFamily::metallic) {
                // Eight stable damped inharmonic modes excited once per layer.
                // Coefficients are prepared at the active sample rate; modes
                // beyond the anti-alias cutoff are omitted.
                const auto& modes = modal_[static_cast<std::size_t>(layer.modalNote)];
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
                    source += next*(1+character*(static_cast<double>(mode)/7-.5));
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
                const double frequency = std::min(layer.tunedHz * pitchRatio * (1.0 + 3.0 * read(impactPitch_)), 0.4 * rate_);
                const double exciter = 0.7 * std::sin(6.283185307179586 * layer.phase) + 0.3 * noise * read(impactNoise_);
                layer.phase += frequency / rate_; if (layer.phase >= 1.0) layer.phase -= 1.0;
                const double sweep = std::clamp(1.0 - std::abs(elapsed / 0.15 - 1.0), 0.0, 1.0);
                const double gp = std::min(sweep * tableIntervals, std::nextafter(static_cast<double>(tableIntervals), 0.0));
                const auto gi = static_cast<std::size_t>(gp);
                const double g = svfG_[gi] + (gp - static_cast<double>(gi)) * (svfG_[gi+1] - svfG_[gi]);
                const double damping = 0.28-.15*character;
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
                const double frequency = std::min(layer.tunedHz * pitchRatio * (2.0 + (12.0+8*character) * read(impactPitch_)), 0.40 * rate_);
                source = 0.8 * std::sin(6.283185307179586 * layer.phase) + 0.2 * noise * read(impactNoise_);
                layer.phase += frequency / rate_; if (layer.phase >= 1.0) layer.phase -= 1.0;
                envelope = read(impactNoise_);
            } else if (settings.family == NoiseFamily::whoosh) {
                // Bounded variable delay on this layer's own signal. Its
                // slowly moving read tap supplies a subtle Doppler shift.
                constexpr unsigned mask = 255;
                const double delaySamples = 1.0 + t * (std::min(254.0, rate_ * 0.002*(1+.60*character)) - 1.0);
                const auto whole = static_cast<unsigned>(delaySamples);
                const double mix = delaySamples - whole;
                const auto read0 = (layer.delayWrite - whole) & mask;
                const auto read1 = (read0 - 1) & mask;
                const double delayed = layer.delay[read0] * (1.0 - mix) + layer.delay[read1] * mix;
                layer.delay[layer.delayWrite] = static_cast<float>(filtered);
                layer.delayWrite = (layer.delayWrite + 1) & mask;
                const double frequency = std::min(layer.tunedHz * pitchRatio * (2.0 - 1.5 * t), rate_ * 0.45);
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
                const double fundamental = std::clamp(layer.tunedHz * pitchRatio * 0.19, 30.0, 160.0);
                const double frequency = std::min(fundamental * (1.0 + 3.0 * read(impactPitch_)), rate_ * 0.45);
                const double body = std::sin(6.283185307179586 * layer.phase);
                const double sub = std::sin(6.283185307179586 * layer.subPhase);
                layer.phase += frequency / rate_; if (layer.phase >= 1.0) layer.phase -= 1.0;
                layer.subPhase += fundamental * 0.5 / rate_; if (layer.subPhase >= 1.0) layer.subPhase -= 1.0;
                source = (0.65 * body + (0.20+.15*character) * sub) * read(impactBody_) + 0.25 * noise * read(impactNoise_);
                envelope = 1.0;
            } else if (settings.family == NoiseFamily::shepard) {
                // Six octave-spaced sine partials traverse a six-octave
                // window. At each wrap, the Hann-like weight is zero, so
                // resetting the frequency by six octaves is masked.
                const double factor = risingFactors_[index] + fraction * (risingFactors_[index + 1] - risingFactors_[index]);
                source = 0.0;
                constexpr std::array<double,6> octaveRatios {0.125, 0.25, 0.5, 1.0, 2.0, 4.0};
                for (std::size_t partial = 0; partial < layer.partialPhases.size(); ++partial) {
                    double octave = octaveRatios[partial] * factor;
                    if (static_cast<double>(partial) + 3.0 * t >= 6.0) octave *= 1.0 / 64.0;
                    const double frequency = layer.tunedHz * pitchRatio * octave;
                    const double weight = (shepardWeights_[partial][index] + fraction * (shepardWeights_[partial][index + 1] - shepardWeights_[partial][index]))*
                        (1+character*(static_cast<double>(partial)/5-.5));
                    auto& phase = layer.partialPhases[partial];
                    // Fade partials to zero before Nyquist rather than abruptly
                    // gating a moving partial as it crosses the cutoff.
                    const double antiAlias = std::clamp((0.45 * rate_ - frequency) / (0.05 * rate_), 0.0, 1.0);
                    if (weight > 0.0 && antiAlias > 0.0)
                        source += weight * antiAlias * std::sin(6.283185307179586 * phase);
                    phase += std::min(frequency, 0.45 * rate_) / rate_;
                    if (phase >= 1.0) phase -= 1.0;
                }
                source *= 1.0 / 3.0;
                envelope = 0.8;
            } else if (descending) {
                const double pitchFactor = pitchFactors_[index] + fraction * (pitchFactors_[index + 1] - pitchFactors_[index]);
                const double frequency = std::min(layer.tunedHz * pitchRatio * pitchFactor, 0.45 * rate_);
                const double tonal = std::sin(6.283185307179586 * layer.phase);
                layer.phase += frequency / rate_;
                if (layer.phase >= 1.0) layer.phase -= 1.0;
                source = (0.5-.4*character)*filtered+(0.5+.4*character)*tonal;
                envelope = 1.0 - 0.85 * t;
            }
            const double sample = layer.shape(source) * v.velocity * envelope * fadeIn * fadeOut * release *
                                  v.layerScale * layerGain * settings.gainCurve.evaluate(static_cast<float>(t)) *
                                  globalAmplitude;
            const double pan = std::clamp(static_cast<double>(settings.pan) + panOffset +
                                          2.0 * settings.panCurve.evaluate(static_cast<float>(t)) - 1.0 +
                                          2.0 * globalPan - 1.0 +
                                          (settings.family == NoiseFamily::whoosh ? 2.0 * t - 1.0 : 0.0) +
                                          (settings.family == NoiseFamily::texture ? 0.35 * std::sin(6.283185307179586 * t * layerDuration * 0.11) : 0.0), -1.0, 1.0);
            const double monitored=sample*soloGains_[i];
            left += monitored * std::min(1.0, 1.0 - pan);
            right += monitored * std::min(1.0, 1.0 + pan);
        }
    }
    struct Grain {
        bool active=false;
        std::size_t voiceIndex=0, layerIndex=0;
        std::uint64_t serial=0;
        double position=0, speed=1;
        unsigned age=0, length=0;
    };
    void mixGrains(double& left, double& right) noexcept {
        if (activeGrains_ != 0) for (auto& grain:grains_) {
            if (!grain.active) continue;
            auto& owner = grain.voiceIndex < voices_.size() ? voices_[grain.voiceIndex] : retiring_[grain.voiceIndex-voices_.size()];
            if (!owner.active || owner.serial != grain.serial || grain.age >= grain.length) { grain.active=false; --activeGrains_; continue; }
            auto& layer = owner.layers[grain.layerIndex];
            if (!layer.grainLayerActive) { grain.active=false; --activeGrains_; continue; }
            const auto& lease=sampleLeases_[grain.voiceIndex];
            const auto* data=lease.data()?lease.data():sample_.data();
            const auto length=lease.data()?lease.frames():sample_.size();
            // Position is initialized in [0,length) and wrapped after every
            // increment. Negative wrapping can round to exactly length; treat
            // that as the first index with zero fraction, as the modulo did.
            const auto integral=static_cast<std::size_t>(grain.position);
            const auto at=integral==length?0:integral;
            const auto next=at+1==length?0:at+1;
            const double frac=grain.position-static_cast<double>(integral);
            const double source=data[at] + frac * (data[next] - data[at]);
            const double windowPos=static_cast<double>(grain.age) * (grainWindow_.size()-1) / (grain.length-1);
            const auto w=static_cast<std::size_t>(windowPos);
            const auto wn=std::min(w+1,grainWindow_.size()-1);
            const double window=grainWindow_[w] + (windowPos-w)*(grainWindow_[wn]-grainWindow_[w]);
            if(layer.filterGrains) layer.grainSum+=source*window;
            else {
                const double sample=source*window*layer.grainGain*soloGains_[grain.layerIndex];
                left+=sample*std::min(1.0,1.0-layer.grainPan);
                right+=sample*std::min(1.0,1.0+layer.grainPan);
            }
            grain.position+=grain.speed;
            if (grain.position >= length || grain.position < 0){
                grain.position=std::fmod(grain.position,static_cast<double>(length));
                if(grain.position<0)grain.position+=length;
            }
            if (++grain.age >= grain.length) { grain.active=false; --activeGrains_; }
        }
        const auto mixFiltered=[&](auto& voices){
            for(auto& voice:voices)if(voice.active)for(std::size_t i=0;i<voice.layers.size();++i){
                auto& layer=voice.layers[i];
                if(layer.grainLayerActive&&layer.filterGrains){
                    const double sample=layer.shape(layer.grainSum)*layer.grainGain*soloGains_[i];
                    left+=sample*std::min(1.0,1.0-layer.grainPan);
                    right+=sample*std::min(1.0,1.0+layer.grainPan);
                }
            }
        };
        if(hasFilteredGrains_){mixFiltered(voices_);mixFiltered(retiring_);}
    }
    void releaseSampleLeases() noexcept {
        if(sampleStore_)for(auto& lease:sampleLeases_)sampleStore_->release(lease);
    }
    static constexpr std::size_t tableIntervals = 4096;
    std::array<double, tableIntervals + 1> coefficients_ {}, pitchFactors_ {}, risingFactors_ {};
    std::array<double, tableIntervals + 1> impactPitch_ {}, impactBody_ {}, impactNoise_ {};
    std::array<double, tableIntervals + 1> svfG_ {}, squelchDecay_ {};
    std::array<std::array<double, tableIntervals + 1>, 6> shepardWeights_ {};
    struct ModalCoefficient { double a1 = 0, a2 = 0; bool enabled = false; };
    std::array<double, 128> noteHz_ {};
    std::array<std::array<ModalCoefficient, 8>, 128> modal_ {};
    std::array<float, 8192> sample_ {};
    std::array<double, 1025> grainWindow_ {};
    std::array<Grain, 64> grains_ {};
    unsigned grainLength_ = 2, grainInterval_ = 1, activeGrains_ = 0;
    NoiseProgram program_;
    std::uint64_t programPublication_=0;
    std::array<double,4> soloGains_{1,1,1,1};
    double soloStep_=.005;
    unsigned soloMask_=0;
    bool soloRamping_=false;
    bool hasFilteredGrains_=false;
    macro::Runtime voiceMotion_;
    std::array<float,8> macroBases_{.5f,.5f,.5f,.5f,.5f,.5f,.5f,.5f};
    std::array<std::array<MidiKey,128>,16> midiKeys_ {};
    double tempo_ = 120.0, motionTempo_ = 120.0;
    bool musicalClockAvailable_ = false;
    timeline::BarGrid barGrid_;
    std::uint64_t blockOffset_ = 0;
    double rate_ = 44100.0,glitchGridIncrement_=120.0/(15.0*44100.0);
    std::uint64_t serial_ = 0;
    std::uint32_t droppedEvents_=0;
    std::array<Voice, 8> voices_ {};
    std::array<Voice,4> retiring_ {};
    sample::Store* sampleStore_=nullptr; // non-owning, lifetime guaranteed by caller
    std::array<sample::Store::Lease,12> sampleLeases_{};
};
} // namespace vox::fx
