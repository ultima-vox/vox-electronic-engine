#include "engine/SynthEngine.h"

#include <algorithm>
#include <cmath>

namespace vox::syn::engine {
namespace {

constexpr float pi = 3.14159265358979323846f;
constexpr int fadeMs = 3;

float flush (float value) noexcept
{
    return std::fabs (value) < 1.0e-20f ? 0.0f : value;
}

double midiHz (double note) noexcept
{
    return 440.0 * std::exp2 ((note - 69.0) / 12.0);
}

int keyIndex (int channel, int note) noexcept
{
    return channel * 128 + note;
}

bool monoEngine (EngineKind kind) noexcept
{
    return kind != EngineKind::va;
}

float shapedOsc (VaOsc& osc, int wave, double dt, float pulseWidth) noexcept
{
    return osc.next (wave, dt, pulseWidth);
}

} // namespace

Wavetable::Wavetable() noexcept
{
    constexpr int maxHarmonic = 48;
    for (int frame = 0; frame < frames; ++frame)
    {
        const int harmonics = 1 + (frame * (maxHarmonic - 1)) / (frames - 1);
        float peak = 0.0f;
        for (int i = 0; i < cycle; ++i)
        {
            double sum = 0.0;
            const double phase = (2.0 * 3.14159265358979323846 * static_cast<double> (i)) / static_cast<double> (cycle);
            for (int harmonic = 1; harmonic <= harmonics; ++harmonic)
                sum += std::sin (phase * harmonic) / harmonic;
            data[frame][i] = static_cast<float> (sum);
            peak = std::max (peak, std::fabs (data[frame][i]));
        }
        const float scale = peak > 1.0e-8f ? 0.9f / peak : 0.0f;
        for (int i = 0; i < cycle; ++i)
            data[frame][i] *= scale;
    }
}

float Wavetable::read (float position, double phase, double cyclesPerSample) const noexcept
{
    const int user = std::clamp (static_cast<int> (position * static_cast<float> (frames - 1)), 0, frames - 1);
    const int safe = std::clamp (static_cast<int> (0.45 / std::max (cyclesPerSample, 1.0e-8)), 1, 48);
    const int mip = std::clamp ((safe - 1) * (frames - 1) / (48 - 1), 0, frames - 1);
    const int frame = std::min (user, mip);
    const double wrapped = phase - std::floor (phase);
    const double pos = wrapped * static_cast<double> (cycle);
    const int index = static_cast<int> (pos) % cycle;
    const float frac = static_cast<float> (pos - std::floor (pos));
    const float a = data[frame][index];
    const float b = data[frame][(index + 1) % cycle];
    return a + (b - a) * frac;
}

const Wavetable& sharedWavetable() noexcept
{
    static const Wavetable table;
    return table;
}

void SynthEngine::prepare (double sampleRate, int maximumBlockSize) noexcept
{
    if (! std::isfinite (sampleRate) || sampleRate < 8000.0 || sampleRate > 384000.0
        || maximumBlockSize <= 0)
    {
        prepared_ = false;
        return;
    }
    sampleRate_ = sampleRate;
    maximumBlockSize_ = maximumBlockSize;
    prepared_ = true;
    (void) sharedWavetable();
    reset();
}

void SynthEngine::reset() noexcept
{
    age_ = 1;
    for (int index = 0; index < partCount; ++index)
    {
        auto& part = parts_[index];
        part.mono = {};
        part.latest = noKey;
        part.bendSemis = 0.0f;
        for (auto& key : part.keys)
            key = {};
        for (auto& down : part.sustain)
            down = false;
        part.engine = index == 0 ? EngineKind::psyBass : EngineKind::acid;
    }
    for (auto& voice : voices_)
        voice = {};
    for (auto& row : lfo_)
        for (auto& lfo : row)
            lfo = {};
    for (auto& mod : mod_)
        mod = {};
}

int SynthEngine::activeVoices() const noexcept
{
    return used();
}

double SynthEngine::monoPhase (int part) const noexcept
{
    return monoOscPhase (part, 0);
}

double SynthEngine::monoOscPhase (int part, int osc) const noexcept
{
    if (part < 0 || part >= partCount)
        return 0.0;
    const auto& mono = parts_[part].mono;
    if (osc == 1)
        return mono.square.phase;
    if (osc == 2)
        return mono.sub.phase;
    return mono.osc.phase;
}

double SynthEngine::monoFrequency (int part) const noexcept
{
    if (part < 0 || part >= partCount)
        return 0.0;
    return parts_[part].mono.frequency;
}

int SynthEngine::monoNote (int part) const noexcept
{
    if (part < 0 || part >= partCount)
        return -1;
    return parts_[part].mono.note;
}

bool SynthEngine::monoGate (int part) const noexcept
{
    if (part < 0 || part >= partCount)
        return false;
    return parts_[part].mono.gate;
}

float SynthEngine::monoAccent (int part) const noexcept
{
    if (part < 0 || part >= partCount)
        return 0.0f;
    return parts_[part].mono.accent;
}

int SynthEngine::glideSource (int part) const noexcept
{
    if (part < 0 || part >= partCount)
        return 0;
    return parts_[part].mono.glideSource;
}

bool SynthEngine::vaNoteHeld (int part, int note) const noexcept
{
    if (part < 0 || part >= partCount || note < 0 || note > 127)
        return false;
    for (const auto& voice : voices_)
        if (voice.active && ! voice.dying && voice.gate && voice.part == part && voice.note == note)
            return true;
    return false;
}

int SynthEngine::vaFilterStage (int part, int note) const noexcept
{
    if (part < 0 || part >= partCount || note < 0 || note > 127)
        return -1;
    for (const auto& voice : voices_)
        if (voice.active && ! voice.dying && voice.part == part && voice.note == note)
            return static_cast<int> (voice.filterAmp.stage);
    return -1;
}

float SynthEngine::vaAccentGain (int part, int note) const noexcept
{
    if (part < 0 || part >= partCount || note < 0 || note > 127)
        return 0.0f;
    for (const auto& voice : voices_)
        if (voice.active && ! voice.dying && voice.part == part && voice.note == note)
            return voice.accentGain;
    return 0.0f;
}

int SynthEngine::vaActive (int part) const noexcept
{
    if (part < 0 || part >= partCount)
        return 0;
    int count = 0;
    for (const auto& voice : voices_)
        if (voice.active && ! voice.dying && voice.part == part)
            ++count;
    return count;
}

double SynthEngine::lfoPhase (int part, int index) const noexcept
{
    if (part < 0 || part >= partCount || index < 0 || index >= 4)
        return 0.0;
    return lfo_[part][index].phase;
}

double SynthEngine::vaRetrigPhase (int part, int note, int index) const noexcept
{
    if (part < 0 || part >= partCount || note < 0 || note > 127 || index < 0 || index >= 4)
        return 0.0;
    for (const auto& voice : voices_)
        if (voice.active && ! voice.dying && voice.part == part && voice.note == note)
            return voice.retrig[index].phase;
    return 0.0;
}

float SynthEngine::modCutoff (int part) const noexcept
{
    if (part < 0 || part >= partCount)
        return 0.0f;
    return lastCutoff_[part];
}

int SynthEngine::vaFadeLeft() const noexcept
{
    int left = 0;
    for (const auto& voice : voices_)
        if (voice.active && voice.dying)
            left = std::max (left, voice.fadeLeft);
    return left;
}

int SynthEngine::used() const noexcept
{
    int count = 0;
    for (const auto& part : parts_)
        if (monoEngine (part.engine) && part.mono.active)
            ++count;
    for (const auto& voice : voices_)
        if (voice.active)
            ++count;
    return count;
}

SynthEngine::VaVoice* SynthEngine::oldestVa() noexcept
{
    VaVoice* found = nullptr;
    for (auto& voice : voices_)
    {
        if (! voice.active || voice.dying)
            continue;
        if (found == nullptr || voice.age < found->age)
            found = &voice;
    }
    if (found != nullptr)
        return found;
    for (auto& voice : voices_)
        if (voice.active)
            return &voice;
    return nullptr;
}

void SynthEngine::silencePart (int part) noexcept
{
    auto& slot = parts_[part];
    slot.mono = {};
    slot.latest = noKey;
    for (auto& key : slot.keys)
        key = {};
    for (auto& voice : voices_)
        if (voice.part == part)
            voice = {};
}

void SynthEngine::latchEngine (int part, EngineKind next) noexcept
{
    auto& slot = parts_[part];
    if (slot.engine == next)
        return;
    int heldNote = -1;
    int heldChannel = 0;
    int heldVelocity = 100;
    if (slot.latest != noKey && slot.keys[slot.latest].alive)
    {
        heldNote = static_cast<int> (slot.latest % 128);
        heldChannel = static_cast<int> (slot.latest / 128);
        heldVelocity = slot.keys[slot.latest].velocity > 0 ? slot.keys[slot.latest].velocity : 100;
    }
    else if (slot.mono.gate && slot.mono.note >= 0)
    {
        heldNote = slot.mono.note;
        heldVelocity = std::clamp (static_cast<int> (slot.mono.velocity * 127.0f + 0.5f), 1, 127);
    }
    silencePart (part);
    slot.engine = next;
    if (heldNote < 0)
        return;
    pushKey (slot, keyIndex (heldChannel, heldNote), heldVelocity);
    const float gain = static_cast<float> (heldVelocity) / 127.0f;
    if (monoEngine (next))
        startMono (part, heldNote, gain, false, {});
    else
        startVa (part, heldChannel, heldNote, gain, {});
}

void SynthEngine::pushKey (Part& part, int key, int velocity) noexcept
{
    removeKey (part, key);
    auto& held = part.keys[key];
    held.velocity = static_cast<std::uint8_t> (velocity);
    held.stack = 1;
    held.alive = true;
    held.down = true;
    held.previous = noKey;
    held.next = part.latest;
    if (part.latest != noKey)
        part.keys[part.latest].previous = static_cast<std::uint16_t> (key);
    part.latest = static_cast<std::uint16_t> (key);
}

void SynthEngine::removeKey (Part& part, int key) noexcept
{
    auto& held = part.keys[key];
    if (! held.alive)
        return;
    if (held.previous != noKey)
        part.keys[held.previous].next = held.next;
    else if (part.latest == key)
        part.latest = held.next;
    if (held.next != noKey)
        part.keys[held.next].previous = held.previous;
    held = {};
}

void SynthEngine::freeMonoRoom() noexcept
{
    while (used() >= maxVoices)
    {
        auto* voice = oldestVa();
        if (voice == nullptr)
            return;
        *voice = {};
    }
}

void SynthEngine::startMono (int part, int note, float velocity, bool legato, NoteArticulation art) noexcept
{
    if (! monoEngine (parts_[part].engine))
        return;
    auto& mono = parts_[part].mono;
    const bool was = mono.active && mono.gate;
    const double target = midiHz (static_cast<double> (note) + parts_[part].bendSemis);
    if (! was)
        freeMonoRoom();
    mono.targetFrequency = target;
    mono.velocityTarget = velocity;
    mono.note = note;
    mono.gate = true;
    mono.active = true;
    mono.glide = (legato && was) || (art.forceSlide && was);
    const auto& knobs = live_[part];
    if (art.forceSlide && was)
        mono.glideSource = 1;
    else if (mono.glide && knobs.slide > 0.01f)
        mono.glideSource = 1;
    else if (was && knobs.portamento > 0.01f)
        mono.glideSource = 2;
    else
        mono.glideSource = 0;
    mono.glide = mono.glideSource != 0;
    mono.slideFloor = art.forceSlide && was ? 0.45f : 0.0f;
    const bool accented = art.forceAccent
                       || (parts_[part].engine == EngineKind::acid && velocity >= (110.0f / 127.0f));
    mono.accentTarget = accented ? 1.0f : 0.0f;
    if (! mono.glide)
    {
        mono.velocity = velocity;
        mono.accent = mono.accentTarget;
        mono.frequency = target;
        mono.osc.reset();
        mono.square.reset();
        mono.sub.reset();
        mono.filterEnv = 1.0f;
        mono.amp = 0.0f;
        mono.svf.reset();
        mono.ladder[0] = mono.ladder[1] = mono.ladder[2] = mono.ladder[3] = 0.0f;
        mono.pitchTotal = std::max (1, static_cast<int> (sampleRate_ * knobs.pitchSeconds));
        mono.pitchLeft = mono.pitchTotal;
    }
}

void SynthEngine::finishVoice (VaVoice& slot, int part, int channel, int note, float velocity, float accent,
                               int index, int copies, float cents, float pan) noexcept
{
    slot = {};
    slot.active = true;
    slot.gate = true;
    slot.part = part;
    slot.channel = channel;
    slot.note = note;
    slot.velocity = velocity;
    slot.accentGain = accent;
    slot.age = ++age_;
    slot.noise = 0x9e3779b9u ^ (static_cast<std::uint32_t> (note) * 0x85ebca6bu) ^ static_cast<std::uint32_t> (slot.age);
    slot.fade = 1.0f;
    slot.unisonIndex = index;
    slot.unisonCopies = std::max (1, copies);
    slot.detuneCents = cents;
    slot.pan = pan;
    const double offset = copies <= 1 ? 0.0 : static_cast<double> (index) / static_cast<double> (copies);
    slot.oscA.core.phase = offset;
    slot.oscB.core.phase = offset * 0.5;
    slot.wtPhase = offset;
    slot.amp.gate();
    slot.filterAmp.gate();
}

void SynthEngine::startVa (int part, int channel, int note, float velocity, NoteArticulation art) noexcept
{
    if (parts_[part].engine != EngineKind::va)
        return;
    const float accent = art.forceAccent ? 1.35f : 1.0f;
    bool held = false;
    for (auto& voice : voices_)
    {
        if (! voice.active || voice.dying || voice.part != part || voice.note != note || voice.channel != channel)
            continue;
        held = true;
        voice.velocity = velocity;
        voice.accentGain = accent;
        voice.amp.gate();
        voice.filterAmp.gate();
        voice.gate = true;
        voice.age = ++age_;
        for (int index = 0; index < 4; ++index)
            if (live_[part].lfoRetrig[index])
                voice.retrig[index] = {};
    }
    if (held)
        return;

    const int copies = std::clamp (live_[part].unison, 1, 4);
    const float mid = 0.5f * static_cast<float> (copies - 1);
    for (int index = 0; index < copies; ++index)
    {
        const float cents = (static_cast<float> (index) - mid) * live_[part].unisonDetune;
        const float pan = copies <= 1 ? 0.0f : ((static_cast<float> (index) - mid) / std::max (0.5f, mid)) * live_[part].unisonSpread;
        if (used() < maxVoices)
        {
            VaVoice* slot = nullptr;
            for (auto& voice : voices_)
                if (! voice.active)
                {
                    slot = &voice;
                    break;
                }
            if (slot != nullptr)
            {
                finishVoice (*slot, part, channel, note, velocity, accent, index, copies, cents, pan);
                continue;
            }
        }
        VaVoice* slot = oldestVa();
        if (slot == nullptr)
            return;
        slot->dying = true;
        slot->pendingNote = note;
        slot->pendingVelocity = velocity;
        slot->pendingAccent = accent;
        slot->pendingPart = part;
        slot->pendingUnison = index;
        slot->pendingCopies = copies;
        slot->pendingDetune = cents;
        slot->pendingPan = pan;
        slot->fadeTotal = std::max (1, static_cast<int> (sampleRate_ * (fadeMs / 1000.0)));
        slot->fadeLeft = slot->fadeTotal;
        slot->fade = 1.0f;
        slot->channel = channel;
        if (slot->gate)
            slot->filterAmp.startRelease (std::max (0.001, live_[part].filterRelease), sampleRate_);
    }
}

void SynthEngine::restoreLatest (int part, bool fromSlide) noexcept
{
    auto& slot = parts_[part];
    if (slot.latest == noKey)
    {
        slot.mono.gate = false;
        return;
    }
    const int note = slot.latest % 128;
    const float velocity = static_cast<float> (slot.keys[slot.latest].velocity) / 127.0f;
    if (monoEngine (slot.engine))
        startMono (part, note, std::max (0.15f, velocity), fromSlide, {});
    else
        startVa (part, slot.latest / 128, note, std::max (0.15f, velocity), {});
}

void SynthEngine::noteOn (int part, int channel, int note, int velocity, NoteArticulation art) noexcept
{
    if (channel < 0 || channel > 15 || note < 0 || note > 127 || velocity <= 0)
        return;
    auto& slot = parts_[part];
    const int key = keyIndex (channel, note);
    const int stacked = slot.keys[key].alive ? slot.keys[key].stack : 0;
    const bool had = slot.mono.gate && slot.mono.active;
    pushKey (slot, key, velocity);
    slot.keys[key].stack = static_cast<std::uint8_t> (std::min (255, stacked + 1));
    const float gain = static_cast<float> (velocity) / 127.0f;
    if (monoEngine (slot.engine))
        startMono (part, note, gain, had, art);
    else
        startVa (part, channel, note, gain, art);
}

void SynthEngine::releaseVa (int part, int channel, int note) noexcept
{
    for (auto& voice : voices_)
        if (voice.active && voice.part == part && voice.channel == channel && voice.note == note && ! voice.dying)
        {
            voice.gate = false;
            voice.amp.startRelease (live_[part].release, sampleRate_);
            voice.filterAmp.startRelease (std::max (0.001, live_[part].filterRelease), sampleRate_);
        }
}

void SynthEngine::noteOff (int part, int channel, int note) noexcept
{
    if (channel < 0 || channel > 15 || note < 0 || note > 127)
        return;
    auto& slot = parts_[part];
    const int key = keyIndex (channel, note);
    if (! slot.keys[key].alive)
        return;
    if (slot.keys[key].stack > 1)
    {
        --slot.keys[key].stack;
        return;
    }
    if (slot.sustain[channel])
    {
        slot.keys[key].down = false;
        return;
    }
    const bool wasLatest = slot.latest == key;
    removeKey (slot, key);
    if (monoEngine (slot.engine))
    {
        if (wasLatest)
            restoreLatest (part, true);
    }
    else
        releaseVa (part, channel, note);
}

void SynthEngine::pedal (int part, int channel, bool down) noexcept
{
    auto& slot = parts_[part];
    slot.sustain[channel] = down;
    if (down)
        return;
    const auto latest = slot.latest;
    for (int note = 0; note < 128; ++note)
    {
        const int key = keyIndex (channel, note);
        if (slot.keys[key].alive && ! slot.keys[key].down)
        {
            removeKey (slot, key);
            if (! monoEngine (slot.engine))
                releaseVa (part, channel, note);
        }
    }
    if (latest != slot.latest && monoEngine (slot.engine))
        restoreLatest (part, true);
}

void SynthEngine::allOff (int part, bool hard) noexcept
{
    auto& slot = parts_[part];
    slot.latest = noKey;
    for (auto& key : slot.keys)
        key = {};
    for (auto& down : slot.sustain)
        down = false;
    if (hard)
    {
        slot.mono = {};
        for (auto& voice : voices_)
            if (voice.active && voice.part == part)
                voice = {};
        return;
    }
    slot.mono.gate = false;
    for (auto& voice : voices_)
        if (voice.active && voice.part == part)
        {
            voice.pendingNote = -1;
            if (voice.gate)
            {
                voice.gate = false;
                voice.amp.startRelease (live_[part].release, sampleRate_);
                voice.filterAmp.startRelease (std::max (0.001, live_[part].filterRelease), sampleRate_);
            }
        }
}

void SynthEngine::handle (int part, std::uint8_t status, std::uint8_t d1, std::uint8_t d2, std::uint8_t articulation) noexcept
{
    const auto type = static_cast<std::uint8_t> (status & 0xf0u);
    const int channel = status & 0x0f;
    NoteArticulation art;
    art.forceAccent = (articulation & articulateAccent) != 0;
    art.forceSlide = (articulation & articulateSlide) != 0;
    if (type == 0x90 && d2 > 0)
        noteOn (part, channel, d1, d2, art);
    else if (type == 0x80 || type == 0x90)
        noteOff (part, channel, d1);
    else if (type == 0xb0 && d1 == 64)
        pedal (part, channel, d2 >= 64);
    else if (type == 0xb0 && d1 == 120)
        allOff (part, true);
    else if (type == 0xb0 && d1 == 123)
        allOff (part, false);
    else if (type == 0xe0)
    {
        const int value = static_cast<int> (d1) | (static_cast<int> (d2) << 7);
        auto& slot = parts_[part];
        slot.bendSemis = (static_cast<float> (value) - 8192.0f) * (2.0f / 8192.0f);
        if (slot.mono.note >= 0)
            slot.mono.targetFrequency = midiHz (static_cast<double> (slot.mono.note) + slot.bendSemis);
    }
}

float SynthEngine::acidFilter (int part, float input, float cutoffHz, float resonance) noexcept
{
    auto& ladder = parts_[part].mono.ladder;
    const float coefficient = std::clamp (
        1.0f - std::exp (-2.0f * pi * cutoffHz / static_cast<float> (sampleRate_)), 0.0f, 0.92f);
    const float feedback = 3.75f * std::clamp (resonance, 0.0f, 1.0f);
    float stage = std::tanh (input - feedback * ladder[3]);
    for (float& pole : ladder)
    {
        pole += coefficient * (std::tanh (stage) - std::tanh (pole));
        pole = flush (std::clamp (pole, -4.0f, 4.0f));
        stage = pole;
    }
    return ladder[3];
}

float SynthEngine::renderMono (int part, const PartParams& params) noexcept
{
    auto& mono = parts_[part].mono;
    if (! mono.active)
        return 0.0f;

    const float slideControl = mono.glideSource == 1 ? std::max (params.slide, mono.slideFloor) : params.slide;
    const float slideSeconds = 0.004f + 0.32f * slideControl * slideControl;
    const float portaSeconds = 0.001f + 0.50f * params.portamento * params.portamento;
    const float glideSeconds = mono.glideSource == 1 ? slideSeconds
                             : mono.glideSource == 2 ? portaSeconds
                                                     : 0.0f;
    if (glideSeconds > 0.0f)
    {
        const double coeff = 1.0 - std::exp (-1.0 / (sampleRate_ * glideSeconds));
        mono.frequency += (mono.targetFrequency - mono.frequency) * coeff;
    }
    else
        mono.frequency = mono.targetFrequency;
    mono.frequency = std::clamp (mono.frequency, 8.0, sampleRate_ * 0.45);

    const float slew = 1.0f - std::exp (-1.0f / static_cast<float> (sampleRate_ * 0.003));
    mono.velocity += slew * (mono.velocityTarget - mono.velocity);
    mono.accent += slew * (mono.accentTarget - mono.accent);

    const float attack = 1.0f - std::exp (-1.0f / static_cast<float> (sampleRate_ * 0.002));
    const float release = 1.0f - std::exp (-1.0f / static_cast<float> (sampleRate_ * 0.06));
    const float target = mono.gate ? 1.0f : 0.0f;
    mono.amp += (mono.gate ? attack : release) * (target - mono.amp);

    float semitones = 0.0f;
    if (parts_[part].engine == EngineKind::psyBass && mono.pitchLeft > 0)
    {
        const double t = 1.0 - static_cast<double> (mono.pitchLeft) / static_cast<double> (mono.pitchTotal);
        semitones = static_cast<float> (12.0 * std::pow (1.0 - t, 2.0));
        --mono.pitchLeft;
    }
    const double frequency = std::clamp (mono.frequency * std::exp2 (semitones / 12.0), 8.0, sampleRate_ * 0.45);
    const double increment = frequency / sampleRate_;

    float sample = 0.0f;
    if (parts_[part].engine == EngineKind::psyBass)
    {
        const float saw = static_cast<float> (mono.osc.next (vstengine::dspcore::BlepOscillator::saw, increment));
        const float sub = static_cast<float> (mono.sub.next (vstengine::dspcore::BlepOscillator::sine, increment * 0.5));
        const float subGain = std::clamp (params.sub, 0.0f, 1.0f);
        sample = (1.0f - subGain) * saw + subGain * sub;
        sample = std::tanh (sample * (1.0f + 4.0f * params.drive));
        const float cutoff = 40.0f * std::pow (180.0f, std::clamp (params.cutoff, 0.0f, 1.0f));
        sample = static_cast<float> (mono.svf.next (sample, cutoff, std::clamp (params.resonance, 0.0f, 1.0f), sampleRate_));
        sample *= 1.0f + 0.45f * mono.accent;
    }
    else
    {
        const float saw = static_cast<float> (mono.osc.next (vstengine::dspcore::BlepOscillator::saw, increment));
        const float square = static_cast<float> (mono.square.next (vstengine::dspcore::BlepOscillator::square, increment));
        const float squareGain = std::clamp (params.square, 0.0f, 1.0f);
        sample = (1.0f - squareGain) * saw + squareGain * square;
        sample = std::tanh (sample * (1.0f + 6.0f * params.drive * params.drive));
        const float envScale = std::clamp (params.env, 0.05f, 1.0f);
        mono.filterEnv *= std::exp (-1.0f / static_cast<float> (sampleRate_ * (0.05 + 0.45 * (1.0f - params.cutoff)) * envScale));
        if (mono.glide && mono.filterEnv < 0.35f)
        {
            const float rise = 1.0f - std::exp (-1.0f / static_cast<float> (sampleRate_ * 0.003));
            mono.filterEnv += rise * (0.35f - mono.filterEnv);
        }
        const float base = 50.0f * std::pow (80.0f, std::clamp (params.cutoff, 0.0f, 1.0f));
        const float cutoff = std::clamp (base * std::exp2 ((2.0f + 4.0f * params.resonance) * mono.filterEnv * (1.0f + mono.accent)),
                                         35.0f, static_cast<float> (sampleRate_ * 0.45));
        sample = acidFilter (part, sample, cutoff, params.resonance);
        sample = std::tanh (sample * (1.0f + params.drive));
    }

    sample *= mono.amp * (0.35f + 0.65f * mono.velocity) * 0.45f;
    if (! std::isfinite (sample))
    {
        mono.svf.reset();
        mono.ladder[0] = mono.ladder[1] = mono.ladder[2] = mono.ladder[3] = 0.0f;
        sample = 0.0f;
    }
    if (! mono.gate && mono.amp < 1.0e-4f)
    {
        mono.active = false;
        mono.amp = 0.0f;
    }
    return sample * std::clamp (params.level, 0.0f, 1.0f);
}

float SynthEngine::renderVa (VaVoice& voice, const PartParams& params, const VoiceMod& mod) noexcept
{
    if (! voice.active)
        return 0.0f;
    if (voice.dying)
    {
        const double frequency = std::min (sampleRate_ * 0.22, midiHz (voice.note));
        const double increment = frequency / sampleRate_;
        const float saw = static_cast<float> (voice.oscA.next (vstengine::dspcore::BlepOscillator::saw, increment));
        const float env = static_cast<float> (voice.amp.next (0.01, 0.2, 0.7, sampleRate_));
        float sample = saw * env * voice.fade * voice.velocity;
        if (voice.fadeTotal > 0)
            voice.fade = static_cast<float> (voice.fadeLeft) / static_cast<float> (voice.fadeTotal);
        if (voice.fadeLeft > 0)
            --voice.fadeLeft;
        if (voice.fadeLeft == 0)
        {
            const int note = voice.pendingNote;
            const float velocity = voice.pendingVelocity;
            const float accent = voice.pendingAccent;
            const int channel = voice.channel;
            const int part = voice.pendingPart;
            const int index = voice.pendingUnison;
            const int copies = voice.pendingCopies;
            const float cents = voice.pendingDetune;
            const float pan = voice.pendingPan;
            voice = {};
            if (note >= 0)
                finishVoice (voice, part, channel, note, velocity, accent, index, std::max (1, copies), cents, pan);
        }
        return sample * std::clamp (params.level, 0.0f, 1.0f) * 0.35f;
    }

    if (! voice.gate && voice.amp.idle())
    {
        voice = {};
        return 0.0f;
    }
    VoiceMod local = mod;
    applyRetrig (voice, params, local);
    const double bend = parts_[voice.part].bendSemis;
    const double note = static_cast<double> (voice.note) + bend + static_cast<double> (local.pitch)
                      + static_cast<double> (voice.detuneCents) / 100.0;
    const double frequency = std::min (sampleRate_ * 0.22, midiHz (note));
    const double increment = frequency / sampleRate_;
    const double detune = std::clamp (params.detuneCents, 0.0, 50.0);
    float carrier = 0.0f;
    if (params.oscLevel[2] > 0.0f || params.fm > 0.0f || local.fm > 0.0f)
    {
        const double incC = increment * std::exp2 (static_cast<double> (params.osc3Semi) / 12.0);
        carrier = shapedOsc (voice.oscC, params.oscWave[2], incC, params.oscPulse[2]);
    }
    double incA = increment;
    const float fm = params.fm + local.fm;
    if (fm > 0.0f)
        incA *= std::exp2 (std::clamp (static_cast<double> (fm) * static_cast<double> (carrier), -2.0, 2.0));
    const float sawA = shapedOsc (voice.oscA, params.oscWave[0], incA, params.oscPulse[0]);
    const float sawB = shapedOsc (voice.oscB, params.oscWave[1], increment * std::exp2 (detune / 1200.0), params.oscPulse[1]);
    const float sub = static_cast<float> (voice.sub.next (vstengine::dspcore::BlepOscillator::sine, increment * 0.5));
    voice.noise = voice.noise * 1664525u + 1013904223u;
    const float noise = (static_cast<float> (voice.noise >> 8) / 16777216.0f) * 2.0f - 1.0f;
    float sample = params.oscLevel[0] * sawA + params.oscLevel[1] * sawB + params.vaSub * sub
                 + std::clamp (params.noise, 0.0f, 1.0f) * noise;
    if (params.oscLevel[2] > 0.0f)
        sample += params.oscLevel[2] * carrier;
    if (params.ring > 0.0f)
        sample += params.ring * sawA * sawB;
    if (params.wtLevel > 0.0f)
    {
        const float table = sharedWavetable().read (params.wtPos, voice.wtPhase, increment);
        voice.wtPhase += increment;
        voice.wtPhase -= std::floor (voice.wtPhase);
        sample += params.wtLevel * table;
    }
    const float env = static_cast<float> (voice.amp.next (params.attack, params.decay, params.sustain, sampleRate_));
    const float filterLevel = static_cast<float> (voice.filterAmp.next (params.filterAttack, params.filterDecay, params.filterSustain, sampleRate_));
    const float cutoffNorm = local.cutoff == 0.0f
                                 ? params.cutoff
                                 : std::clamp (params.cutoff + local.cutoff, 0.0f, 1.0f);
    const float cutoff = 80.0f * std::pow (90.0f, std::clamp (cutoffNorm, 0.0f, 1.0f));
    float scale = 1.0f;
    if (params.keyTrack != 0.0f)
        scale *= std::exp2 (params.keyTrack * (static_cast<float> (voice.note) - 60.0f) / 12.0f);
    if (params.filterEnv != 0.0f)
        scale *= std::exp2 (params.filterEnv * 4.0f * filterLevel);
    const float shapedCutoff = scale == 1.0f
                                   ? cutoff
                                   : std::clamp (cutoff * scale, 20.0f, static_cast<float> (sampleRate_ * 0.45));
    const float resonance = std::clamp (params.resonance + local.resonance, 0.0f, 1.0f);
    if (params.filterType == 0 && params.filter2Mix <= 0.0f)
        sample = static_cast<float> (voice.svf.next (sample, shapedCutoff, resonance, sampleRate_));
    else
    {
        const float dry = sample;
        const float first = static_cast<float> (voice.flt1.next (dry, shapedCutoff, resonance, sampleRate_, params.filterType));
        sample = first;
        if (params.filter2Mix > 0.0f)
        {
            const float secondBaseNorm = local.filter2 == 0.0f
                                              ? params.filter2Cutoff
                                              : std::clamp (params.filter2Cutoff + local.filter2, 0.0f, 1.0f);
            const float secondBase = 80.0f * std::pow (90.0f, std::clamp (secondBaseNorm, 0.0f, 1.0f));
            const float secondCutoff = scale == 1.0f
                                           ? secondBase
                                           : std::clamp (secondBase * scale, 20.0f, static_cast<float> (sampleRate_ * 0.45));
            const float second = static_cast<float> (voice.flt2.next (
                params.filterRoute == 0 ? first : dry, secondCutoff,
                std::clamp (params.filter2Resonance, 0.0f, 1.0f), sampleRate_, params.filter2Type));
            const float mix = std::clamp (params.filter2Mix, 0.0f, 1.0f);
            sample = (1.0f - mix) * first + mix * second;
        }
    }
    sample *= env * voice.velocity * voice.accentGain * 0.4f * std::clamp (params.level, 0.0f, 1.0f);
    if (! std::isfinite (sample))
    {
        voice.svf.reset();
        voice.flt1.reset();
        voice.flt2.reset();
        return 0.0f;
    }
    return sample;
}

void SynthEngine::updateMod (int part, const BlockParams& params) noexcept
{
    bpmLive_ = params.bpmValid && params.bpm > 1.0 ? params.bpm : 120.0;
    auto& mod = mod_[part];
    mod = {};
    const auto& knobs = params.part[part];
    float source[13] {};
    for (int index = 0; index < 4; ++index)
        source[index + 1] = advanceLfo (lfo_[part][index], lfoHz (knobs, index), knobs.lfoShape[index]);
    for (int index = 0; index < 8; ++index)
        source[index + 5] = std::clamp (params.macro[index], 0.0f, 1.0f) * 2.0f - 1.0f;
    for (int index = 0; index < PartParams::routeSlots; ++index)
    {
        const int src = knobs.routeSource[index];
        const int dst = knobs.routeDest[index];
        const float amount = knobs.routeAmount[index];
        if (src <= 0 || src > 12 || dst <= 0 || amount == 0.0f)
            continue;
        if (src <= 4 && knobs.lfoRetrig[src - 1])
            continue;
        const float delta = amount * source[src];
        switch (dst)
        {
            case 1: mod.cutoff += delta; break;
            case 2: mod.resonance += delta; break;
            case 3: mod.pitch += delta * 12.0f; break;
            case 4: mod.fm += delta; break;
            case 5: mod.filter2 += delta; break;
            default: break;
        }
    }
    lastCutoff_[part] = mod.cutoff;
}

double SynthEngine::lfoHz (const PartParams& params, int index) const noexcept
{
    if (! params.lfoSync[index])
        return std::clamp (static_cast<double> (params.lfoRate[index]), 0.0, 40.0);
    static constexpr double beats[] = { 4.0, 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    const int div = std::clamp (params.lfoDiv[index], 0, 8);
    return (bpmLive_ / 60.0) / beats[div];
}

float SynthEngine::advanceLfo (LfoState& lfo, double hz, int shape) noexcept
{
    const double step = std::max (0.0, hz) / sampleRate_;
    lfo.phase += step;
    bool wrapped = false;
    if (lfo.phase >= 1.0)
    {
        lfo.phase -= std::floor (lfo.phase);
        wrapped = true;
    }
    if (wrapped)
    {
        lfo.noise = lfo.noise * 1664525u + 1013904223u;
        lfo.held = (static_cast<float> (lfo.noise >> 8) / 16777216.0f) * 2.0f - 1.0f;
        lfo.target = lfo.held;
    }
    lfo.smooth += 0.01f * (lfo.target - lfo.smooth);
    const double phase = lfo.phase;
    switch (std::clamp (shape, 0, 6))
    {
        case 1: return static_cast<float> (4.0 * std::fabs (phase - 0.5) - 1.0);
        case 2: return static_cast<float> (2.0 * phase - 1.0);
        case 3: return static_cast<float> (1.0 - 2.0 * phase);
        case 4: return phase < 0.5 ? 1.0f : -1.0f;
        case 5: return lfo.held;
        case 6: return lfo.smooth;
        default: return static_cast<float> (std::sin (2.0 * pi * phase));
    }
}

void SynthEngine::applyRetrig (VaVoice& voice, const PartParams& params, VoiceMod& mod) noexcept
{
    float value[4] {};
    for (int index = 0; index < 4; ++index)
    {
        if (! params.lfoRetrig[index])
            continue;
        value[index] = advanceLfo (voice.retrig[index], lfoHz (params, index), params.lfoShape[index]);
    }
    for (int index = 0; index < PartParams::routeSlots; ++index)
    {
        const int src = params.routeSource[index];
        const int dst = params.routeDest[index];
        const float amount = params.routeAmount[index];
        if (src < 1 || src > 4 || dst <= 0 || amount == 0.0f || ! params.lfoRetrig[src - 1])
            continue;
        const float delta = amount * value[src - 1];
        switch (dst)
        {
            case 1: mod.cutoff += delta; break;
            case 2: mod.resonance += delta; break;
            case 3: mod.pitch += delta * 12.0f; break;
            case 4: mod.fm += delta; break;
            case 5: mod.filter2 += delta; break;
            default: break;
        }
    }
}

void SynthEngine::process (float* left, float* right, int frames,
                           const Event* events, int eventCount,
                           const BlockParams& params) noexcept
{
    if (left == nullptr || right == nullptr || frames <= 0)
        return;
    if (! prepared_ || frames > maximumBlockSize_)
    {
        std::fill (left, left + frames, 0.0f);
        std::fill (right, right + frames, 0.0f);
        return;
    }

    for (int part = 0; part < partCount; ++part)
    {
        live_[part] = params.part[part];
        latchEngine (part, params.part[part].engine);
    }

    int eventIndex = 0;
    const float output = std::clamp (params.output, 0.0f, 1.0f);
    for (int sample = 0; sample < frames; ++sample)
    {
        while (eventIndex < eventCount && events[eventIndex].offset <= static_cast<std::uint32_t> (sample))
        {
            const auto& event = events[eventIndex++];
            if (event.offset != static_cast<std::uint32_t> (sample) || event.status == 0)
                continue;
            for (int part = 0; part < partCount; ++part)
                handle (part, event.status, event.data1, event.data2, event.articulation);
        }
        float mixL = 0.0f;
        float mixR = 0.0f;
        for (int part = 0; part < partCount; ++part)
            updateMod (part, params);
        for (int part = 0; part < partCount; ++part)
        {
            if (monoEngine (parts_[part].engine))
            {
                const float monoSample = renderMono (part, params.part[part]);
                mixL += monoSample;
                mixR += monoSample;
            }
            else
                for (auto& voice : voices_)
                    if (voice.active && voice.part == part)
                    {
                        const float voiceSample = renderVa (voice, params.part[part], mod_[part]);
                        const float pan = std::clamp (voice.pan, -1.0f, 1.0f);
                        mixL += voiceSample * (pan > 0.0f ? 1.0f - pan : 1.0f);
                        mixR += voiceSample * (pan < 0.0f ? 1.0f + pan : 1.0f);
                    }
        }
        mixL = std::tanh (mixL) * output;
        mixR = std::tanh (mixR) * output;
        if (! std::isfinite (mixL))
            mixL = 0.0f;
        if (! std::isfinite (mixR))
            mixR = 0.0f;
        left[sample] = mixL;
        right[sample] = mixR;
    }
}

} // namespace vox::syn::engine
