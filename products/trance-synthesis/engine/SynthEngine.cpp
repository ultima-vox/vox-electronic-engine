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

} // namespace

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

void SynthEngine::startVa (int part, int channel, int note, float velocity, NoteArticulation art) noexcept
{
    if (parts_[part].engine != EngineKind::va)
        return;
    for (auto& voice : voices_)
    {
        if (voice.active && ! voice.dying && voice.part == part && voice.note == note && voice.channel == channel)
        {
            voice.velocity = velocity;
            voice.accentGain = art.forceAccent ? 1.35f : 1.0f;
            voice.amp.gate();
            voice.gate = true;
            voice.age = ++age_;
            return;
        }
    }
    VaVoice* slot = nullptr;
    if (used() < maxVoices)
    {
        for (auto& voice : voices_)
            if (! voice.active)
            {
                slot = &voice;
                break;
            }
    }
    if (slot == nullptr)
    {
        slot = oldestVa();
        if (slot == nullptr)
            return;
        slot->dying = true;
        slot->pendingNote = note;
        slot->pendingVelocity = velocity;
        slot->pendingAccent = art.forceAccent ? 1.35f : 1.0f;
        slot->pendingPart = part;
        slot->fadeTotal = std::max (1, static_cast<int> (sampleRate_ * (fadeMs / 1000.0)));
        slot->fadeLeft = slot->fadeTotal;
        slot->fade = 1.0f;
        slot->channel = channel;
        return;
    }
    *slot = {};
    slot->active = true;
    slot->gate = true;
    slot->part = part;
    slot->channel = channel;
    slot->note = note;
    slot->velocity = velocity;
    slot->accentGain = art.forceAccent ? 1.35f : 1.0f;
    slot->age = ++age_;
    slot->noise = 0x9e3779b9u ^ (static_cast<std::uint32_t> (note) * 0x85ebca6bu) ^ static_cast<std::uint32_t> (slot->age);
    slot->fade = 1.0f;
    slot->amp.gate();
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

float SynthEngine::renderVa (VaVoice& voice, const PartParams& params) noexcept
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
            const int channel = voice.channel;
            const int part = voice.pendingPart;
            voice = {};
            if (note >= 0)
            {
                voice.active = true;
                voice.gate = true;
                voice.part = part;
                voice.channel = channel;
                voice.note = note;
                voice.velocity = velocity;
                voice.accentGain = voice.pendingAccent;
                voice.age = ++age_;
                voice.noise = 0x9e3779b9u ^ static_cast<std::uint32_t> (note * 17);
                voice.amp.gate();
            }
        }
        return sample * std::clamp (params.level, 0.0f, 1.0f) * 0.35f;
    }

    if (! voice.gate && voice.amp.idle())
    {
        voice = {};
        return 0.0f;
    }
    const double bend = parts_[voice.part].bendSemis;
    const double frequency = std::min (sampleRate_ * 0.22, midiHz (static_cast<double> (voice.note) + bend));
    const double increment = frequency / sampleRate_;
    const double detune = std::clamp (params.detuneCents, 0.0, 50.0);
    const float sawA = static_cast<float> (voice.oscA.next (vstengine::dspcore::BlepOscillator::saw, increment));
    const float sawB = static_cast<float> (voice.oscB.next (vstengine::dspcore::BlepOscillator::saw,
                                                            increment * std::exp2 (detune / 1200.0)));
    const float sub = static_cast<float> (voice.sub.next (vstengine::dspcore::BlepOscillator::sine, increment * 0.5));
    voice.noise = voice.noise * 1664525u + 1013904223u;
    const float noise = (static_cast<float> (voice.noise >> 8) / 16777216.0f) * 2.0f - 1.0f;
    float sample = 0.62f * sawA + 0.30f * sawB + 0.16f * sub + std::clamp (params.noise, 0.0f, 1.0f) * noise;
    const float env = static_cast<float> (voice.amp.next (params.attack, params.decay, params.sustain, sampleRate_));
    const float cutoff = 80.0f * std::pow (90.0f, std::clamp (params.cutoff, 0.0f, 1.0f));
    sample = static_cast<float> (voice.svf.next (sample, cutoff, std::clamp (params.resonance, 0.0f, 1.0f), sampleRate_));
    sample *= env * voice.velocity * voice.accentGain * 0.4f * std::clamp (params.level, 0.0f, 1.0f);
    if (! std::isfinite (sample))
    {
        voice.svf.reset();
        return 0.0f;
    }
    return sample;
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
        float mix = 0.0f;
        for (int part = 0; part < partCount; ++part)
        {
            if (monoEngine (parts_[part].engine))
                mix += renderMono (part, params.part[part]);
            else
                for (auto& voice : voices_)
                    if (voice.active && voice.part == part)
                        mix += renderVa (voice, params.part[part]);
        }
        mix = std::tanh (mix) * output;
        if (! std::isfinite (mix))
            mix = 0.0f;
        left[sample] = mix;
        right[sample] = mix;
    }
}

} // namespace vox::syn::engine
