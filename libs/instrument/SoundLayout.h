#pragma once
// Declarative SOUND page of an instrument (#82): sections of controls, each
// bound 1:1 to one real direct parameter. A .vins package ships it as a
// signed member (manifest "ui"); the host renders it with its own components
// through the slot's pool parameters. It holds no values and no behavior, so
// it is not part of the instrument's state or parameter identity.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vstengine::instrument {

struct InstrumentDescriptor;

inline constexpr int soundLayoutSchema = 1;
inline constexpr std::size_t maxSoundSections = 16;
inline constexpr std::size_t maxSoundTitle = 32;

enum class SoundControlKind : std::uint8_t {
  knob,     // floating or integer parameter
  selector, // choice parameter
  toggle    // boolean parameter
};

struct SoundControl {
  std::string parameter; // ParameterId of a direct parameter
  SoundControlKind kind{SoundControlKind::knob};
  std::string label; // empty: the parameter's name
};

struct SoundSection {
  std::string title;
  std::vector<SoundControl> controls;
};

struct SoundLayout {
  std::vector<SoundSection> sections;
};

// Checks a layout against the instrument it describes. Strict: the instrument
// has direct parameters; every control names one of them with a kind that
// matches its type; no parameter appears twice and every direct parameter
// appears once, so the page shows exactly the instrument's real controls.
[[nodiscard]] bool validateSoundLayout(const SoundLayout &, const InstrumentDescriptor &,
                                       std::string &why);

} // namespace vstengine::instrument
