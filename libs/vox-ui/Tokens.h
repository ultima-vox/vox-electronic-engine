#pragma once

#include <juce_graphics/juce_graphics.h>

namespace vox::ui::tokens {

// VOX MUSIC design language — Gate-1 accepted palette (owner review 2026-11-18,
// comment 6980638479) as extracted from the frozen reference
// apps/vox-music-prototype/src/design-system/system.css at PR #160
// e420d2c0fa95cebf4ec0ff0c4b359d9d35c28d93 (::root dark theme).
namespace colour {
inline const auto background = juce::Colour (0xff101218);
inline const auto panel = juce::Colour (0xff181b22);
inline const auto panelRaised = juce::Colour (0xff22262f);
inline const auto control = juce::Colour (0xff11141a); // well
inline const auto wellGroup = juce::Colour (0xff151820); // grouped dial wells (fx.css)
inline const auto graph = juce::Colour (0xff11141a);

inline const auto border = juce::Colour (0xff313640);
inline const auto borderSubtle = juce::Colour (0xff292e37);

inline const auto accent = juce::Colour (0xfff3bd72);        // default product accent
inline const auto accentHover = juce::Colour (0xfff8d194);
inline const auto accentDim = juce::Colour (0xff7a5f36);

inline const auto text = juce::Colour (0xffeef0f3);
inline const auto textSecondary = juce::Colour (0xffadb4c0);
inline const auto textMuted = juce::Colour (0xff929aa9);

inline const auto danger = juce::Colour (0xfffb9995);
inline const auto success = juce::Colour (0xff89d4aa);
inline const auto warning = juce::Colour (0xfff3bd72);

// Product accents ([data-theme] variants of the same language).
namespace fx {
inline const auto accent = juce::Colour (0xff7ce2d0);   // VOX FX
inline const auto accentLine = juce::Colour (0xff517e7c);
inline const auto onAccent = juce::Colour (0xff112627);
inline const auto modulation = juce::Colour (0xffb3a7f4);
} // namespace fx
namespace synth {
inline const auto accent = juce::Colour (0xffc3a9f7);
inline const auto onAccent = juce::Colour (0xff21192c);
} // namespace synth
} // namespace colour

namespace spacing {
inline constexpr int xs = 4;
inline constexpr int sm = 8;
inline constexpr int md = 12;
inline constexpr int lg = 16;
inline constexpr int xl = 20;
inline constexpr int xxl = 24;
inline constexpr int xxxl = 32;
} // namespace spacing

namespace radius {
inline constexpr float small = 4.0f;
inline constexpr float medium = 6.0f;
inline constexpr float large = 9.0f;
inline constexpr float panel = 10.0f;
} // namespace radius

namespace size {
inline constexpr int controlHeight = 32;
inline constexpr int tabHeight = 36;
inline constexpr int knobSmall = 36;
inline constexpr int knobNormal = 48;
inline constexpr int knobLarge = 64;
inline constexpr int instrumentSlotMinHeight = 54;
inline constexpr int instrumentSlotMaxHeight = 58;
inline constexpr int referenceWidth = 1440;
inline constexpr int referenceHeight = 1080;
inline constexpr int moduleHeaderHeight = 42;
} // namespace size

} // namespace vox::ui::tokens
