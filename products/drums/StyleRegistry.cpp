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
    { 0, "dcadd0b1-5bfc-45d1-8154-e152b32ebe03", "full-on", "Full-on / Psy", "Psy", 140, 148 },
    { 1, "07de80e8-7de8-42d5-9318-53a2cb88cef8", "goa", "Goa", "Psy", 138, 148 },
    { 2, "bc13d2b9-ce97-4aaf-84cf-1bf84a409fe6", "house", "House", "House & Techno", 120, 126 },
    { 3, "b9a2e70e-6f37-4dc2-ba52-ce5f61fc770b", "techno", "Techno", "House & Techno", 125, 135 },
    { 4, "93565ca6-5d7d-4cda-9fba-5a434ebd560b", "trap", "Trap", "Bass & Breaks", 130, 150 },
    { 5, "5e8c088c-e6ea-4038-8994-8be735c4e1a8", "breaks", "Breaks", "Bass & Breaks", 85, 130 },
    { 6, "8939c28f-742c-41e1-aef9-e5de8d6d6ca6", "ambient", "Ambient", "Ambient", 60, 100 },
    { 7, "38e16490-1704-4476-8ff1-5dae06109562", "uplifting-trance", "Uplifting Trance", "Trance", 136, 140 },
    { 8, "cf4f04dc-8002-41f7-8593-128ed7f8ad31", "anthem-trance", "Anthem Trance", "Trance", 138, 140 },
    { 9, "ba3859d9-a9bb-4d2a-acfe-af7f117398a8", "dark-psy", "Dark Psy", "Psy", 145, 150 },
    { 10, "3d5eecb1-dc83-448a-af83-a05251d6783d", "melodic-trance", "Melodic Trance", "Trance", 132, 138 },
    { 11, "62362a3e-4589-4061-964b-20008ff39879", "hard-rock", "Hard Rock", "Rock & Metal", 110, 140 },
    { 12, "ad7deadd-26fd-4945-bdd3-b8a9c12254a9", "heavy-metal", "Heavy Metal", "Rock & Metal", 120, 180 },
    { 13, "cc3a3846-08a1-466e-b61c-c4f8ffae7c4b", "symphonic-metal", "Symphonic Metal", "Rock & Metal", 100, 180 },
    { 14, "2d31cde4-1b5d-4b06-91b8-71c932d94bd9", "nordic-metal", "Nordic Metal", "Rock & Metal", 90, 140 },
    { 15, "2affaf5a-2ca7-484c-b3f1-af939403cac1", "tech-house", "Tech House", "House & Techno", 124, 128 },
    { 16, "1f1ef7bb-769a-4d19-b658-c9775ddd405e", "drum-and-bass", "Drum & Bass", "Bass & Breaks", 170, 176 },
    { 17, "dac0f9f7-9e2b-4be9-ad46-1a555e7858d4", "melodic-drum-and-bass", "Melodic Drum & Bass", "Bass & Breaks", 170, 176 },
    { 18, "3f65cadd-77b9-406c-80bf-ad8f4f9670f7", "melodic-techno", "Melodic Techno", "House & Techno", 120, 126 },
    { 19, "7146e97c-118a-455c-885e-5d04cec72ef6", "progressive-trance", "Progressive Trance", "Trance", 128, 134 },
    { 20, "7f62e2ca-3d02-461d-9905-f8900511cda6", "dream-trance", "Dream Trance", "Trance", 136, 140 },
    { 21, "0439e7b8-7fbb-414a-9dfc-8341a1657a88", "hard-trance", "Hard Trance", "Trance", 145, 155 },
    { 22, "bbc77dde-f332-4ee2-86de-a5c278867da0", "symphonic-trance", "Symphonic Trance", "Trance", 136, 140 },
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
            if (info == nullptr || info->id != i || d.id != info->uuid)
            {
                if (p.error.isEmpty())
                    p.error = "factory style " + juce::String (i) + ": key '" + d.key + "' / id " + d.id + " is not registry entry " + juce::String (i);
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

const StyleInfo* infoByUuid (const juce::String& uuid) noexcept
{
    for (const auto& s : table)
        if (uuid == s.uuid)
            return &s;
    return nullptr;
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
