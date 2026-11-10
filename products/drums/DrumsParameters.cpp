#include "DrumsParameters.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace vox::drums {
namespace {

juce::String formatSeconds (float v)
{
    return v < 1.0f ? juce::String (v * 1000.0f, v < 0.1f ? 1 : 0) + " ms" : juce::String (v, 2) + " s";
}

float parseSeconds (const juce::String& t)
{
    const float n = t.retainCharacters ("+-.0123456789").getFloatValue();
    return t.trim().endsWithIgnoreCase ("ms") ? n / 1000.0f : n;
}

juce::NormalisableRange<float> logRange (float lo, float hi)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (std::sqrt (lo * hi));
    return r;
}

} // namespace

const kit::KitSettings& defaultKit()
{
    static const kit::KitSettings k = kit::legacyKit();
    return k;
}

juce::String modelName (kit::Model model)
{
    static const char* names[kit::modelCount] = { "Kick", "Snare", "Clap", "Hat", "Tom", "Rim", "Cymbal", "Perc" };
    const int i = static_cast<int> (model);
    return i >= 0 && i < kit::modelCount ? names[i] : "?";
}

juce::String padName (int pad)
{
    static const char* legacy[8] = { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Tom", "Rim", "Cymbal" };
    if (pad >= 0 && pad < 8)
        return legacy[pad];
    return "Perc " + juce::String (pad - 7);
}

std::string padPrefix (int pad)
{
    std::ostringstream s;
    s << "drm.p" << std::setw (2) << std::setfill ('0') << (pad + 1);
    return s.str();
}

const std::vector<PluginParameter>& pluginParameters()
{
    static const std::vector<PluginParameter> table = [] {
        std::vector<PluginParameter> t;
        for (int pad = 0; pad < padCount; ++pad)
        {
            const auto prefix = padPrefix (pad);
            const auto label = "Pad " + std::to_string (pad + 1) + " ";
            t.push_back ({ prefix + ".level", label + "Level", Field::level, pad });
            t.push_back ({ prefix + ".tune", label + "Tune", Field::tune, pad });
            t.push_back ({ prefix + ".decay", label + "Decay", Field::decay, pad });
            t.push_back ({ prefix + ".pan", label + "Pan", Field::pan, pad });
            t.push_back ({ prefix + ".cutoff", label + "Cutoff", Field::cutoff, pad });
            t.push_back ({ prefix + ".mute", label + "Mute", Field::mute, pad });
        }
        t.push_back ({ "drm.kit.volume", "Kit Volume", Field::kitVolume });
        t.push_back ({ "drm.kit.velocity", "Velocity Sens", Field::kitVelocity });
        t.push_back ({ "drm.kit.hat_choke", "Choke Groups", Field::kitChoke });
        t.push_back ({ "drm.seq.mode", "Seq Mode", Field::seqMode });
        t.push_back ({ patternParameterId.toStdString(), "Pattern", Field::seqPattern });
        t.push_back ({ "drm.seq.swing", "Seq Swing", Field::seqSwing });
        t.push_back ({ "drm.seq.accent", "Seq Accent", Field::seqAccent });
        for (int pad = 0; pad < padCount; ++pad)
            t.push_back ({ "drm.seq.lane." + padPrefix (pad).substr (4), "Lane " + std::to_string (pad + 1), Field::seqLane, pad });
        return t;
    }();
    return table;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto& kit = defaultKit();
    const auto floatParam = [&] (const PluginParameter& e, juce::NormalisableRange<float> range, float def,
                                 std::function<juce::String (float, int)> toText,
                                 std::function<float (const juce::String&)> fromText) {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { e.id, 1 }, e.name, range, def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (std::move (toText)).withValueFromStringFunction (std::move (fromText))));
    };
    const auto percent = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
    const auto fromPercent = [] (const juce::String& t) { return t.retainCharacters ("+-.0123456789").getFloatValue() / 100.0f; };
    for (const auto& e : pluginParameters())
    {
        const auto* pad = e.pad >= 0 ? &kit.pads[static_cast<std::size_t> (e.pad)] : nullptr;
        switch (e.field)
        {
            case Field::level:
                floatParam (e, { 0.0f, 1.0f }, pad->level, percent, fromPercent);
                break;
            case Field::tune:
                floatParam (e, { -24.0f, 24.0f }, pad->tune,
                            [] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " st"; },
                            [] (const juce::String& t) { return t.retainCharacters ("+-.0123456789").getFloatValue(); });
                break;
            case Field::decay:
                floatParam (e, logRange (0.01f, 10.0f), pad->decay, [] (float v, int) { return formatSeconds (v); }, parseSeconds);
                break;
            case Field::pan:
                floatParam (e, { -1.0f, 1.0f }, pad->pan,
                            [] (float v, int) {
                                const int a = juce::roundToInt (std::abs (v) * 100.0f);
                                return a == 0 ? juce::String ("C") : (v < 0.0f ? "L " : "R ") + juce::String (a);
                            },
                            [] (const juce::String& t) {
                                const float n = t.retainCharacters (".0123456789").getFloatValue() / 100.0f;
                                return t.trim().startsWithIgnoreCase ("L") ? -n : n;
                            });
                break;
            case Field::cutoff:
                floatParam (e, logRange (20.0f, 20000.0f), pad->cutoff,
                            [] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; },
                            [] (const juce::String& t) {
                                const float n = t.retainCharacters ("+-.0123456789").getFloatValue();
                                return t.containsIgnoreCase ("k") ? n * 1000.0f : n;
                            });
                break;
            case Field::mute:
                layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { e.id, 1 }, e.name, false));
                break;
            case Field::kitVolume:
                floatParam (e, { 0.0f, 1.0f }, kit.volume, percent, fromPercent);
                break;
            case Field::kitVelocity:
                floatParam (e, { 0.0f, 1.0f }, kit.velocitySensitivity, percent, fromPercent);
                break;
            case Field::kitChoke:
                layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { e.id, 1 }, e.name, kit.chokeEnabled));
                break;
            case Field::seqMode:
                layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { e.id, 1 }, e.name,
                                                                          juce::StringArray { "Host MIDI", "Internal", "Both" }, 0));
                break;
            case Field::seqPattern:
            {
                juce::StringArray names;
                for (int i = 0; i < kit::factoryPatternCount; ++i)
                    names.add (kit::factoryPatternName (i));
                layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { e.id, 1 }, e.name, names, 0));
                break;
            }
            case Field::seqSwing:
                floatParam (e, { 0.0f, 0.5f }, kit.swing, percent, fromPercent);
                break;
            case Field::seqAccent:
                floatParam (e, { 0.0f, 1.0f }, kit.accent, percent, fromPercent);
                break;
            case Field::seqLane:
                layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { e.id, 1 }, e.name, true));
                break;
        }
    }
    return layout;
}

