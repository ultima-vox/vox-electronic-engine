#include "StyleRegistry.h"

#include <mutex>

namespace vox::drums::style {

namespace {

// The definitions, in the order of VOX_DRUMS_FACTORY_STYLES (CMake); their
// keys must equal the registry keys of ids 0..6.
const char* const factoryText[] = {
#include "FactoryStyles.inc"
};
constexpr int factoryCount = static_cast<int> (sizeof (factoryText) / sizeof (factoryText[0]));

constexpr std::array<StyleInfo, registrySize> table { {
    { 0, "full-on", "Full-on / Psy", "Psy", 140, 148 },
    { 1, "goa", "Goa", "Psy", 138, 148 },
    { 2, "house", "House", "House & Techno", 120, 126 },
    { 3, "techno", "Techno", "House & Techno", 125, 135 },
    { 4, "trap", "Trap", "Bass & Breaks", 130, 150 },
    { 5, "breaks", "Breaks", "Bass & Breaks", 85, 130 },
    { 6, "ambient", "Ambient", "Ambient", 60, 100 },
    { 7, "uplifting-trance", "Uplifting Trance", "Trance", 136, 140 },
    { 8, "anthem-trance", "Anthem Trance", "Trance", 138, 140 },
    { 9, "dark-psy", "Dark Psy", "Psy", 145, 150 },
    { 10, "melodic-trance", "Melodic Trance", "Trance", 132, 138 },
    { 11, "hard-rock", "Hard Rock", "Rock & Metal", 110, 140 },
    { 12, "heavy-metal", "Heavy Metal", "Rock & Metal", 120, 180 },
    { 13, "symphonic-metal", "Symphonic Metal", "Rock & Metal", 100, 180 },
    { 14, "nordic-metal", "Nordic Metal", "Rock & Metal", 90, 140 },
    { 15, "tech-house", "Tech House", "House & Techno", 124, 128 },
    { 16, "drum-and-bass", "Drum & Bass", "Bass & Breaks", 170, 176 },
    { 17, "melodic-drum-and-bass", "Melodic Drum & Bass", "Bass & Breaks", 170, 176 },
    { 18, "melodic-techno", "Melodic Techno", "House & Techno", 120, 126 },
    { 19, "progressive-trance", "Progressive Trance", "Trance", 128, 134 },
    { 20, "dream-trance", "Dream Trance", "Trance", 136, 140 },
    { 21, "hard-trance", "Hard Trance", "Trance", 145, 155 },
    { 22, "symphonic-trance", "Symphonic Trance", "Trance", 136, 140 },
} };

struct Parsed {
    std::array<std::optional<Definition>, registrySize> definitions;
    juce::String error;
};

const Parsed& parsed()
{
    static Parsed p;
    static std::once_flag once;
    std::call_once (once, [] {
        for (int i = 0; i < factoryCount; ++i)
        {
            Definition d;
            const auto result = parse (juce::String::fromUTF8 (factoryText[i]), d);
            if (result.failed())
            {
                if (p.error.isEmpty())
                    p.error = "factory style " + juce::String (i) + ": " + result.getErrorMessage();
                continue;
            }
            const auto* info = infoByKey (d.key);
            if (info == nullptr || info->id != i)
            {
                if (p.error.isEmpty())
                    p.error = "factory style " + juce::String (i) + ": key '" + d.key + "' is not registry id " + juce::String (i);
                continue;
            }
            p.definitions[static_cast<std::size_t> (i)] = std::move (d);
        }
    });
    return p;
}

} // namespace

const std::array<StyleInfo, registrySize>& registry() noexcept
{
    return table;
}

const StyleInfo* infoById (int id) noexcept
{
    return id >= 0 && id < registrySize ? &table[static_cast<std::size_t> (id)] : nullptr;
}

const StyleInfo* infoByKey (const juce::String& key) noexcept
{
    for (const auto& s : table)
        if (key == s.key)
            return &s;
    return nullptr;
}

const Definition* factoryDefinition (int id)
{
    const auto& p = parsed();
    if (id < 0 || id >= registrySize || ! p.definitions[static_cast<std::size_t> (id)])
        return nullptr;
    return &*p.definitions[static_cast<std::size_t> (id)];
}

bool available (int id)
{
    return factoryDefinition (id) != nullptr;
}

juce::String factoryParseError()
{
    return parsed().error;
}

} // namespace vox::drums::style
