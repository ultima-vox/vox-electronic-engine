// Bass content handoff, checked with the unchanged host-owned #89 parser.
#include "bass/FullOnBassParameters.h"
#include "instrument/native/NativeModule.h"
#include "instrument/vins/VinsPackage.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstdlib>

namespace {
void check(bool ok, const std::string& why) {
    if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
}
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: bass_sound_layout_tests <absolute DLL path> <sound.json>\n";
        return 2;
    }
    namespace instrument = vstengine::instrument;
    std::string why;
    const auto module = instrument::native::NativeModule::acquire(argv[1], why);
    check(module != nullptr, "canonical loader: " + why);
    const instrument::InstrumentDescriptor* descriptor = nullptr;
    for (const auto& candidate : module->descriptors())
        if (candidate.id == vstengine::bass::fullOnInstrumentId) descriptor = &candidate;
    check(descriptor != nullptr, "Bass descriptor exists");
    const auto& canonical = vstengine::bass::fullOnParameters;
    check(descriptor->parameters.size() == canonical.size(), "13 independent parameters");
    for (std::size_t i = 0; i < canonical.size(); ++i) {
        const auto& actual = descriptor->parameters[i];
        const auto& expected = canonical[i];
        check(actual.id == expected.id && actual.unit == expected.unit
                  && actual.poolIndex == static_cast<int>(i)
                  && actual.type == instrument::ParameterType::floating
                  && actual.minimum == expected.minimum && actual.maximum == expected.maximum
                  && actual.defaultValue == expected.initial,
              "canonical ID/type/unit/range/default/pool: " + std::string(expected.id));
    }
    std::ifstream input(argv[2], std::ios::binary);
    check(input.is_open(), "open Bass SOUND layout");
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    instrument::SoundLayout layout;
    check(instrument::vins::parseSoundLayout(bytes, layout, why), "host parser: " + why);
    check(instrument::validateSoundLayout(layout, *descriptor, why), "host validation: " + why);
    std::size_t count = 0;
    for (const auto& section : layout.sections) count += section.controls.size();
    check(count == 13 && layout.sections.size() == 5, "five sections cover all 13 controls once");
    auto missing = layout;
    missing.sections.pop_back();
    check(!instrument::validateSoundLayout(missing, *descriptor, why), "missing control rejected");
    auto duplicate = layout;
    duplicate.sections.back().controls.push_back(layout.sections.front().controls.front());
    check(!instrument::validateSoundLayout(duplicate, *descriptor, why), "duplicate rejected");
    auto wrongType = layout;
    wrongType.sections.front().controls.front().kind = instrument::SoundControlKind::toggle;
    check(!instrument::validateSoundLayout(wrongType, *descriptor, why), "wrong type rejected");
    std::cout << "Bass SOUND layout: canonical 13-control mapping passed\n";
}