std::string describeLayout (juce::AudioProcessorValueTreeState& state)
{
    std::ostringstream out;
    out << "id,name,type,minimum,maximum,default,automatable,choices\n";
    out << std::setprecision (9);
    for (const auto& entry : pluginParameters())
    {
        auto* parameter = state.getParameter (entry.id);
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        const char* type = dynamic_cast<juce::AudioParameterBool*> (parameter) != nullptr ? "toggle"
                         : dynamic_cast<juce::AudioParameterChoice*> (parameter) != nullptr ? "choice"
                                                                                           : "float";
        std::string choices;
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter))
            choices = choice->choices.joinIntoString ("|").toStdString();
        // The default as declared (the default kit's value), not its round trip
        // through a skewed 0..1 range, whose last digit can differ between C
        // runtimes.
        float def = 0.0f;
        if (dynamic_cast<juce::AudioParameterFloat*> (parameter) != nullptr)
        {
            const auto& kit = defaultKit();
            const auto* pad = entry.pad >= 0 ? &kit.pads[static_cast<std::size_t> (entry.pad)] : nullptr;
            switch (entry.field)
            {
                case Field::level: def = pad->level; break;
                case Field::tune: def = pad->tune; break;
                case Field::decay: def = pad->decay; break;
                case Field::pan: def = pad->pan; break;
                case Field::cutoff: def = pad->cutoff; break;
                case Field::kitVolume: def = kit.volume; break;
                case Field::kitVelocity: def = kit.velocitySensitivity; break;
                case Field::seqSwing: def = kit.swing; break;
                case Field::seqAccent: def = kit.accent; break;
                case Field::mute:
                case Field::kitChoke:
                case Field::seqMode:
                case Field::seqPattern:
                case Field::seqLane:
                    def = ranged->convertFrom0to1 (ranged->getDefaultValue());
                    break;
            }
        }
        else
            def = ranged->convertFrom0to1 (ranged->getDefaultValue());
        const auto& range = ranged->getNormalisableRange();
        out << entry.id << ",\"" << parameter->getName (128) << "\"," << type << ',' << range.start << ',' << range.end << ','
            << def << ',' << (parameter->isAutomatable() ? 1 : 0) << ",\"" << choices << "\"\n";
    }
    return out.str();
}

} // namespace vox::drums
