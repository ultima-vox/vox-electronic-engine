#include "DrumsParameters.h"

#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace vox::drums {
namespace {

constexpr std::array<const char*, padCount> legacyPadKeys { "kick", "snare", "clap", "chat",
                                                            "ohat", "tom", "rim", "cymbal" };

// Engine features that arrive in later Drums PRs; not host parameters yet.
bool deferred (const std::string& name)
{
    return name == "source" || name == "output" || name == "send1" || name == "send2"
        || name.rfind ("sample_", 0) == 0;
}

int padIndex (const std::string& key)
{
    for (int i = 0; i < padCount; ++i)
        if (key == legacyPadKeys[static_cast<std::size_t> (i)])
            return i;
    return -1;
}

juce::NormalisableRange<float> rangeFor (const EngineParameter& p)
{
    juce::NormalisableRange<float> range (p.minimum, p.maximum);
    // Frequencies and times are heard logarithmically: put the geometric
    // mean at the knob centre.
    if ((p.unit == "Hz" || p.unit == "s") && p.minimum > 0.0f)
        range.setSkewForCentre (std::sqrt (p.minimum * p.maximum));
    return range;
}

// Host and editor value text: units a musician reads, not raw floats.
juce::String formatValue (const EngineParameter& p, float v)
{
    const auto& u = p.unit;
    const juce::String id (p.legacyId);
    if (u == "Hz")
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    if (u == "s")
        return v < 1.0f ? juce::String (v * 1000.0f, v < 0.1f ? 1 : 0) + " ms" : juce::String (v, 2) + " s";
    if (u == "st")
        return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " st";
    if (u == "ct")
        return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct";
    if (id.endsWith (".pan"))
    {
        const int amount = juce::roundToInt (std::abs (v) * 100.0f);
        return amount == 0 ? juce::String ("C") : (v < 0.0f ? "L " : "R ") + juce::String (amount);
    }
    if (id.endsWith (".drive"))
        return "x" + juce::String (v, 2);
    if (p.minimum < 0.0f)
        return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %";
    return juce::String (juce::roundToInt (v * 100.0f / juce::jmax (1.0e-6f, p.maximum))) + " %";
}

// Inverse for typed values: the leading number in the unit formatValue uses.
float parseValue (const EngineParameter& p, const juce::String& text)
{
    const auto t = text.trim();
    const float number = t.retainCharacters ("+-.0123456789").getFloatValue();
    if (p.unit == "Hz")
        return t.containsIgnoreCase ("k") ? number * 1000.0f : number;
    if (p.unit == "s")
        return t.endsWithIgnoreCase ("ms") ? number / 1000.0f : number;
    if (p.unit == "st" || p.unit == "ct" || juce::String (p.legacyId).endsWith (".drive"))
        return number;
    if (juce::String (p.legacyId).endsWith (".pan"))
        return t.startsWithIgnoreCase ("L") ? -number / 100.0f : number / 100.0f;
    if (p.minimum < 0.0f)
        return number / 100.0f;
    return number / 100.0f * p.maximum;
}

} // namespace

std::string padPrefix (int pad)
{
    std::ostringstream s;
    s << "drm.p" << std::setw (2) << std::setfill ('0') << (pad + 1);
    return s.str();
}

std::string pluginIdFor (const std::string& legacyId)
{
    const auto dot = legacyId.find ('.');
    if (dot == std::string::npos)
        return {};
    const auto scope = legacyId.substr (0, dot);
    const auto name = legacyId.substr (dot + 1);
    if (scope == "kit")
        return "drm.kit." + name;
    if (scope == "seq")
    {
        if (name.rfind ("lane.", 0) == 0)
        {
            const int pad = padIndex (name.substr (5));
            return pad < 0 ? std::string {} : "drm.seq.lane." + padPrefix (pad).substr (4);
        }
        return "drm.seq." + name;
    }
    const int pad = padIndex (scope);
    if (pad < 0 || deferred (name))
        return {};
    return padPrefix (pad) + "." + name;
}

const std::vector<PluginParameter>& pluginParameters()
{
    static const std::vector<PluginParameter> table = [] {
        std::vector<PluginParameter> result;
        for (const auto& p : DrumEngine::parameters())
        {
            auto id = pluginIdFor (p.legacyId);
            if (! id.empty())
                result.push_back ({ std::move (id), static_cast<int> (p.index), &p });
        }
        result.push_back ({ patternParameterId.toStdString(), -1, nullptr });
        return result;
    }();
    return table;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& entry : pluginParameters())
    {
        const juce::ParameterID id { entry.id, 1 };
        if (entry.engine == nullptr)
        {
            juce::StringArray names;
            for (const auto& name : DrumEngine::factoryPatternNames())
                names.add (name);
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                id, "Pattern", names, 0, juce::AudioParameterChoiceAttributes().withLabel ("")));
            continue;
        }
        const auto& p = *entry.engine;
        switch (p.type)
        {
            case EngineParameter::Type::toggle:
                layout.add (std::make_unique<juce::AudioParameterBool> (
                    id, p.name, p.defaultValue >= 0.5f,
                    juce::AudioParameterBoolAttributes().withAutomatable (p.automatable)));
                break;
            case EngineParameter::Type::choice:
            {
                juce::StringArray names;
                for (const auto& c : p.choices)
                    names.add (c);
                layout.add (std::make_unique<juce::AudioParameterChoice> (
                    id, p.name, names, static_cast<int> (std::lround (p.defaultValue)),
                    juce::AudioParameterChoiceAttributes().withAutomatable (p.automatable)));
                break;
            }
            case EngineParameter::Type::continuous:
            {
                const auto* engine = &p;
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    id, p.name, rangeFor (p), p.defaultValue,
                    juce::AudioParameterFloatAttributes()
                        .withAutomatable (p.automatable)
                        .withStringFromValueFunction ([engine] (float v, int) { return formatValue (*engine, v); })
                        .withValueFromStringFunction ([engine] (const juce::String& t) { return parseValue (*engine, t); })));
            }
                break;
        }
    }
    return layout;
}

std::string describeLayout (juce::AudioProcessorValueTreeState& state)
{
    std::ostringstream out;
    out << "id,legacy_id,name,type,minimum,maximum,default,automatable,choices\n";
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
        const auto& range = ranged->getNormalisableRange();
        out << entry.id << ',' << (entry.engine != nullptr ? entry.engine->legacyId : "") << ",\""
            << parameter->getName (128) << "\"," << type << ',' << range.start << ',' << range.end << ','
            // The engine's default, not its round trip through a skewed 0..1
            // range (that last digit can differ between C runtimes).
            << (entry.engine != nullptr ? entry.engine->defaultValue : 0.0f) << ','
            << (parameter->isAutomatable() ? 1 : 0) << ",\"" << choices << "\"\n";
    }
    return out.str();
}

} // namespace vox::drums
